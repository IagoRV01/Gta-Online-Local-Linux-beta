// GodEye backend. Phase 9: reads the game server's telemetry over its loopback admin API, names what it can (zone,
// vehicle, weapon) and pushes it to browsers over WebSocket, changes only. Serves the web app too.
// Phase 10: logged-in users act on the game (wanted, police, traffic, world) through checked server commands.
// Phase 14: the web game on top: Hack Credits, hacking minigames, daily limits, bounties and OPERATOR events.
// Accounts are made on the page (the first one, from the server's network, is the ADMIN), and the ADMIN has a control
// panel: the PC, the game server, GodEye, accounts, settings, credits and logs.
//   node src/main.ts [godeye.json]
// GodEye never talks to GTA, and browsers never talk to the game server.
import { appendFileSync, createReadStream, existsSync, readFileSync, mkdirSync } from "node:fs";
import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { createGzip, gzipSync } from "node:zlib";
import { ACTIONS, Limits, allowed, toCommand, type ActionRequest } from "./actions.ts";
import { AdminLink } from "./admin.ts";
import { LoginLimiter, ROLES, Sessions, SignupLimiter, Users, type Role } from "./auth.ts";
import { EVENTS, GAME_DEFAULTS, Game, eventLines, type GameConfig } from "./game.ts";
import { SIGNUP_ROLES, SettingsStore, system, tail } from "./panel.ts";
import { Puzzles } from "./puzzles.ts";
import { loadVehicles, vehicleName, weaponName } from "./names.ts";
import { Video, VIDEO_DEFAULTS, type VideoConfig } from "./video.ts";
import { acceptUpgrade, type WsClient } from "./ws.ts";
import { Zones } from "./zones.ts";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, "..");           // godeye/backend
const FRONT = resolve(ROOT, "../frontend");

// ---- config
interface Config {
  http: { bind: string; port: number };
  admin: { host: string; port: number; secretFile: string };
  mapFile: string;
  map3dDir: string;
  satDir: string;                  // the 2D satellite tiles (tools/godeye-sat), made like the 3D map from the game files
  logFile: string;
  usersFile: string;
  settingsFile: string;            // what the ADMIN changes on the control panel
  serverLogFile: string;           // the game server's log, shown on the control panel
  registration: "open" | "closed"; // the first value of the sign-up setting
  publicMap3d: "login" | "open";   // the 3D map on the public address: login (default) or open
  video: VideoConfig;              // Phase 13: the camera clients as live video
  game: GameConfig;                // Phase 14: credits, minigames, limits, events
  allowNetworks: string[];
}
const cfgPath = resolve(process.argv[2] ?? join(ROOT, "godeye.json"));
const defaults: Config = {
  http: { bind: "0.0.0.0", port: 8080 },
  admin: { host: "127.0.0.1", port: 4500, secretFile: "D:/GTALocalOnline/data/admin.secret" },
  mapFile: "../data/map.json",
  map3dDir: "../data/map3d",
  satDir: "../data/sat",
  logFile: "D:/GTALocalOnline/logs/godeye.log",
  usersFile: "users.json",
  settingsFile: "settings.json",
  serverLogFile: "D:/GTALocalOnline/logs/game-server.log",
  registration: "open",
  publicMap3d: "login",
  video: VIDEO_DEFAULTS,
  game: GAME_DEFAULTS,
  allowNetworks: ["127.0.0.0/8", "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"],
};
let cfg: Config = defaults;
if (existsSync(cfgPath)) {
  const c = JSON.parse(readFileSync(cfgPath, "utf8"));
  cfg = { ...defaults, ...c, http: { ...defaults.http, ...c.http }, admin: { ...defaults.admin, ...c.admin }, video: { ...defaults.video, ...c.video },
    game: { ...defaults.game, ...c.game, prices: { ...defaults.game.prices, ...c.game?.prices }, daily: { ...defaults.game.daily, ...c.game?.daily }, puzzles: { ...defaults.game.puzzles, ...c.game?.puzzles } } };
}
const rel = (p: string) => resolve(dirname(cfgPath), p);

function log(level: string, msg: string): void {
  const line = `${new Date().toISOString()} ${level} godeye ${msg}`;
  console.log(line);
  try { mkdirSync(dirname(cfg.logFile), { recursive: true }); appendFileSync(cfg.logFile, line + "\n"); } catch { /* console only */ }
}

