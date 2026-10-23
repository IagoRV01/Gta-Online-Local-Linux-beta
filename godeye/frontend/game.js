// GodEye as a web game (Phase 14): Hack Credits, the hacking minigames, OPERATOR events and the leaderboard.
// The backend decides everything (prices, daily limits, each puzzle's answer); this page shows it and plays the puzzles.
"use strict";

let gs = null;          // /api/game for the logged-in user
let gameEvent = null;   // the running event, for every viewer

const EVENT_NAMES = { storm: "Electrical storm", gridlock: "Gridlock", manhunt: "Manhunt", happyhour: "Happy hour" };
const fmt = (n) => Number(n).toLocaleString("en-US").replace(/,/g, " ");

async function loadGame() {
  if (!auth.user || !auth.game) { gs = null; $("gameBox").hidden = true; return; }
  const r = await api("api/game");
  if (!r.ok || !r.data.enabled) { gs = null; $("gameBox").hidden = true; return; }
  gs = r.data;
  renderGame();
  onGameEvent(gs.event);
}
setInterval(() => { if (auth.user) loadGame(); }, 15000);   // also what earns the per-minute income

function onCredits(c) { if (gs) { gs.credits = c; $("credits").textContent = gs.free ? "free" : `◈ ${fmt(c)}`; } }

function renderGame() {
  $("gameBox").hidden = false;
  $("credits").textContent = gs.free ? "free" : `◈ ${fmt(gs.credits)}`;
  const left = Object.entries(gs.left).map(([a, n]) => `${a} ${n}`).join(" · ");
  $("gameLimits").textContent = gs.free ? "ADMIN acts free (adminPlays is off)." : left ? `Left today: ${left}` : "";
  $("gameIncome").textContent = gs.free ? "" : `+${gs.income} a minute while this page is open and someone plays, plus a daily bonus.`;
  $("board").innerHTML = gs.board.length ? gs.board.map((b, i) =>
    `<li${b.user.toLowerCase() === auth.user.toLowerCase() ? ' class="me"' : ""}><span>${i + 1}</span><b>${esc(b.user)}</b><i>${esc(b.role)}</i><em>◈ ${fmt(b.credits)}</em></li>`).join("") : `<li class="empty">Nobody has played yet.</li>`;
  // OPERATOR: the events
  $("eventCtl").hidden = !gs.events.length;
  $("eventBtns").innerHTML = gs.events.map((e) =>
    `<button data-event="${esc(e.key)}" title="${esc(e.blurb)} · ${e.minutes} min${e.target ? " · on the selected player" : ""}">${esc(e.label)}${e.cost ? ` ◈${fmt(e.cost)}` : ""}</button>`).join("")
    + (gs.event && (auth.role === "ADMIN" || gs.event.by.toLowerCase() === auth.user.toLowerCase()) ? `<button id="eventStop" class="red">Stop "${esc(gs.event.label)}"</button>` : "");
  if ($("eventStop")) $("eventStop").onclick = () => act({ action: "event", value: "stop" });
  for (const b of document.querySelectorAll("[data-event]")) {
    b.onclick = () => {
      const e = gs.events.find((x) => x.key === b.dataset.event);
      const p = { action: "event", value: e.key };
      if (e.target) {
        if (selected == null || !players.get(selected)?.info.playing) { toast("Pick a player who is in the world first.", true); return; }
        p.target = selected;
      }
      act(p);
    };
  }
  // ADMIN: give credits
  $("grantForm").hidden = auth.role !== "ADMIN";
  if (auth.role === "ADMIN") {
    const pick = $("grantUser"), keep = pick.value;
    pick.innerHTML = gs.board.map((b) => `<option>${esc(b.user)}</option>`).join("");
    if ([...pick.options].some((o) => o.value === keep)) pick.value = keep;
  }
  updateCooldowns();
}

$("grantForm").addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const r = await api("api/game/grant", { user: $("grantUser").value, amount: Number($("grantAmount").value) });
  toast(r.ok ? `${r.data.user} now has ◈ ${fmt(r.data.credits)}` : r.data.error || "refused", !r.ok);
  if (r.ok) loadGame();
});

// the price a control button shows (the backend charges its own, the same)
function priceOf(act, value) {
  if (!gs || gs.free) return 0;
  if ((act === "traffic" && value === "normal") || (act === "blackout" && value === "off") || (act === "wanted" && !/^\+/.test(value || ""))) return 0;
  const base = gs.prices[act] ?? 0;
  return act === "wanted" ? base * Number(value.slice(1)) : base;
}
const needsHack = (act, value) => !!gs && gs.puzzles.includes(act) && priceOf(act, value) > 0;

