// GodEye control (Phase 10): log in, then act on the game. The backend checks the role, the cooldown and every value;
// the buttons only show what this role may do and count the cooldowns down.
"use strict";

let auth = { user: null };

// mood: which Zorua says it (zorua.js); nothing without the sprites
function toast(text, bad = false, mood = bad ? "error" : "") {
  const t = $("toast");
  t.innerHTML = (mood ? zMood(mood, "zport") : "") + `<span>${esc(text)}</span>`;
  t.className = "toast show" + (bad ? " bad" : "");
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { t.className = "toast"; }, 3500);
}

async function api(path, body) {
  const r = await fetch(path, {
    method: body ? "POST" : "GET",
    headers: body ? { "Content-Type": "application/json", ...(auth.csrf ? { "X-CSRF-Token": auth.csrf } : {}) } : {},
    body: body ? JSON.stringify(body) : undefined,
    credentials: "same-origin",
  });
  let data = {};
  try { data = await r.json(); } catch { /* empty */ }
  return { ok: r.ok, status: r.status, data };
}

function renderAuth() {
  const who = $("who");
  if (!auth.user) {
    const canSignup = !!auth.setup || !!auth.signup?.length;
    who.innerHTML = auth.control === false && !canSignup ? "" : `<button id="loginBtn" class="ghost">${auth.setup ? "Set up GodEye" : canSignup ? "Log in · Sign up" : "Log in"}</button>`;
    $("control").hidden = true;
    $("gameBox").hidden = true;
    if (typeof closePanel === "function") closePanel();
    const b = $("loginBtn");
    if (b) b.onclick = () => openAuth(auth.setup ? "signup" : "login");
    return;
  }
  who.innerHTML = `${zRole(auth.role)}<span><b>${esc(auth.user)}</b> · ${esc(auth.role)}</span>${auth.panel ? `<button id="panelBtn" class="ghost">Panel</button>` : ""}<button id="logoutBtn" class="ghost">Log out</button>`;
  if (auth.panel) $("panelBtn").onclick = () => ($("panelView").hidden ? openPanel() : closePanel());
  $("logoutBtn").onclick = async () => { await api("api/logout", {}); const r = await api("api/me"); auth = r.data || { user: null }; renderAuth(); };
  $("control").hidden = false;
  if (typeof loadGame === "function") loadGame();   // Phase 14: credits, events, leaderboard
  $("roleTag").textContent = auth.role;
  for (const g of document.querySelectorAll(".ctl")) g.hidden = !g.dataset.need.split(" ").some((a) => auth.can.includes(a));
  for (const b of document.querySelectorAll("[data-act]")) b.hidden = !auth.can.includes(b.dataset.act);
}

// the access dialog: log in, or make an account (the first one is the ADMIN; then a side to play)
const ROLE_BLURB = {
  HACKER: "Traffic lights, blackouts and cameras: hack the city.",
  DISPATCH: "Wanted stars, police alerts and roadblocks: hunt the players.",
  OPERATOR: "Weather, time, cameras, messages and events: run the show.",
};
let authMode = "login";
function openAuth(mode) {
  authMode = mode;
  const signup = mode === "signup", roles = auth.setup ? [] : auth.signup || [];
  for (const b of document.querySelectorAll("[data-auth]")) b.classList.toggle("on", b.dataset.auth === mode);
  $("authTabs").hidden = !!auth.setup || !roles.length || auth.control === false;
  $("setupNote").hidden = !auth.setup;
  $("pw2Row").hidden = !signup;
  $("rolePick").hidden = !signup || !roles.length;
  $("roleChoices").innerHTML = roles.map((r, i) => `<label class="rolecard">${zRole(r)}<input type="radio" name="role" value="${r}"${i ? "" : " checked"}><span><b>${r}</b><small>${esc(ROLE_BLURB[r] || "")}</small></span></label>`).join("");
  $("loginGo").textContent = !signup ? "Log in" : auth.setup ? "Make the ADMIN account" : "Create account";
  $("loginForm").password.autocomplete = signup ? "new-password" : "current-password";
  $("loginErr").textContent = "";
  zSet("loginZ", "login");
  if (!$("loginDlg").open) $("loginDlg").showModal();
}
for (const b of document.querySelectorAll("[data-auth]")) b.onclick = () => openAuth(b.dataset.auth);

