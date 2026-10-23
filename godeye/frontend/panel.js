// GodEye's control panel (ADMIN): everything a server owner watches, on one page over the map. How the PC is doing
// (CPU, memory, graphics cards, disks), the game server, GodEye itself, the accounts and their roles, the settings,
// the credits and the logs. It asks /api/panel every 5 s while it is open.
"use strict";

let panelTimer = 0, logTimer = 0, logWhich = "godeye", panelData = null;

const fmtBytes = (b) => (b >= 1 << 30 ? (b / (1 << 30)).toFixed(1) + " GB" : (b / (1 << 20)).toFixed(0) + " MB");
const fmtAge = (s) => (s == null ? "never" : s < 60 ? `${s} s` : s < 3600 ? `${Math.floor(s / 60)} min` : s < 86400 ? `${Math.floor(s / 3600)} h ${Math.floor((s % 3600) / 60)} min` : `${Math.floor(s / 86400)} d`);
const fmtWhen = (iso) => (iso ? new Date(iso).toLocaleString([], { day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit" }) : "—");
const meter = (label, pct, text) => `<div class="pm"><span>${esc(label)}</span><progress max="100" value="${Math.max(0, Math.min(100, Math.round(pct)))}"></progress><b>${esc(text)}</b></div>`;

function openPanel() {
  $("panelView").hidden = false;
  $("panelZ").innerHTML = zMood("Inspired", "zport");
  refreshPanel();
  loadLogs();
  clearInterval(panelTimer); clearInterval(logTimer);
  panelTimer = setInterval(refreshPanel, 5000);
  logTimer = setInterval(loadLogs, 10000);
}
function closePanel() {
  $("panelView").hidden = true;
  clearInterval(panelTimer); clearInterval(logTimer);
}
$("panelClose").onclick = closePanel;

async function refreshPanel() {
  const r = await api("api/panel");
  if (!r.ok) { if (r.status === 401 || r.status === 403) closePanel(); return; }
  panelData = r.data;
  renderPanel(r.data);
}

function renderPanel(d) {
  const sys = d.system, used = sys.mem.total - sys.mem.free;
  $("pSystem").innerHTML = `<p class="pnote">${esc(sys.host)} · ${esc(sys.cpuName)} · up ${fmtAge(sys.uptime)}</p>`
    + meter("CPU", sys.cpu, `${sys.cpu}% of ${sys.cores} threads`)
    + meter("Memory", (used / sys.mem.total) * 100, `${fmtBytes(used)} / ${fmtBytes(sys.mem.total)}`)
    + sys.gpus.map((g, i) => meter(`GPU ${i}`, g.util, `${g.util}% · ${g.temp} °C`) + meter(g.name.replace("NVIDIA GeForce ", ""), (g.memUsed / g.memTotal) * 100, `${(g.memUsed / 1024).toFixed(1)} / ${(g.memTotal / 1024).toFixed(0)} GB`)).join("")
    + sys.disks.map((k) => meter(`Disk ${k.path}`, ((k.total - k.free) / k.total) * 100, `${fmtBytes(k.free)} free`)).join("");

  const sv = d.server, w = sv.world;
  $("pServer").innerHTML = `<p class="pstate ${sv.online ? "ok" : "bad"}">${sv.online ? "Connected" : "Not connected"}${sv.info ? ` · ${esc(sv.info.name)} · protocol v${sv.info.protocol}` : ""}</p>`
    + `<dl class="pdl"><dt>Players</dt><dd>${sv.players} online, ${sv.playing} in the world</dd>`
    + (sv.names.length ? `<dt>Who</dt><dd>${sv.names.map(esc).join(", ")}</dd>` : "")
    + (w ? `<dt>World</dt><dd>${String(w.hour).padStart(2, "0")}:${String(w.minute).padStart(2, "0")} · ${esc(w.weather)}${w.blackout ? " · BLACKOUT" : ""}</dd>` : "") + `</dl>`;

  const ge = d.godeye;
  $("pGodEye").innerHTML = `<dl class="pdl"><dt>Running</dt><dd>${fmtAge(ge.uptime)} · Node ${esc(ge.node)} · ${fmtBytes(ge.rss)}</dd>`
    + `<dt>Viewers</dt><dd>${ge.browsers} open page${ge.browsers === 1 ? "" : "s"}</dd>`
    + `<dt>Public address</dt><dd>${ge.lastPublic == null ? "no visit since GodEye started" : `last visit ${fmtAge(ge.lastPublic)} ago`}</dd>`
    + `<dt>Maps</dt><dd>2D ${ge.map2d ? "✓" : "missing"} · 3D ${ge.map3d ? `built ${esc(ge.map3d.replace(/^(\d{4})(\d\d)(\d\d)(\d\d)(\d\d).*/, "$3/$2/$1 $4:$5"))}` : "missing"}</dd>`
    + `<dt>Cameras</dt><dd>${ge.cameras} camera client${ge.cameras === 1 ? "" : "s"}${ge.video.length ? " · video " + ge.video.map((v) => `${esc(v.name)} ${v.live ? "live" : "waiting"}`).join(", ") : ""}</dd></dl>`;

  const roles = ["HACKER", "DISPATCH", "OPERATOR", "ADMIN"];
  $("pUsers").innerHTML = `<table class="ptable"><thead><tr><th>Name</th><th>Role</th><th>Created</th><th>Last login</th><th></th></tr></thead><tbody>`
    + d.users.sort((a, b) => a.name.localeCompare(b.name)).map((u) => `<tr data-user="${esc(u.name)}"><td><span class="pname">${zRole(u.role)}<b>${esc(u.name)}</b></span></td>`
      + `<td><select data-op="role" aria-label="Role of ${esc(u.name)}">${roles.map((r) => `<option${r === u.role ? " selected" : ""}>${r}</option>`).join("")}</select></td>`
      + `<td>${fmtWhen(u.created)}</td><td>${fmtWhen(u.lastLogin)}</td>`
      + `<td class="pact"><button data-op="password" class="ghost">Password</button><button data-op="delete" class="ghost danger">Delete</button></td></tr>`).join("")
    + `</tbody></table>`;

  const st = d.settings;
  if (!$("pSettings").contains(document.activeElement)) {   // never under the ADMIN's fingers
    $("setRegistration").value = st.registration;
    $("setMap3d").value = st.publicMap3d;
    for (const c of document.querySelectorAll("[data-signup-role]")) c.checked = st.signupRoles.includes(c.dataset.signupRole);
  }

  if (d.game) {
    const ev = d.game.event;
    $("pGame").innerHTML = (ev ? `<p class="pstate warn">⚡ ${esc(ev.label)} by ${esc(ev.by)} · ends ${new Date(ev.endsAt).toLocaleTimeString()} <button id="pStopEvent" class="ghost danger">Stop</button></p>` : `<p class="pnote">No event running.</p>`)
      + `<ol class="board">${d.game.board.map((b, i) => `<li><span>${i + 1}</span><b>${esc(b.user)}</b><i>${esc(b.role)}</i><em>◈ ${fmt(b.credits)}</em></li>`).join("")}</ol>`
      + `<div class="pledger">${d.game.recent.map((e) => `<div><time>${fmtWhen(e.at)}</time><b>${esc(e.user)}</b><span class="${e.delta < 0 ? "neg" : "pos"}">${e.delta > 0 ? "+" : ""}${e.delta}</span><i>${esc(e.why)}</i></div>`).join("")}</div>`;
    const stop = $("pStopEvent");
    if (stop) stop.onclick = () => act({ action: "event", value: "stop" }).then(refreshPanel);
  } else $("pGame").innerHTML = `<p class="pnote">The web game is off (game.enabled in godeye.json).</p>`;

  $("pAudit").innerHTML = d.audit.length ? d.audit.map((e) => `<div class="ev"><time>${fmtWhen(e.at)}</time><span><b>${esc(e.user)}</b> ${esc(describe(e))}${e.ok ? "" : " (refused)"}</span></div>`).join("") : `<p class="pnote">No actions yet.</p>`;
}

// accounts: role, password, delete (one row at a time)
$("pUsers").addEventListener("change", async (e) => {
  const sel = e.target.closest("[data-op=role]");
  if (!sel) return;
  const name = sel.closest("tr").dataset.user;
  const r = await api("api/panel/user", { op: "role", name, role: sel.value });
  toast(r.ok ? `${name} is now ${sel.value}` : r.data.error || "refused", !r.ok, r.ok ? "ok" : "error");
  refreshPanel();
});
$("pUsers").addEventListener("click", async (e) => {
  const b = e.target.closest("button[data-op]");
  if (!b) return;
  const row = b.closest("tr"), name = row.dataset.user;
  if (b.dataset.op === "delete") {
    if (!confirm(`Delete the account ${name}? Their credits stay in the ledger.`)) return;
    const r = await api("api/panel/user", { op: "delete", name });
    toast(r.ok ? `${name} deleted` : r.data.error || "refused", !r.ok, r.ok ? "ok" : "error");
    refreshPanel();
  } else if (b.dataset.op === "password") {
    const cell = b.parentElement;
    cell.innerHTML = `<form class="pinline"><input type="password" minlength="8" maxlength="200" placeholder="New password" autocomplete="new-password" required><button class="ghost">Save</button></form>`;
    clearInterval(panelTimer);   // no refresh while typing
    cell.querySelector("input").focus();
    cell.querySelector("form").onsubmit = async (ev) => {
      ev.preventDefault();
      const r = await api("api/panel/user", { op: "password", name, password: cell.querySelector("input").value });
      toast(r.ok ? `New password set for ${name}` : r.data.error || "refused", !r.ok, r.ok ? "ok" : "error");
      panelTimer = setInterval(refreshPanel, 5000);
      refreshPanel();
    };
  }
});
$("pNewUser").addEventListener("submit", async (e) => {
  e.preventDefault();
  const f = new FormData(e.target);
  const r = await api("api/panel/user", { op: "create", name: f.get("name"), role: f.get("role"), password: f.get("password") });
  toast(r.ok ? `Account ${f.get("name")} made` : r.data.error || "refused", !r.ok, r.ok ? "ok" : "error");
  if (r.ok) e.target.reset();
  refreshPanel();
});
$("pSettings").addEventListener("submit", async (e) => {
  e.preventDefault();
  const signupRoles = [...document.querySelectorAll("[data-signup-role]")].filter((c) => c.checked).map((c) => c.dataset.signupRole);
  const r = await api("api/panel/settings", { registration: $("setRegistration").value, publicMap3d: $("setMap3d").value, signupRoles });
  toast(r.ok ? "Settings saved" : r.data.error || "refused", !r.ok, r.ok ? "ok" : "error");
  document.activeElement?.blur();
  refreshPanel();
});

// logs: the last lines, GodEye's or the game server's
async function loadLogs() {
  const r = await api(`api/panel/logs?which=${logWhich}`);
  if (!r.ok) return;
  const pre = $("pLog"), atEnd = pre.scrollTop + pre.clientHeight >= pre.scrollHeight - 20;
  pre.textContent = r.data.lines.join("\n");
  $("pLogFile").textContent = r.data.file;
  if (atEnd) pre.scrollTop = pre.scrollHeight;
}
for (const b of document.querySelectorAll("[data-log]")) {
  b.onclick = () => {
    logWhich = b.dataset.log;
    for (const x of document.querySelectorAll("[data-log]")) x.classList.toggle("on", x === b);
    $("pLog").scrollTop = 1e9;
    loadLogs();
  };
}