// ---- the running event: a banner over the map for everyone, counting down
function onGameEvent(e) {
  gameEvent = e;
  const b = $("evBanner");
  b.hidden = !e;
  if (e) tickEvent();
}
function tickEvent() {
  if (!gameEvent) return;
  const s = Math.max(0, Math.round((gameEvent.endsAt - Date.now()) / 1000));
  const extra = gameEvent.priceX < 1 ? " · hacks half price" : gameEvent.bountyX > 1 ? ` · bounties ×${gameEvent.bountyX}` : "";
  $("evBanner").textContent = `⚡ ${gameEvent.label.toUpperCase()}${gameEvent.targetName ? ` · ${gameEvent.targetName}` : ""}${extra} · ${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}
setInterval(tickEvent, 1000);

// ---- the hacking minigames. Resolves with the answer, or null when given up or out of time.
function hack(p, price) {
  return new Promise((done) => {
    const dlg = $("hackDlg"), body = $("hackBody");
    let over = false, timer = 0;
    const end = (answer) => {
      if (over) return;
      over = true; clearInterval(timer); dlg.close(); done(answer);
    };
    $("hackTitle").textContent = p.title;
    $("hackCost").textContent = price ? `◈ ${fmt(price)} on success` : "";
    const t0 = Date.now();
    const tick = () => {
      const left = Math.max(0, p.seconds - (Date.now() - t0) / 1000);
      $("hackTime").textContent = `00:${String(Math.ceil(left)).padStart(2, "0")}`;
      $("hackTime").classList.toggle("low", left < 6);
      if (left <= 0) end(null);
    };
    tick(); timer = setInterval(tick, 200);
    if (p.kind === "sequence") {
      $("hackGoal").textContent = "CONNECT: find the missing node";
      body.innerHTML = `<div class="seq">${p.terms.map((t) => (t === null ? `<b class="q">?</b>` : `<span>${t}</span>`)).join("<i>→</i>")}</div>`
        + `<div class="opts">${p.options.map((o) => `<button data-o="${o}">${o}</button>`).join("")}</div>`;
      for (const b of body.querySelectorAll("[data-o]")) b.onclick = () => end(Number(b.dataset.o));
    } else {
      $("hackGoal").textContent = "ROUTE: link IN to OUT around the firewalls";
      route(p, body, end);
    }
    $("hackAbort").onclick = () => end(null);
    dlg.oncancel = (e) => { e.preventDefault(); end(null); };
    dlg.showModal();
  });
}

function route(p, body, end) {
  const S = 64, R = 15, pad = 34;
  const at = (n) => [pad + (n % p.w) * S, pad + Math.floor(n / p.w) * S];
  const linked = new Set(p.links.map(([a, b]) => `${Math.min(a, b)}-${Math.max(a, b)}`));
  const locked = new Set(p.locked);
  const path = [p.start];
  const W = pad * 2 + (p.w - 1) * S, H = pad * 2 + (p.h - 1) * S;
  const draw = () => {
    const on = new Set(path);
    const lines = p.links.map(([a, b]) => {
      const [x1, y1] = at(a), [x2, y2] = at(b);
      const i = path.indexOf(a), j = path.indexOf(b);
      const used = i >= 0 && j >= 0 && Math.abs(i - j) === 1;
      return `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" class="${used ? "used" : ""}"/>`;
    }).join("");
    const last = path[path.length - 1];
    const nodes = Array.from({ length: p.w * p.h }, (_, n) => {
      const [x, y] = at(n);
      const can = !on.has(n) && !locked.has(n) && linked.has(`${Math.min(n, last)}-${Math.max(n, last)}`);
      const cls = locked.has(n) ? "lock" : n === p.start ? "in" : n === p.end ? "out" : on.has(n) ? "on" : can ? "can" : "";
      const label = locked.has(n) ? "✕" : n === p.start ? "IN" : n === p.end ? "OUT" : String(n + 1);
      return `<g class="node ${cls}" data-n="${n}"><circle cx="${x}" cy="${y}" r="${R}"/><text x="${x}" y="${y + 4}">${label}</text></g>`;
    }).join("");
    body.innerHTML = `<svg class="route" viewBox="0 0 ${W} ${H}" role="img" aria-label="Network">${lines}${nodes}</svg><p class="hint">Click linked nodes one by one. Click the last one again to step back.</p>`;
    for (const g of body.querySelectorAll("[data-n]")) g.onclick = () => pick(Number(g.dataset.n));
  };
  const pick = (n) => {
    const last = path[path.length - 1];
    if (n === last && path.length > 1) { path.pop(); draw(); return; }
    if (path.includes(n) || locked.has(n) || !linked.has(`${Math.min(n, last)}-${Math.max(n, last)}`)) return;
    path.push(n);
    if (n === p.end) { draw(); setTimeout(() => end(path.slice()), 250); return; }
    draw();
  };
  draw();
}

function hackFlash(ok) {
  const f = $("hackFlash");
  f.textContent = ok ? "ACCESS GRANTED" : "ACCESS DENIED";
  f.className = "hackflash show " + (ok ? "ok" : "bad");
  clearTimeout(hackFlash.t);
  hackFlash.t = setTimeout(() => { f.className = "hackflash"; }, 1400);
}