$("loginForm").addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const f = new FormData(ev.target);
  let r;
  if (authMode === "signup") {
    if (f.get("password") !== f.get("password2")) { $("loginErr").textContent = "the two passwords differ"; zSet("loginZ", "loginBad"); return; }
    r = await api("api/register", { name: f.get("name"), password: f.get("password"), role: f.get("role") || "" });
  } else r = await api("api/login", { name: f.get("name"), password: f.get("password") });
  if (!r.ok) { $("loginErr").textContent = r.data.error || "login failed"; zSet("loginZ", "loginBad"); return; }
  auth = r.data;
  ev.target.reset();
  $("loginDlg").close();
  renderAuth();
  toast(authMode === "signup" ? `Welcome, ${auth.user}! You play as ${auth.role}.` : `Logged in as ${auth.user} (${auth.role})`, false, "welcome");
  loadAudit();
  loadGame();
});
$("loginCancel").onclick = () => $("loginDlg").close();

// ---- actions
const PLAYER_ACTIONS = new Set(["wanted", "alert", "roadblock"]);

async function act(payload) {
  if (PLAYER_ACTIONS.has(payload.action)) {
    if (selected == null || !players.get(selected)?.info.playing) { toast("Pick a player who is in the world first.", true); return; }
    payload.target = selected;
  }
  let r = await api("api/action", payload);
  if (r.status === 202 && r.data.puzzle) {   // Phase 14: a hacking minigame first
    const answer = await hack(r.data.puzzle, r.data.price);
    if (answer == null) { toast("Hack aborted.", true, "aborted"); return; }
    r = await api("api/action", { ...payload, puzzle: { id: r.data.puzzle.id, answer } });
    hackFlash(r.ok, r.ok ? "granted" : /slow/.test(r.data.error || "") ? "slow" : "denied");
  }
  auth.cooldowns = auth.cooldowns || {};
  if (r.data.cooldownUntil) auth.cooldowns[payload.action] = r.data.cooldownUntil;
  const mood = { 402: "credits", 429: "cooldown", 503: "offline", 403: r.data.denied ? "denied" : "error" }[r.status] ?? "error";
  if (!r.ok) { toast(r.data.error || "refused", true, mood); updateCooldowns(); if (r.status === 401) { auth = { user: null, control: true }; renderAuth(); } return; }
  if (r.data.credits != null && typeof onCredits === "function") onCredits(r.data.credits);
  if (typeof loadGame === "function") loadGame();
  updateCooldowns();
}

for (const b of document.querySelectorAll("[data-act]")) {
  b.addEventListener("click", () => {
    const p = { action: b.dataset.act, value: b.dataset.value };
    if (p.action === "traffic") p.seconds = Number($("trafficSecs").value);
    act(p);
  });
}

$("msgForm").addEventListener("submit", (ev) => {
  ev.preventDefault();
  const text = $("msgText").value.trim();
  if (!text) return;
  const target = selected != null && players.get(selected)?.info.playing ? selected : "all";
  act({ action: "notify", target, text }).then(() => { $("msgText").value = ""; });
});

function updateCooldowns() {
  if (!auth.user) return;
  const now = Date.now();
  for (const b of document.querySelectorAll("[data-act]")) {
    const until = auth.cooldowns?.[b.dataset.act] ?? 0;
    const left = Math.ceil((until - now) / 1000);
    if (!b.dataset.label) b.dataset.label = b.textContent;
    const price = typeof priceOf === "function" ? priceOf(b.dataset.act, b.dataset.value) : 0;   // Phase 14
    b.disabled = left > 0;
    b.textContent = left > 0 ? `${b.dataset.label} · ${left}s` : b.dataset.label + (price ? ` ◈${price}` : "");
    b.title = typeof needsHack === "function" && needsHack(b.dataset.act, b.dataset.value) ? "Needs a hack first" : "";
  }
  const p = selected != null ? players.get(selected)?.info : null;
  $("targetName").textContent = p ? (p.playing ? p.name : `${p.name} (story mode)`) : "pick a player on the list";
}
setInterval(updateCooldowns, 500);

