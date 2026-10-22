// GodEye backend (Phase 9: GodEye Basic). Reads the game server's telemetry over its loopback admin API, names what it
// can (zone, vehicle, weapon) and pushes it to browsers over WebSocket, changes only. Serves the web app too.
//   node src/main.ts [godeye.json]
// GodEye never talks to GTA, and browsers never talk to the game server.
import { appendFileSync, existsSync, readFileSync, mkdirSync } from "node:fs";
import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { gzipSync } from "node:zlib";
import { AdminLink } from "./admin.ts";
import { loadVehicles, vehicleName, weaponName } from "./names.ts";
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
  logFile: string;
  allowNetworks: string[];
}
const cfgPath = resolve(process.argv[2] ?? join(ROOT, "godeye.json"));
const defaults: Config = {
  http: { bind: "0.0.0.0", port: 8080 },
  admin: { host: "127.0.0.1", port: 4500, secretFile: "D:/GTALocalOnline/data/admin.secret" },
  mapFile: "../data/map.json",
  logFile: "D:/GTALocalOnline/logs/godeye.log",
  allowNetworks: ["127.0.0.0/8", "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"],
};
let cfg: Config = defaults;
if (existsSync(cfgPath)) {
  const c = JSON.parse(readFileSync(cfgPath, "utf8"));
  cfg = { ...defaults, ...c, http: { ...defaults.http, ...c.http }, admin: { ...defaults.admin, ...c.admin } };
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
function allowed(addr: string | undefined): boolean {
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
  return { t: "full", online: admin.connected, server: serverInfo, world, players: [...players.values()] };
}

const admin = new AdminLink(cfg.admin);
admin.on("log", (m: string) => log("WARN", m));
admin.on("up", (hello: any) => {
  serverInfo = { name: hello.server, protocol: hello.protocol, telemetry: hello.telemetry };
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
  const changed: any[] = [];
  for (const p of m.players ?? []) { const e = enrich(p); players.set(p.id, e); changed.push(e); }
  for (const id of m.gone ?? []) players.delete(id);
  if (m.full) broadcast(fullState());
  else broadcast({ t: "tel", ms: m.ms, world: m.world, players: changed, gone: m.gone ?? [] });
});
admin.start();

// ---- HTTP: the web app, the map data, a JSON snapshot
const STATIC: Record<string, [string, string]> = {
  "/": ["index.html", "text/html; charset=utf-8"],
  "/index.html": ["index.html", "text/html; charset=utf-8"],
  "/app.js": ["app.js", "text/javascript; charset=utf-8"],
  "/style.css": ["style.css", "text/css; charset=utf-8"],
};
const SECURITY_HEADERS = {
  "Content-Security-Policy": "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'",
  "X-Content-Type-Options": "nosniff",
  "Referrer-Policy": "no-referrer",
  "Cache-Control": "no-store",
};

function send(res: ServerResponse, code: number, type: string, body: string | Buffer, extra: Record<string, string> = {}): void {
  res.writeHead(code, { "Content-Type": type, ...SECURITY_HEADERS, ...extra });
  res.end(body);
}

const http = createServer((req: IncomingMessage, res: ServerResponse) => {
  if (!allowed(req.socket.remoteAddress)) { send(res, 403, "text/plain", "LAN only\n"); return; }
  if (req.method !== "GET" && req.method !== "HEAD") { send(res, 405, "text/plain", "method not allowed\n"); return; }
  const path = new URL(req.url ?? "/", "http://x").pathname;
  const st = STATIC[path];
  if (st) {
    try { send(res, 200, st[1], readFileSync(join(FRONT, st[0]))); } catch { send(res, 500, "text/plain", "missing file\n"); }
    return;
  }
  if (path === "/data/map.json") {
    if (!mapGz) { send(res, 404, "application/json", "{}"); return; }
    if (/\bgzip\b/.test(String(req.headers["accept-encoding"] ?? ""))) send(res, 200, "application/json", mapGz, { "Content-Encoding": "gzip", "Cache-Control": "max-age=3600" });
    else send(res, 200, "application/json", readFileSync(mapFile), { "Cache-Control": "max-age=3600" });
    return;
  }
  if (path === "/api/zones") { send(res, 200, "application/json", zoneLabelsJson); return; }
  if (path === "/api/state") { send(res, 200, "application/json", JSON.stringify(fullState())); return; }
  if (path === "/api/health") { send(res, 200, "application/json", JSON.stringify({ ok: true, server: admin.connected, browsers: browsers.size })); return; }
  send(res, 404, "text/plain", "not found\n");
});

http.on("upgrade", (req: IncomingMessage, sock, _head) => {
  const addr = req.socket.remoteAddress ?? "";
  if (!allowed(addr) || new URL(req.url ?? "/", "http://x").pathname !== "/ws") { sock.end("HTTP/1.1 403 Forbidden\r\n\r\n"); return; }
  if (browsers.size >= 32) { sock.end("HTTP/1.1 503 Service Unavailable\r\n\r\n"); return; }
  const ws = acceptUpgrade(req, sock, addr);
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