// ---- LAN only: browsers from outside the private networks are refused
function ipv4(addr: string): number | null {
  const m = /^(?:::ffff:)?(\d+)\.(\d+)\.(\d+)\.(\d+)$/.exec(addr);
  return m ? ((+m[1] << 24) | (+m[2] << 16) | (+m[3] << 8) | +m[4]) >>> 0 : null;
}
const nets = cfg.allowNetworks.map((n) => {
  const [a, bits] = n.split("/");
  const mask = Number(bits) === 0 ? 0 : (~0 << (32 - Number(bits))) >>> 0;
  return { base: (ipv4(a) ?? 0) & mask, mask };
});
// The public address (godeye/cloudflare): the Worker marks every request it forwards through the tunnel. Viewing is open
// there; the 3D map (made of the game's own models and textures) and the controls need a GodEye login.
function isPublic(req: IncomingMessage): boolean { return req.headers["x-godeye-public"] === "1"; }
function clientAddr(req: IncomingMessage): string {
  return isPublic(req) ? String(req.headers["cf-connecting-ip"] ?? "public") : req.socket.remoteAddress ?? "";
}
function hostOf(req: IncomingMessage): string {
  return isPublic(req) ? String(req.headers["x-forwarded-host"] ?? "") : String(req.headers["host"] ?? "");
}

function lanAllowed(addr: string | undefined): boolean {
  if (!addr) return false;
  if (addr === "::1") return true;
  const v = ipv4(addr);
  return v !== null && nets.some((n) => ((v & n.mask) >>> 0) === n.base >>> 0);
}

// ---- map data (made locally from the game files; optional: without it the page shows a grid)
const zones = new Zones();
let mapGz: Buffer | null = null;
const mapFile = rel(cfg.mapFile);
if (existsSync(mapFile)) {
  const raw = readFileSync(mapFile);
  mapGz = gzipSync(raw);
  try { zones.load(JSON.parse(raw.toString("utf8")).zones ?? []); } catch (e) { log("WARN", `map.json: ${e}`); }
  log("INFO", `map ${mapFile}: ${(raw.length / 1024) | 0} KB (${(mapGz.length / 1024) | 0} KB gzipped), ${zones.count} zone boxes`);
} else log("WARN", `no ${mapFile}: run tools/godeye-map to make it (see godeye/README.md)`);
const zoneLabelsJson = JSON.stringify(zones.labels());
log("INFO", `${loadVehicles(join(ROOT, "data/vehicles.txt"))} vehicle names`);

// ---- state: the server's view plus names, as the browsers get it
interface World { hour: number; minute: number; frozen: boolean; weather: string; next: string; auto: boolean; blackout: boolean }
const players = new Map<number, any>();
let world: World | null = null;
let serverInfo: any = null;
let cameras: any[] = [];   // the camera clients (Phase 12) and what each shows
let cctv: any[] = [];      // the CCTV cameras they can show

function enrich(p: any): any {
  const out: any = { ...p };
  if (p.playing) {
    const z = zones.nameAt(p.x, p.y, p.z);
    out.zone = z.name; out.zoneCode = z.code;
    out.weaponName = weaponName(p.weapon);
    if (p.veh) { const v = vehicleName(p.veh.model); out.veh = { ...p.veh, name: v.name, cls: v.cls }; }
  }
  return out;
}

const browsers = new Set<WsClient>();
function broadcast(msg: object): void {
  const text = JSON.stringify(msg);
  for (const b of browsers) b.send(text);
}
function fullState(): object {
  return { t: "full", online: admin.connected, server: serverInfo, world, players: [...players.values()], cameras, cctv, gameEvent: game?.publicEvent() ?? null };
}

const admin = new AdminLink(cfg.admin);
admin.on("log", (m: string) => log("WARN", m));
admin.on("up", (hello: any) => {
  serverInfo = { name: hello.server, protocol: hello.protocol, telemetry: hello.telemetry };
  cctv = Array.isArray(hello.cctv) ? hello.cctv : [];
  log("INFO", `connected to the game server "${hello.server}" (protocol v${hello.protocol}, telemetry ${hello.telemetry} Hz)`);
});
admin.on("down", () => {
  log("WARN", "lost the game server; retrying");
  players.clear();
  broadcast({ t: "full", online: false, server: serverInfo, world, players: [] });
});
admin.on("msg", (m: any) => {
  if (m.t !== "tel") return;
  if (m.full) players.clear();
  if (m.world) world = m.world;
  if (m.cameras) cameras = m.cameras;
  const changed: any[] = [];
  for (const p of m.players ?? []) { const e = enrich(p); players.set(p.id, e); changed.push(e); }
  for (const id of m.gone ?? []) players.delete(id);
  for (const b of game?.checkBounties((id) => players.get(id)?.deaths) ?? []) {   // Phase 14: the police got them
    const entry: AuditEntry = { at: new Date().toISOString(), user: b.user, role: "", action: "bounty", line: `bounty ${b.target} ${b.reward}`, ok: true };
    audit.push(entry); if (audit.length > 200) audit.shift();
    log("INFO", `bounty: ${b.user} earns ${b.reward} credits (player ${b.target} died)`);
    broadcast({ t: "event", entry });
  }
  if (m.full) broadcast(fullState());
  else broadcast({ t: "tel", ms: m.ms, world: m.world, cameras: m.cameras, players: changed, gone: m.gone ?? [] });
});
admin.start();