// ---- cameras (Phase 12): which camera client, then a CCTV camera or a way to frame the selected player
let camList = [], cctvList = [];
function onCameras(cams, cctv) {
  camList = cams;
  if (cctv) {
    cctvList = cctv;
    $("cctvBtns").innerHTML = cctv.map((c) => `<button data-cctv="${esc(c.id)}" title="${esc(c.zone)}">${esc(c.id.replace("CAM_", ""))} ${esc(c.name)}</button>`).join("");
    for (const b of document.querySelectorAll("[data-cctv]")) b.onclick = () => act({ action: "camera", camera: camTarget(), value: "cctv", text: b.dataset.cctv });
  }
  const pick = $("camPick"), keep = pick.value;
  pick.innerHTML = `<option value="all">All cameras</option>` + cams.map((c) => `<option value="${c.id}">${esc(c.name)}</option>`).join("");
  pick.value = [...pick.options].some((o) => o.value === keep) ? keep : "all";
  $("camState").innerHTML = cams.length ? cams.map((c) => `<span><b>${esc(c.name)}</b> · ${esc(c.label)}</span>`).join("") : "No camera client is connected.";
}
const camTarget = () => ($("camPick").value === "all" ? "all" : Number($("camPick").value));
for (const b of document.querySelectorAll("[data-cam]")) {
  b.addEventListener("click", () => {
    if (selected == null || !players.get(selected)?.info.playing) { toast("Pick a player who is in the world first.", true); return; }
    act({ action: "camera", camera: camTarget(), value: b.dataset.cam, target: selected });
  });
}

// ---- the event feed (every viewer sees what GodEye did)
const events = [];
function onGodEyeEvent(e) {
  events.unshift(e);
  if (events.length > 30) events.pop();
  $("events").innerHTML = events.map((x) => {
    const t = new Date(x.at);
    const hhmm = `${String(t.getHours()).padStart(2, "0")}:${String(t.getMinutes()).padStart(2, "0")}`;
    return `<div class="ev"><time>${hhmm}</time><span><b>${esc(x.user)}</b> ${esc(describe(x))}</span></div>`;
  }).join("");
}
function describe(e) {
  const [verb, a, ...rest] = e.line.split(" ");
  const name = (id) => players.get(Number(id))?.info.name ?? `player ${id}`;
  switch (verb) {
    case "wanted": return `set ${name(a)}'s wanted level ${rest[0]}`;
    case "alert": return `sent a police alert to ${name(a)}`;
    case "roadblock": return `set up a roadblock ahead of ${name(a)}`;
    case "traffic": return a === "normal" ? "put the traffic lights back to normal" : `turned every traffic light ${a.toUpperCase()}${rest[0] > 0 ? ` for ${rest[0]} s` : ""}`;
    case "blackout": return a === "on" ? "cut the city's power" : "turned the power back on";
    case "weather": return a === "auto" ? "let the weather run on its own" : `changed the weather to ${a}`;
    case "time": return `set the time to ${a} ${rest.join(":")}`;
    case "notify": return `sent a message to ${a === "all" ? "everyone" : name(a)}`;
    case "event": return a === "manhunt" ? `started a manhunt on ${name(rest[0])}` : `started the event "${EVENT_NAMES[a] ?? a}"`;
    case "eventEnd": return `"${EVENT_NAMES[a] ?? a}" is over`;
    case "eventStop": return `stopped "${EVENT_NAMES[a] ?? a}"`;
    case "bounty": return `collected a ${rest[0]}-credit bounty on ${name(a)}`;
    case "camera": return rest[0] === "cctv" ? `switched ${a === "all" ? "the cameras" : "a camera"} to ${rest[1]}` : `put ${a === "all" ? "the cameras" : "a camera"} on ${name(rest[1])} (${rest[0]})`;
  }
  return e.line;
}

async function loadAudit() {
  const a = await api("api/audit");
  if (!a.ok) return;
  events.length = 0;
  for (const e of a.data.slice().reverse()) onGodEyeEvent(e);
}

(async () => {
  const r = await api("api/me");
  auth = r.data || { user: null };
  renderAuth();
  if (auth.map3d === false && is3d) setView(false);   // a remembered 3D view on the public address, before logging in
  if (auth.user) loadAudit();
})();
