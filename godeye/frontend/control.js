// GodEye control (Phase 10): log in, then act on the game. The backend checks the role, the cooldown and every value;
// the buttons only show what this role may do and count the cooldowns down.
"use strict";

let auth = { user: null };

function toast(text, bad = false) {
  const t = $("toast");
  t.textContent = text;
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
    who.innerHTML = auth.control === false ? "" : `<button id="loginBtn" class="ghost">Log in</button>`;
    $("control").hidden = true;
    const b = $("loginBtn");
    if (b) b.onclick = () => { $("loginErr").textContent = ""; $("loginDlg").showModal(); };
    return;
  }
  who.innerHTML = `<span><b>${esc(auth.user)}</b> · ${esc(auth.role)}</span><button id="logoutBtn" class="ghost">Log out</button>`;
  $("logoutBtn").onclick = async () => { await api("api/logout", {}); auth = { user: null, control: true }; renderAuth(); };
  $("control").hidden = false;
  $("roleTag").textContent = auth.role;
  for (const g of document.querySelectorAll(".ctl")) g.hidden = !g.dataset.need.split(" ").some((a) => auth.can.includes(a));
  for (const b of document.querySelectorAll("[data-act]")) b.hidden = !auth.can.includes(b.dataset.act);
}

$("loginForm").addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const f = new FormData(ev.target);
  const r = await api("api/login", { name: f.get("name"), password: f.get("password") });
  if (!r.ok) { $("loginErr").textContent = r.data.error || "login failed"; return; }
  auth = r.data;
  ev.target.reset();
  $("loginDlg").close();
  renderAuth();
  toast(`Logged in as ${auth.user} (${auth.role})`);
  loadAudit();
});
$("loginCancel").onclick = () => $("loginDlg").close();

// ---- actions
const PLAYER_ACTIONS = new Set(["wanted", "alert", "roadblock"]);

async function act(payload) {
  if (PLAYER_ACTIONS.has(payload.action)) {
    if (selected == null || !players.get(selected)?.info.playing) { toast("Pick a player who is in the world first.", true); return; }
    payload.target = selected;
  }
  const r = await api("api/action", payload);
  if (!r.ok) { toast(r.data.error || "refused", true); if (r.status === 401) { auth = { user: null, control: true }; renderAuth(); } return; }
  auth.cooldowns = auth.cooldowns || {};
  auth.cooldowns[payload.action] = r.data.cooldownUntil;
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
    b.disabled = left > 0;
    b.textContent = left > 0 ? `${b.dataset.label} · ${left}s` : b.dataset.label;
  }
  const p = selected != null ? players.get(selected)?.info : null;
  $("targetName").textContent = p ? (p.playing ? p.name : `${p.name} (story mode)`) : "pick a player on the list";
}
setInterval(updateCooldowns, 500);

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
  if (auth.user) loadAudit();
})();