// ---- live video of the camera clients (Phase 13)
for (const k of ["mediamtx", "mediamtxConfig", "ffmpeg"] as const) if (cfg.video[k]) cfg.video[k] = rel(cfg.video[k]);
const video = new Video(cfg.video, log);
video.start();

// ---- accounts and control (Phase 10)
const users = new Users(rel(cfg.usersFile));
const sessions = new Sessions();
const loginLimit = new LoginLimiter();
const signupLimit = new SignupLimiter();
const settings = new SettingsStore(rel(cfg.settingsFile), { registration: cfg.registration, signupRoles: SIGNUP_ROLES, publicMap3d: cfg.publicMap3d });
const startedAt = Date.now();
let lastPublicAt = 0;   // the last request through the public address
const limits = new Limits();
// ---- the web game (Phase 14)
cfg.game.ledgerFile = rel(cfg.game.ledgerFile);
const game = cfg.game.enabled ? new Game(cfg.game, log) : null;
const puzzles = new Puzzles();
log("INFO", `${users.count} GodEye users in ${rel(cfg.usersFile)}${users.count ? "" : " (none yet: the first account made on this PC or its LAN becomes the ADMIN)"}; sign-up ${settings.value.registration}`);
interface AuditEntry { at: string; user: string; role: string; action: string; line: string; ok: boolean }
const audit: AuditEntry[] = [];
function record(user: string, role: string, action: string, line: string, ok: boolean): void {
  const entry: AuditEntry = { at: new Date().toISOString(), user, role, action, line, ok };
  audit.push(entry); if (audit.length > 200) audit.shift();
  if (ok) broadcast({ t: "event", entry: { ...entry, label: ACTIONS[action]?.label ?? action } });
}

// every minute the open pages earn credits while someone is in the world; every second, a finished event is undone
if (game) {
  setInterval(() => game.income([...players.values()].some((p) => p.playing)), 60000);
  setInterval(async () => {
    const ev = game.event;
    if (!ev || Date.now() < ev.endsAt || !admin.connected || endingEvent) return;
    endingEvent = true;
    for (const l of ev.end) await admin.command(l);
    game.endEvent();
    endingEvent = false;
    log("INFO", `event "${ev.label}" ended`);
    record("GodEye", "", "event", `eventEnd ${ev.key}`, true);
    broadcast({ t: "game", event: null });
  }, 1000);
}
let endingEvent = false;

function readBody(req: IncomingMessage): Promise<any> {
  return new Promise((done) => {
    let size = 0, chunks: Buffer[] = [];
    req.on("data", (c: Buffer) => { size += c.length; if (size > 4096) { req.destroy(); done(null); } else chunks.push(c); });
    req.on("end", () => { try { done(JSON.parse(Buffer.concat(chunks).toString("utf8"))); } catch { done(null); } });
    req.on("error", () => done(null));
  });
}

// a POST must come from our own page: same Origin as the Host, and (once logged in) the session's CSRF token
function sameOrigin(req: IncomingMessage): boolean {
  const origin = req.headers["origin"];
  if (!origin) return false;
  try { return new URL(String(origin)).host === hostOf(req); } catch { return false; }
}

function me(s: ReturnType<Sessions["get"]>, pub = false): object {
  const map3d = !pub || settings.value.publicMap3d === "open" || !!s;
  // setup: no account yet, so this visitor (on the server's network) can make the ADMIN one
  const signup = users.count > 0 && settings.value.registration === "open" ? settings.value.signupRoles : [];
  if (!s) return { user: null, control: users.count > 0, signup, setup: users.count === 0 && !pub, public: pub, map3d };
  const can = Object.keys(ACTIONS).filter((a) => allowed(s.role, a) && (a !== "event" || !!game));
  return { user: s.user, role: s.role, csrf: s.csrf, can, cooldowns: limits.cooldowns(s.user), public: pub, map3d, game: !!game, panel: s.role === "ADMIN" };
}

async function handlePost(req: IncomingMessage, res: ServerResponse, path: string): Promise<void> {
  const json = (code: number, body: object, extra: Record<string, string> = {}) => send(res, code, "application/json", JSON.stringify(body), extra);
  if (!sameOrigin(req)) { json(403, { error: "cross-site request refused" }); return; }
  const addr = clientAddr(req), pub = isPublic(req);
  const body = await readBody(req);
  if (!body || typeof body !== "object") { json(400, { error: "bad request" }); return; }
  if (path === "/api/login") {
    if (loginLimit.blocked(addr)) { json(429, { error: "too many failed logins: wait 5 minutes" }); return; }
    const u = users.check(String(body.name ?? "").slice(0, 32), String(body.password ?? "").slice(0, 200));
    if (!u) { loginLimit.fail(addr); log("WARN", `failed login for "${String(body.name ?? "").slice(0, 32)}" from ${addr}`); json(401, { error: "wrong name or password" }); return; }
    loginLimit.ok(addr);
    users.touchLogin(u.name);
    const s = sessions.create(u);
    log("INFO", `${u.name} (${u.role}) logged in from ${addr}`);
    json(200, me(s, pub), { "Set-Cookie": sessions.cookie(s, pub) });
    return;
  }
  if (path === "/api/register") {   // a new account: the ADMIN one on a fresh install, then the roles the ADMIN allows
    const name = String(body.name ?? ""), password = String(body.password ?? "");
    const first = users.count === 0;
    if (first && pub) { json(403, { error: "make the first (ADMIN) account on the server PC or its network" }); return; }
    if (!first && settings.value.registration !== "open") { json(403, { error: "sign-up is closed on this GodEye" }); return; }
    const role = (first ? "ADMIN" : String(body.role ?? "")) as Role;
    if (!first && !settings.value.signupRoles.includes(role)) { json(400, { error: "pick one of the roles offered" }); return; }
    if (!signupLimit.allowed(addr)) { json(429, { error: "too many new accounts from here: try again in an hour" }); return; }
    if (users.count >= 1000) { json(403, { error: "this GodEye is full" }); return; }
    const bad = users.create(name, role, password);
    if (bad) { json(400, { error: bad }); return; }
    signupLimit.made1(addr);
    const u = users.get(name)!;
    users.touchLogin(u.name);
    const s = sessions.create(u);
    log("INFO", `new account ${u.name} (${u.role}) from ${addr}${first ? ": the first one, ADMIN" : ""}`);
    json(200, me(s, pub), { "Set-Cookie": sessions.cookie(s, pub) });
    return;
  }
  const s = sessions.get(req.headers["cookie"]);
  if (!s || req.headers["x-csrf-token"] !== s.csrf) { json(401, { error: "log in first" }); return; }
  if (path === "/api/logout") { sessions.end(s); json(200, { user: null }, { "Set-Cookie": sessions.cookie(null, pub) }); return; }
  if (path === "/api/action") {
    const r = body as ActionRequest & { puzzle?: { id?: unknown; answer?: unknown } };
    const action = String(r.action ?? "");
    if (!allowed(s.role, action) || (action === "event" && !game)) { json(403, { error: `${s.role} cannot use ${action || "that"}` }); return; }
    const cmd = toCommand(r, (id) => !!players.get(id)?.playing);
    if (!cmd.line) { json(400, { error: cmd.error }); return; }
    // an event: its own commands, one at a time
    let ev: { start: string[]; end: string[] } | null = null;
    const target = r.target == null || r.target === "all" ? null : Number(r.target);
    if (action === "event" && r.value === "stop") {   // end the running event now: whoever started it, or ADMIN
      const e = game!.event;
      if (!e) { json(409, { error: "no event is running" }); return; }
      if (s.role !== "ADMIN" && e.by.toLowerCase() !== s.user.toLowerCase()) { json(403, { error: `only ${e.by} or an ADMIN can stop it` }); return; }
      e.endsAt = Date.now();   // the event timer runs its end commands within a second
      record(s.user, s.role, action, `eventStop ${e.key}`, true);
      log("INFO", `event "${e.label}" stopped by ${s.user}`);
      json(200, { ok: true });
      return;
    }
    const lim = limits.check(s.user, s.role, action);
    if (!lim.ok) { json(429, { error: `cooling down: ${lim.waitS} s`, waitS: lim.waitS }); return; }
    if (!admin.connected) { json(503, { error: "the game server is offline" }); return; }
    if (action === "event") {
      const def = EVENTS[String(r.value)];
      if (!def) { json(400, { error: "unknown event" }); return; }
      if (game!.event) { json(409, { error: `"${game!.event.label}" is still running` }); return; }
      ev = eventLines(String(r.value), def.target ? target : null, def.target && target != null ? players.get(target)?.name ?? "" : "");
      if (!ev) { json(400, { error: "pick a player who is in the world first" }); return; }
    }
    // the game: credits and daily limits, then a hacking minigame for some actions
    let price = 0;
    if (game) {
      const q = game.quote(s.user, s.role, r);
      if (q.error) { json(402, { error: q.error }); return; }
      price = q.price;
      if (q.puzzle) {
        if (!r.puzzle) { json(202, { puzzle: puzzles.make(s.user, cmd.line, q.puzzle), price }); return; }
        const res = puzzles.solve(s.user, cmd.line, r.puzzle.id, r.puzzle.answer);
        if (res !== "ok") {
          const until = limits.block(s.user, action, 15);
          log("INFO", `minigame failed by ${s.user} (${s.role}) for "${cmd.line}": ${res}`);
          json(403, { error: res === "expired" ? "ACCESS DENIED: too slow" : "ACCESS DENIED", denied: true, cooldownUntil: until });
          return;
        }
      }
    }
    let ok: boolean;
    if (ev) { ok = true; for (const l of ev.start) ok = (await admin.command(l)) && ok; ok = ok || !ev.start.length; }
    else ok = await admin.command(cmd.line);
    const until = ok ? limits.used(s.user, s.role, action) : 0;
    if (ok && game) {
      game.charge(s.user, s.role, r, price);
      if (action === "wanted" && target != null) game.addBounty(s.user, target, players.get(target)?.deaths ?? 0, price);
      if (ev) {
        const e = game.startEvent(String(r.value), s.user, target, target != null ? players.get(target)?.name ?? "" : "", ev.end);
        log("INFO", `event "${e.label}" started by ${s.user}, until ${new Date(e.endsAt).toLocaleTimeString()}`);
        broadcast({ t: "game", event: game.publicEvent() });
      }
    }
    record(s.user, s.role, action, cmd.line, ok);
    log(ok ? "INFO" : "WARN", `action by ${s.user} (${s.role}): ${cmd.line}${price ? ` for ${price} credits` : ""} -> ${ok ? "ok" : "refused by the game server"}`);
    json(ok ? 200 : 409, ok ? { ok, cooldownUntil: until, credits: game?.balance(s.user) } : { error: "the game server refused it" });
    return;
  }
  // ---- the control panel (ADMIN)
  if (path.startsWith("/api/panel/") && s.role !== "ADMIN") { json(403, { error: "ADMIN only" }); return; }
  if (path === "/api/panel/user") {
    const op = String(body.op ?? ""), name = String(body.name ?? "");
    const target = users.get(name);
    if (op === "create") {
      const role = String(body.role ?? "") as Role;
      if (!ROLES.includes(role)) { json(400, { error: "unknown role" }); return; }
      const bad = users.create(name, role, String(body.password ?? ""));
      if (bad) { json(400, { error: bad }); return; }
      log("INFO", `${s.user} made the account ${name} (${role})`);
      json(200, { ok: true });
      return;
    }
    if (!target) { json(404, { error: "no such user" }); return; }
    const lastAdmin = target.role === "ADMIN" && users.admins() <= 1;
    if (op === "role") {
      const role = String(body.role ?? "") as Role;
      if (!ROLES.includes(role)) { json(400, { error: "unknown role" }); return; }
      if (lastAdmin && role !== "ADMIN") { json(409, { error: "that is the last ADMIN" }); return; }
      users.setRole(target.name, role);
      sessions.updateUser(target.name, role);
      log("INFO", `${s.user} set ${target.name}'s role to ${role}`);
      json(200, { ok: true });
      return;
    }
    if (op === "password") {
      const bad = users.setPassword(target.name, String(body.password ?? ""));
      if (bad) { json(400, { error: bad }); return; }
      if (target.name.toLowerCase() !== s.user.toLowerCase()) sessions.updateUser(target.name, null);   // logged out everywhere
      log("INFO", `${s.user} set a new password for ${target.name}`);
      json(200, { ok: true });
      return;
    }
    if (op === "delete") {
      if (lastAdmin) { json(409, { error: "that is the last ADMIN" }); return; }
      users.remove(target.name); users.save();
      sessions.updateUser(target.name, null);
      log("INFO", `${s.user} deleted the account ${target.name}`);
      json(200, { ok: true });
      return;
    }
    json(400, { error: "unknown operation" });
    return;
  }
  if (path === "/api/panel/settings") {
    const v = settings.update({ registration: body.registration, signupRoles: body.signupRoles, publicMap3d: body.publicMap3d });
    log("INFO", `${s.user} changed the settings: sign-up ${v.registration} (${v.signupRoles.join(", ")}), public 3D ${v.publicMap3d}`);
    json(200, v);
    return;
  }
  if (path === "/api/game/grant") {   // ADMIN: give (or take) credits
    if (!game || s.role !== "ADMIN") { json(403, { error: "ADMIN only" }); return; }
    const who = users.list().find((u) => u.name.toLowerCase() === String(body.user ?? "").toLowerCase());
    const amount = Number(body.amount);
    if (!who || !Number.isInteger(amount) || amount === 0 || Math.abs(amount) > 100000) { json(400, { error: "a GodEye user and a whole amount up to 100000" }); return; }
    game.grant(who.name, amount, s.user);
    log("INFO", `${s.user} granted ${amount} credits to ${who.name}`);
    json(200, { ok: true, user: who.name, credits: game.balance(who.name) });
    return;
  }
  json(404, { error: "not found" });
}

// ---- HTTP: the web app, the map data, a JSON snapshot
const STATIC: Record<string, [string, string]> = {
  "/": ["index.html", "text/html; charset=utf-8"],
  "/index.html": ["index.html", "text/html; charset=utf-8"],
  "/app.js": ["app.js", "text/javascript; charset=utf-8"],
  "/control.js": ["control.js", "text/javascript; charset=utf-8"],
  "/map3d.js": ["map3d.js", "text/javascript; charset=utf-8"],
  "/video.js": ["video.js", "text/javascript; charset=utf-8"],
  "/game.js": ["game.js", "text/javascript; charset=utf-8"],
  "/zorua.js": ["zorua.js", "text/javascript; charset=utf-8"],
  "/panel.js": ["panel.js", "text/javascript; charset=utf-8"],
  "/vendor/three/three.module.min.js": ["vendor/three/three.module.min.js", "text/javascript; charset=utf-8"],
  "/style.css": ["style.css", "text/css; charset=utf-8"],
};
const SECURITY_HEADERS = {
  "Content-Security-Policy": "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; connect-src 'self'; media-src 'self' blob:; worker-src 'self' blob:; frame-ancestors 'none'",
  "X-Content-Type-Options": "nosniff",
  "Referrer-Policy": "no-referrer",
  "Cache-Control": "no-store",
};

function send(res: ServerResponse, code: number, type: string, body: string | Buffer, extra: Record<string, string> = {}): void {
  res.writeHead(code, { "Content-Type": type, ...SECURITY_HEADERS, ...extra });
  res.end(body);
}

const http = createServer((req: IncomingMessage, res: ServerResponse) => {
  if (!lanAllowed(req.socket.remoteAddress)) { send(res, 403, "text/plain", "LAN only\n"); return; }
  if (isPublic(req)) lastPublicAt = Date.now();
  const path = new URL(req.url ?? "/", "http://x").pathname;
  if (path.startsWith("/video/")) {   // Phase 13: WHEP signalling and HLS, relayed to mediamtx
    if (req.method !== "GET" && !sameOrigin(req)) { send(res, 403, "text/plain", "cross-site request refused\n"); return; }
    if (video.handle(req, res, path, SECURITY_HEADERS)) return;
    send(res, 404, "text/plain", "not found\n");
    return;
  }
  if (path === "/api/video") { send(res, 200, "application/json", JSON.stringify(video.streams)); return; }
  if (req.method === "POST" && path.startsWith("/api/")) { handlePost(req, res, path).catch((e) => { log("ERROR", `POST ${path}: ${e}`); send(res, 500, "text/plain", "error\n"); }); return; }
  if (req.method !== "GET" && req.method !== "HEAD") { send(res, 405, "text/plain", "method not allowed\n"); return; }
  const st = STATIC[path];
  if (st) {
    try { send(res, 200, st[1], readFileSync(join(FRONT, st[0]))); } catch { send(res, 500, "text/plain", "missing file\n"); }
    return;
  }
  // the Zorua art (tools/zorua/fetch_zorua.py; optional, never committed): fixed names only
  const zr = /^\/zorua\/([A-Za-z]{1,12}(?:-[A-Za-z]{1,12})?\.png|manifest\.json|CREDITS\.md)$/.exec(path);
  if (zr) {
    const file = join(FRONT, "zorua", zr[1]);
    if (!existsSync(file)) { send(res, 404, "text/plain", "not found\n"); return; }
    const type = zr[1].endsWith(".png") ? "image/png" : zr[1].endsWith(".json") ? "application/json" : "text/plain; charset=utf-8";
    send(res, 200, type, readFileSync(file), { "Cache-Control": "max-age=86400" });
    return;
  }
  if (path === "/data/map.json") {
    if (!mapGz) { send(res, 404, "application/json", "{}"); return; }
    if (/\bgzip\b/.test(String(req.headers["accept-encoding"] ?? ""))) send(res, 200, "application/json", mapGz, { "Content-Encoding": "gzip", "Cache-Control": "max-age=3600" });
    else send(res, 200, "application/json", readFileSync(mapFile), { "Cache-Control": "max-age=3600" });
    return;
  }
  // the 3D map: only the names tools/godeye-map writes (no paths from the request reach the file system)
  const m3 = /^\/data\/map3d\/(index\.json|t_-?\d{1,3}_-?\d{1,3}\.(?:far|near|tree)\.bin|t_-?\d{1,3}_-?\d{1,3}\.(?:far|near)\.tex|tex\/[0-9a-f]{8}\.bc1)$/.exec(path);
  if (m3 && isPublic(req) && settings.value.publicMap3d !== "open" && !sessions.get(req.headers["cookie"])) {
    send(res, 401, "application/json", JSON.stringify({ error: "log in to see the 3D map" }));
    return;
  }
  if (m3) {
    const file = join(rel(cfg.map3dDir), m3[1]);
    if (!existsSync(file)) { send(res, 404, "text/plain", "not found\n"); return; }
    const type = m3[1].endsWith(".json") ? "application/json" : "application/octet-stream";
    // files carry the export's stamp (?v=): they never change, so browsers and Cloudflare may keep them for good
    const versioned = m3[1] !== "index.json" && new URL(req.url ?? "/", "http://x").searchParams.has("v");
    const headers: Record<string, string> = { "Content-Type": type, ...SECURITY_HEADERS, Vary: "Accept-Encoding",
      "Cache-Control": m3[1] === "index.json" ? "no-cache" : versioned ? "public, max-age=31536000, immutable" : "max-age=3600" };
    // the exporter's Brotli copy when the browser takes it (https, the public address); gzip on the fly on the LAN;
    // nothing for this PC's own window, where compressing only costs time
    const ae = String(req.headers["accept-encoding"] ?? "");
    const br = /\bbr\b/.test(ae) && existsSync(file + ".br");
    const local = !isPublic(req) && /^(::1|::ffff:127\.|127\.)/.test(req.socket.remoteAddress ?? "");
    const gz = !br && !local && /\bgzip\b/.test(ae);
    if (br) headers["Content-Encoding"] = "br";
    else if (gz) headers["Content-Encoding"] = "gzip";
    res.writeHead(200, headers);
    const stream = createReadStream(br ? file + ".br" : file);
    stream.on("error", () => res.destroy());
    (gz ? stream.pipe(createGzip({ level: 1 })) : stream).pipe(res);
    return;
  }
  // the 2D satellite map: the same rules as the 3D one (it is made of the same game files)
  const sat = /^\/data\/sat\/(index\.json|[0-9]\/-?\d{1,4}_-?\d{1,4}\.webp)$/.exec(path);
  if (sat) {
    if (isPublic(req) && settings.value.publicMap3d !== "open" && !sessions.get(req.headers["cookie"])) {
      send(res, 401, "application/json", JSON.stringify({ error: "log in to see the satellite map" }));
      return;
    }
    const file = join(rel(cfg.satDir), sat[1]);
    if (!existsSync(file)) { send(res, 404, "text/plain", "not found\n"); return; }
    const versioned = sat[1] !== "index.json" && new URL(req.url ?? "/", "http://x").searchParams.has("v");
    send(res, 200, sat[1].endsWith(".json") ? "application/json" : "image/webp", readFileSync(file),
      { "Cache-Control": sat[1] === "index.json" ? "no-cache" : versioned ? "public, max-age=31536000, immutable" : "max-age=3600" });
    return;
  }
  if (path === "/api/me") { send(res, 200, "application/json", JSON.stringify(me(sessions.get(req.headers["cookie"]), isPublic(req)))); return; }
  if (path === "/api/game") {   // Phase 14: this user's credits, prices, limits; the event; the leaderboard
    const s = sessions.get(req.headers["cookie"]);
    if (!s) { send(res, 401, "application/json", JSON.stringify({ error: "log in first" })); return; }
    if (!game) { send(res, 200, "application/json", JSON.stringify({ enabled: false })); return; }
    game.touch(s.user);
    const can = Object.keys(ACTIONS).filter((a) => allowed(s.role, a));
    const roles = new Map(users.list().map((u) => [u.name.toLowerCase(), u.role]));
    send(res, 200, "application/json", JSON.stringify({ enabled: true, ...game.status(s.user, s.role, can), event: game.publicEvent(), board: game.board((k) => roles.get(k)) }));
    return;
  }
  if (path === "/api/panel" || path === "/api/panel/logs") {   // the control panel (ADMIN)
    const s = sessions.get(req.headers["cookie"]);
    if (!s || s.role !== "ADMIN") { send(res, s ? 403 : 401, "application/json", JSON.stringify({ error: "ADMIN only" })); return; }
    if (path === "/api/panel/logs") {
      const which = new URL(req.url ?? "/", "http://x").searchParams.get("which");
      const file = which === "server" ? cfg.serverLogFile : cfg.logFile;
      send(res, 200, "application/json", JSON.stringify({ file, lines: tail(file, 250) }));
      return;
    }
    const all = [...players.values()];
    const m3 = join(rel(cfg.map3dDir), "index.json");
    let built3d = "";
    try { built3d = JSON.parse(readFileSync(m3, "utf8")).built ?? ""; } catch { /* no 3D map */ }
    send(res, 200, "application/json", JSON.stringify({
      system: system([cfg.logFile, cfg.usersFile, cfg.map3dDir].map((p) => resolve(dirname(cfgPath), p))),
      server: { online: admin.connected, info: serverInfo, world, players: all.length, playing: all.filter((p) => p.playing).length, names: all.map((p) => p.name) },
      godeye: {
        uptime: Math.round((Date.now() - startedAt) / 1000), node: process.version, rss: process.memoryUsage().rss,
        browsers: browsers.size, lastPublic: lastPublicAt ? Math.round((Date.now() - lastPublicAt) / 1000) : null,
        map2d: !!mapGz, map3d: built3d, video: video.streams, cameras: cameras.length,
      },
      settings: settings.value,
      users: users.list(),
      game: game ? { board: game.board((k) => users.get(k)?.role), event: game.publicEvent(), recent: game.recent(30) } : null,
      audit: audit.slice(-30).reverse(),
    }));
    return;
  }
  if (path === "/api/audit") {
    const s = sessions.get(req.headers["cookie"]);
    if (!s) { send(res, 401, "application/json", JSON.stringify({ error: "log in first" })); return; }
    send(res, 200, "application/json", JSON.stringify(audit.slice(-50).reverse()));
    return;
  }
  if (path === "/api/zones") { send(res, 200, "application/json", zoneLabelsJson); return; }
  if (path === "/api/state") { send(res, 200, "application/json", JSON.stringify(fullState())); return; }
  if (path === "/api/health") { send(res, 200, "application/json", JSON.stringify({ ok: true, server: admin.connected, browsers: browsers.size })); return; }
  send(res, 404, "text/plain", "not found\n");
});

http.on("upgrade", (req: IncomingMessage, sock, _head) => {
  const addr = clientAddr(req);
  if (!lanAllowed(req.socket.remoteAddress) || new URL(req.url ?? "/", "http://x").pathname !== "/ws") { sock.end("HTTP/1.1 403 Forbidden\r\n\r\n"); return; }
  if (browsers.size >= 32) { sock.end("HTTP/1.1 503 Service Unavailable\r\n\r\n"); return; }
  const ws = acceptUpgrade(req, sock, addr, hostOf(req));
  if (!ws) return;
  browsers.add(ws);
  log("INFO", `browser ${addr} connected (${browsers.size} open)`);
  ws.onClose = () => { browsers.delete(ws); log("INFO", `browser ${addr} left (${browsers.size} open)`); };
  ws.onMessage = (text: string) => {
    let m: any;
    try { m = JSON.parse(text); } catch { return; }
    if (m?.t === "full") ws.send(JSON.stringify(fullState()));
  };
  ws.send(JSON.stringify(fullState()));
});

http.listen(cfg.http.port, cfg.http.bind, () => log("INFO", `GodEye on http://${cfg.http.bind}:${cfg.http.port} (LAN only), game server admin API ${cfg.admin.host}:${cfg.admin.port}`));
http.on("error", (e) => { log("ERROR", `http: ${e.message}`); process.exit(1); });
process.on("SIGINT", () => { log("INFO", "stopping"); process.exit(0); });
