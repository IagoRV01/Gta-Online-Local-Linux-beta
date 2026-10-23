// GodEye web app (Phase 9): live 2D map of the players. Telemetry comes over WebSocket at up to 10 Hz (changes only);
// markers are interpolated at the display's frame rate, 150 ms behind the newest sample.
"use strict";

const $ = (id) => document.getElementById(id);
const canvas = $("map"), ctx = canvas.getContext("2d");
const INTERP_DELAY = 150;

// ---------------------------------------------------------------- view (world metres <-> screen pixels)
const view = { cx: 0, cy: 800, s: 0.08, w: 0, h: 0, dpr: 1 };
const toScreen = (x, y) => [(x - view.cx) * view.s + view.w / 2, (view.cy - y) * view.s + view.h / 2];
const toWorld = (sx, sy) => [(sx - view.w / 2) / view.s + view.cx, view.cy - (sy - view.h / 2) / view.s];
let baseDirty = true;
let follow = null;   // player id the view tracks
let selected = null;

function resize() {
  const r = canvas.getBoundingClientRect();
  view.dpr = window.devicePixelRatio || 1;
  view.w = r.width; view.h = r.height;
  canvas.width = Math.round(r.width * view.dpr); canvas.height = Math.round(r.height * view.dpr);
  baseCanvas.width = canvas.width; baseCanvas.height = canvas.height;
  baseDirty = true;
}

// ---------------------------------------------------------------- map data
let map = null, terrain = null, zoneLabels = [];
const ROAD_BUCKET = 512;
const roadBuckets = new Map();   // "bx,by" -> [Float32Array per class]

async function loadMap() {
  try {
    const r = await fetch("data/map.json", { cache: "no-cache" });
    if (!r.ok) throw new Error(r.status);
    map = await r.json();
  } catch { $("nomap").hidden = false; return; }
  buildTerrain();
  const tmp = new Map();
  const R = map.roads;
  for (let k = 0; k < R.length; k += 5) {
    const key = Math.floor((R[k] + R[k + 2]) / 2 / ROAD_BUCKET) + "," + Math.floor((R[k + 1] + R[k + 3]) / 2 / ROAD_BUCKET);
    let b = tmp.get(key);
    if (!b) tmp.set(key, b = [[], [], [], []]);
    b[R[k + 4]].push(R[k], R[k + 1], R[k + 2], R[k + 3]);
  }
  for (const [k, b] of tmp) roadBuckets.set(k, b.map((a) => new Float32Array(a)));
  try { zoneLabels = await (await fetch("api/zones")).json(); } catch { zoneLabels = []; }
  baseDirty = true;
}

// terrain grid -> one pixel per 25 m cell: water, and land shaded by height with light from the north-west
function buildTerrain() {
  const g = map.grid, W = g.w, H = g.h, z = g.z;
  const c = document.createElement("canvas");
  c.width = W; c.height = H;
  const img = c.getContext("2d").createImageData(W, H);
  const at = (i, j) => { i = Math.max(0, Math.min(W - 1, i)); j = Math.max(0, Math.min(H - 1, j)); const v = z[j * W + i]; return v < 0 ? 0 : v; };
  for (let j = 0; j < H; j++) {
    for (let i = 0; i < W; i++) {
      const v = z[j * W + i], o = ((H - 1 - j) * W + i) * 4;
      if (v < 0) { img.data[o] = 10; img.data[o + 1] = 26; img.data[o + 2] = 40; img.data[o + 3] = 255; continue; }
      const dx = (at(i + 1, j) - at(i - 1, j)) / (2 * g.cell), dy = (at(i, j + 1) - at(i, j - 1)) / (2 * g.cell);
      const shade = Math.max(-1, Math.min(1, (-dx + dy) * 1.6));        // lit from the north-west
      const e = Math.min(1, v / 700);
      const base = [30 + e * 34, 38 + e * 30, 36 + e * 22];
      const k = 1 + shade * 0.55;
      img.data[o] = base[0] * k; img.data[o + 1] = base[1] * k; img.data[o + 2] = base[2] * k; img.data[o + 3] = 255;
    }
  }
  c.getContext("2d").putImageData(img, 0, 0);
  terrain = c;
}

// ---------------------------------------------------------------- base layer (redrawn only when the view changes)
const baseCanvas = document.createElement("canvas"), bctx = baseCanvas.getContext("2d");
const ROAD_STYLE = [   // dirt, street, main, motorway: colour, width at 1 px/m, min width, min zoom
  ["#5a4b3a", 4, 0.6, 0.18],
  ["#3c4d5e", 6, 0.6, 0.07],
  ["#6c7f92", 7, 1.0, 0.0],
  ["#c98a45", 8, 1.3, 0.0],
];

// ---- the satellite map (tools/godeye-sat): the game's own models seen from above, in 256 px tiles of 1 to 32 m a
// pixel. The sea is transparent; tiles load as the view needs them, a coarser level fills in meanwhile.
let satIndex = null;
const satImgs = new Map();   // "L/tx_ty" -> Image
(async () => {
  try {
    const r = await fetch("data/sat/index.json", { cache: "no-cache" });
    if (!r.ok) return;
    const j = await r.json();
    satIndex = { ...j, sets: j.tiles.map((l) => new Set(l)) };
    $("lySatRow").hidden = false;
    $("lyRoads").checked = false;   // the picture shows the roads; the drawn network is there to switch on
    baseDirty = true;
  } catch { /* no satellite map: the terrain shading stays */ }
})();
function satTile(L, tx, ty) {
  const key = `${L}/${tx}_${ty}`;
  let img = satImgs.get(key);
  if (!img) {
    img = new Image();
    img.onload = () => { baseDirty = true; };
    img.src = `data/sat/${key}.webp?v=${satIndex.built}`;
    satImgs.set(key, img);
    if (satImgs.size > 900) satImgs.delete(satImgs.keys().next().value);   // forget the oldest
  }
  return img.complete && img.naturalWidth ? img : null;
}
function drawSat(c) {
  const s = view.s, want = Math.log2(1 / (s * view.dpr));
  const L = Math.max(0, Math.min(satIndex.levels - 1, Math.floor(want)));
  const [x0, y1] = toWorld(0, 0), [x1, y0] = toWorld(view.w, view.h);
  c.imageSmoothingEnabled = true;
  c.imageSmoothingQuality = "high";
  for (const lv of [...new Set([Math.min(satIndex.levels - 1, L + 2), L])]) {   // coarse first, then the sharp one
    const S = T_SAT * 2 ** lv;
    for (let tx = Math.floor(x0 / S); tx <= Math.floor(x1 / S); tx++)
      for (let ty = Math.floor(y0 / S); ty <= Math.floor(y1 / S); ty++) {
        if (!satIndex.sets[lv].has(`${tx}_${ty}`)) continue;
        const img = satTile(lv, tx, ty);
        if (!img) continue;
        const [sx, sy] = toScreen(tx * S, (ty + 1) * S);
        c.drawImage(img, sx, sy, S * s + 0.5, S * s + 0.5);   // half a pixel over: no seams between tiles
      }
  }
}
const T_SAT = 256;

function drawBase() {
  const c = bctx, s = view.s;
  if (view.w < 1 || view.h < 1 || !(s > 0)) return;   // a hidden pane has no size yet: nothing to draw
  c.setTransform(view.dpr, 0, 0, view.dpr, 0, 0);
  const sat = satIndex && $("lySat").checked;
  c.fillStyle = sat ? "#0e4a6b" : "#0a1a28";   // with the satellite map, the sea is sea blue
  c.fillRect(0, 0, view.w, view.h);
  if (sat) drawSat(c);
  else if (map && terrain && $("lyTerrain").checked) {
    const g = map.grid;
    const [sx, sy] = toScreen(g.x0, g.y0 + g.h * g.cell);
    c.imageSmoothingEnabled = true;
    c.drawImage(terrain, sx, sy, g.w * g.cell * s, g.h * g.cell * s);
  } else if (!map) drawGrid(c);
  if (map && $("lyRoads").checked) drawRoads(c);
  if (map && $("lyLights").checked && s > 0.25) {
    c.fillStyle = "#ff5a5a";
    const L = map.lights, r = Math.max(1.5, Math.min(4, s * 3));
    for (let k = 0; k < L.length; k += 2) {
      const [x, y] = toScreen(L[k], L[k + 1]);
      if (x < -5 || y < -5 || x > view.w + 5 || y > view.h + 5) continue;
      c.beginPath(); c.arc(x, y, r, 0, 7); c.fill();
    }
  }
  if ($("lyZones").checked && zoneLabels.length) drawZoneLabels(c);
}

function drawGrid(c) {
  c.strokeStyle = "#13283a"; c.lineWidth = 1;
  const step = view.s > 0.3 ? 250 : 1000;
  const [x0, y1] = toWorld(0, 0), [x1, y0] = toWorld(view.w, view.h);
  c.beginPath();
  for (let x = Math.floor(x0 / step) * step; x <= x1; x += step) { const [sx] = toScreen(x, 0); c.moveTo(sx, 0); c.lineTo(sx, view.h); }
  for (let y = Math.floor(y0 / step) * step; y <= y1; y += step) { const [, sy] = toScreen(0, y); c.moveTo(0, sy); c.lineTo(view.w, sy); }
  c.stroke();
}

function drawRoads(c) {
  const s = view.s;
  const [wx0, wy1] = toWorld(-40, -40), [wx1, wy0] = toWorld(view.w + 40, view.h + 40);
  const bx0 = Math.floor(wx0 / ROAD_BUCKET) - 1, bx1 = Math.floor(wx1 / ROAD_BUCKET) + 1;
  const by0 = Math.floor(wy0 / ROAD_BUCKET) - 1, by1 = Math.floor(wy1 / ROAD_BUCKET) + 1;
  const ox = view.w / 2 - view.cx * s, oy = view.h / 2 + view.cy * s;
  c.lineCap = "round";
  for (let cls = 0; cls < 4; cls++) {
    const [col, wpm, wmin, zmin] = ROAD_STYLE[cls];
    if (s < zmin) continue;
    c.strokeStyle = col;
    c.lineWidth = Math.max(wmin, Math.min(wpm * s * 0.6, wpm * 0.6));
    c.beginPath();
    for (let bx = bx0; bx <= bx1; bx++)
      for (let by = by0; by <= by1; by++) {
        const b = roadBuckets.get(bx + "," + by);
        if (!b) continue;
        const a = b[cls];
        for (let k = 0; k < a.length; k += 4) {
          c.moveTo(a[k] * s + ox, oy - a[k + 1] * s);
          c.lineTo(a[k + 2] * s + ox, oy - a[k + 3] * s);
        }
      }
    c.stroke();
  }
}

function drawZoneLabels(c) {
  const s = view.s;
  const minArea = s < 0.06 ? 4e6 : s < 0.12 ? 1.2e6 : s < 0.3 ? 2e5 : 0;
  c.font = `600 ${s < 0.12 ? 10 : 11}px system-ui, sans-serif`;
  c.textAlign = "center"; c.textBaseline = "middle";
  c.fillStyle = "rgba(190, 210, 230, .55)";
  for (const z of zoneLabels) {
    if (z.area < minArea) continue;
    const [x, y] = toScreen(z.x, z.y);
    if (x < -100 || y < -20 || x > view.w + 100 || y > view.h + 20) continue;
    c.fillText(z.name.toUpperCase(), x, y);
  }
}

// ---------------------------------------------------------------- players
const players = new Map();   // id -> {info, samples: [{t,x,y,h}], trail: [[x,y]]}
const COLORS = ["#39d0ff", "#ff6b9a", "#ffd23f", "#7cfc8a", "#c792ff", "#ff9f43", "#4dd8c0", "#f2f2f2"];
const colorOf = (id) => COLORS[(id - 1) % COLORS.length];

function upsert(p, now) {
  let e = players.get(p.id);
  if (!e) players.set(p.id, e = { info: p, samples: [], trail: [] });
  e.info = p;
  if (!p.playing) { e.samples = []; e.trail = []; return; }
  const last = e.samples[e.samples.length - 1];
  // after a pause (nothing changed, so nothing was sent) anchor the old spot just before now, or the marker would glide
  if (last && now - last.t > 300) e.samples.push({ ...last, t: now - 100 });
  e.samples.push({ t: now, x: p.x, y: p.y, h: p.h });
  if (e.samples.length > 30) e.samples.splice(0, e.samples.length - 30);
  const tl = e.trail[e.trail.length - 1];
  if (!tl || Math.hypot(tl[0] - p.x, tl[1] - p.y) > 4) { e.trail.push([p.x, p.y, now]); if (e.trail.length > 400) e.trail.shift(); }
}

function sampleAt(e, t) {
  const S = e.samples;
  if (!S.length) return null;
  if (t <= S[0].t) return S[0];
  for (let i = S.length - 1; i > 0; i--) {
    const a = S[i - 1], b = S[i];
    if (t >= a.t) {
      if (t >= b.t) return b;
      const f = (t - a.t) / (b.t - a.t);
      let dh = ((b.h - a.h + 540) % 360) - 180;
      return { x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f, h: (a.h + dh * f + 360) % 360 };
    }
  }
  return S[S.length - 1];
}

function drawPlayers(now) {
  const c = ctx, t = now - INTERP_DELAY;
  for (const [id, e] of players) {
    if (!e.info.playing) continue;
    const p = sampleAt(e, t);
    if (!p) continue;
    e.drawn = p;
    const col = colorOf(id);
    // trail of the last minute
    c.lineWidth = 2; c.strokeStyle = col; c.lineCap = "round";
    for (let i = 1; i < e.trail.length; i++) {
      const a = e.trail[i - 1], b = e.trail[i];
      const age = (now - b[2]) / 60000;
      if (age > 1) continue;
      c.globalAlpha = 0.45 * (1 - age);
      const [ax, ay] = toScreen(a[0], a[1]), [bx, by] = toScreen(b[0], b[1]);
      c.beginPath(); c.moveTo(ax, ay); c.lineTo(bx, by); c.stroke();
    }
    c.globalAlpha = 1;
    const [x, y] = toScreen(p.x, p.y);
    const sel = selected === id;
    if (sel) { c.beginPath(); c.arc(x, y, 17, 0, 7); c.strokeStyle = col; c.globalAlpha = 0.5 + 0.3 * Math.sin(now / 200); c.lineWidth = 2; c.stroke(); c.globalAlpha = 1; }
    c.save();
    c.translate(x, y);
    c.rotate(-p.h * Math.PI / 180);   // GTA heading: degrees counter-clockwise from north
    c.beginPath();
    if (e.info.veh) { c.roundRect(-6, -9, 12, 18, 3); }
    else { c.moveTo(0, -11); c.lineTo(8, 8); c.lineTo(0, 4); c.lineTo(-8, 8); c.closePath(); }
    c.fillStyle = e.info.dead ? "#55606b" : col;
    c.strokeStyle = "#071018"; c.lineWidth = 2;
    c.fill(); c.stroke();
    if (e.info.veh) { c.beginPath(); c.moveTo(0, -13); c.lineTo(4, -8); c.lineTo(-4, -8); c.closePath(); c.fillStyle = col; c.fill(); }
    c.restore();
    // name and stars
    c.font = "600 12px system-ui, sans-serif"; c.textAlign = "center"; c.textBaseline = "bottom";
    const label = e.info.name + (e.info.dead ? " ✕" : "");
    c.lineWidth = 3; c.strokeStyle = "rgba(4, 8, 12, .85)"; c.strokeText(label, x, y - 15);
    c.fillStyle = "#fff"; c.fillText(label, x, y - 15);
    if (e.info.wanted > 0) {
      c.font = "11px system-ui, sans-serif"; c.fillStyle = "#ffd23f";
      c.strokeText("★".repeat(e.info.wanted), x, y - 28); c.fillText("★".repeat(e.info.wanted), x, y - 28);
    }
  }
}

// ---------------------------------------------------------------- side panel
const COMPASS = ["N", "NW", "W", "SW", "S", "SE", "E", "NE"];
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (ch) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[ch]));
const stars = (n) => `<span class="stars" title="Wanted level ${n}">${"★".repeat(n)}<span class="off">${"★".repeat(5 - n)}</span></span>`;
const SEATS = (s) => s === -1 ? "driver" : `passenger ${s + 1}`;

function renderList() {
  const list = $("list");
  const arr = [...players.values()].sort((a, b) => a.info.id - b.info.id);
  $("count").textContent = arr.length;
  if (!arr.length) { list.innerHTML = `<div class="empty">${online ? "Nobody is connected." : "Waiting for the game server…"}</div>`; return; }
  list.innerHTML = arr.map(({ info: p }) => {
    const col = colorOf(p.id);
    let state;
    if (!p.playing) state = "Online · in story mode";
    else if (p.dead) state = `<span class="dead">Dead</span>`;
    else state = `<span class="live">Online</span>${p.activity ? " · in an activity" : ""}${p.passive ? " · passive" : ""}${p.session ? ` · session ${p.session >= 0x80000000 ? "private" : "#" + p.session}` : ""}`;
    let rows = "";
    if (p.playing) {
      const kmh = Math.round(p.spd * 3.6);
      rows = `<dl class="rows">
        <dt>Zone</dt><dd title="${esc(p.zone)}">${esc(p.zone) || "—"}</dd>
        <dt>Position</dt><dd>${p.x.toFixed(0)}, ${p.y.toFixed(0)}, ${p.z.toFixed(0)}</dd>
        <dt>Heading</dt><dd>${p.h}° ${COMPASS[Math.round(p.h / 45) % 8]}</dd>
        <dt>Speed</dt><dd>${kmh} km/h</dd>
        <dt>Vehicle</dt><dd>${p.veh ? `${esc(p.veh.name)}${p.veh.cls ? ` (${esc(p.veh.cls)})` : ""}, ${SEATS(p.veh.seat)}` : "on foot"}</dd>
        <dt>Weapon</dt><dd>${esc(p.weaponName) || "—"}</dd>
        <dt>Wanted</dt><dd>${stars(p.wanted)}</dd>
        <dt>K / D</dt><dd>${p.kills} / ${p.deaths}</dd>
      </dl>
      <div class="bars">
        <div class="barrow"><span>Health</span><div class="meter"><i class="hp" data-w="${p.hp}"></i></div><span>${p.hp}</span></div>
        <div class="barrow"><span>Armour</span><div class="meter"><i class="ar" data-w="${p.ar}"></i></div><span>${p.ar}</span></div>
      </div>`;
    }
    return `<div class="card${selected === p.id ? " sel" : ""}" data-id="${p.id}">
      <div class="top"><span class="swatch" data-col="${col}"></span><span class="name">${esc(p.name)}</span><span class="rank">RANK ${p.rank}</span></div>
      <div class="state">${state}</div>${rows}</div>`;
  }).join("");
  // CSP forbids inline style attributes: widths and colours are set through the DOM
  for (const el of list.querySelectorAll("[data-w]")) el.style.width = Math.max(0, Math.min(100, +el.dataset.w)) + "%";
  for (const el of list.querySelectorAll("[data-col]")) el.style.background = el.dataset.col;
}
let listDirty = true;
// the panel on a timer, not in the frame loop: frames stop in a background tab, timers only slow down
setInterval(() => { if (listDirty) { listDirty = false; renderList(); } }, 250);

// pointerdown, not click: the cards are rebuilt 4 times a second and a click needs the same element under down and up
$("list").addEventListener("pointerdown", (ev) => {
  const card = ev.target.closest(".card");
  if (!card) return;
  const id = +card.dataset.id;
  if (selected === id) { selected = follow = null; }
  else {
    selected = follow = id;
    const e = players.get(id);
    if (e?.info.playing && view.s < 0.35) { view.s = 0.6; baseDirty = true; }
    if (e?.info.playing && is3d && view3d) view3d.focus(e.info.x, e.info.y, e.info.z, 350);
  }
  listDirty = true;
});

let worldNow = null;   // the game's clock, weather and blackout, for the 3D view's sky (map3d.js)
function renderWorld(w) {
  worldNow = w || worldNow;
  if (!w) { $("world").innerHTML = ""; return; }
  const hh = String(w.hour).padStart(2, "0"), mm = String(w.minute).padStart(2, "0");
  const weather = w.weather === w.next ? w.weather : `${w.weather} → ${w.next}`;
  $("world").innerHTML = `<span>Time <b>${hh}:${mm}</b>${w.frozen ? " (frozen)" : ""}</span><span>Weather <b>${esc(weather)}</b>${w.auto ? "" : " (fixed)"}</span>${w.blackout ? `<span class="badge alert">BLACKOUT</span>` : ""}`;
}

// ---------------------------------------------------------------- connection
let online = false, ws = null, retry = 500;
function setStatus(kind, text) { $("dot").className = "dot" + (kind ? " " + kind : ""); $("server").textContent = text; }

function connect() {
  ws = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws");
  ws.onopen = () => { retry = 500; };
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data), now = performance.now();
    if (m.t === "full") {
      online = !!m.online;
      setStatus(online ? "on" : "wait", online ? (m.server?.name ?? "game server") : "game server offline");
      players.clear();
      for (const p of m.players) upsert(p, now);
      renderWorld(m.world);
      listDirty = false;
      renderList();
      if (typeof onCameras === "function") onCameras(m.cameras || [], m.cctv || []);
      if (typeof onGameEvent === "function") onGameEvent(m.gameEvent || null);
    } else if (m.t === "tel") {
      need2d = true;
      for (const p of m.players) upsert(p, now);
      for (const id of m.gone) { players.delete(id); if (selected === id) selected = follow = null; }
      if (m.world) renderWorld(m.world);
      if (m.cameras && typeof onCameras === "function") onCameras(m.cameras, null);
      if (m.players.length || m.gone.length) listDirty = true;
    } else if (m.t === "event" && typeof onGodEyeEvent === "function") onGodEyeEvent(m.entry);
    else if (m.t === "game" && typeof onGameEvent === "function") onGameEvent(m.event);
  };
  ws.onclose = () => {
    online = false; setStatus("", "GodEye offline, reconnecting…");
    setTimeout(connect, retry); retry = Math.min(retry * 2, 8000);
  };
}

// ---------------------------------------------------------------- input: drag, wheel, pinch, buttons
const pointers = new Map();
let pinch0 = null;
canvas.addEventListener("pointerdown", (e) => { canvas.setPointerCapture(e.pointerId); pointers.set(e.pointerId, [e.offsetX, e.offsetY]); canvas.classList.add("drag"); });
canvas.addEventListener("pointerup", (e) => { pointers.delete(e.pointerId); pinch0 = null; if (!pointers.size) canvas.classList.remove("drag"); });
canvas.addEventListener("pointercancel", (e) => { pointers.delete(e.pointerId); pinch0 = null; });
canvas.addEventListener("pointermove", (e) => {
  const [wx, wy] = toWorld(e.offsetX, e.offsetY);
  $("coords").textContent = `${wx.toFixed(0)}, ${wy.toFixed(0)}`;
  const prev = pointers.get(e.pointerId);
  if (!prev) return;
  pointers.set(e.pointerId, [e.offsetX, e.offsetY]);
  if (pointers.size === 2) {
    const [a, b] = [...pointers.values()];
    const d = Math.hypot(a[0] - b[0], a[1] - b[1]);
    if (pinch0) zoomAt((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, d / pinch0);
    pinch0 = d;
    return;
  }
  view.cx -= (e.offsetX - prev[0]) / view.s;
  view.cy += (e.offsetY - prev[1]) / view.s;
  follow = null;
  baseDirty = true;
});
canvas.addEventListener("wheel", (e) => { e.preventDefault(); zoomAt(e.offsetX, e.offsetY, Math.exp(-e.deltaY * 0.0015)); }, { passive: false });

function zoomAt(sx, sy, f) {
  const [wx, wy] = toWorld(sx, sy);
  view.s = Math.max(0.03, Math.min(6, view.s * f));
  const [nx, ny] = toWorld(sx, sy);
  view.cx += wx - nx; view.cy += wy - ny;
  baseDirty = true;
}
$("zin").onclick = () => zoomAt(view.w / 2, view.h / 2, 1.6);
$("zout").onclick = () => zoomAt(view.w / 2, view.h / 2, 1 / 1.6);
$("fit").onclick = fitAll;
for (const id of ["lySat", "lyTerrain", "lyRoads", "lyZones", "lyLights"]) $(id).onchange = () => { baseDirty = true; };

function fitAll() {
  follow = null;
  const pts = [...players.values()].filter((e) => e.info.playing).map((e) => [e.info.x, e.info.y]);
  if (!pts.length) { view.cx = 200; view.cy = 2000; view.s = Math.max(0.03, Math.min(view.w / 9000, view.h / 13000)); baseDirty = true; return; }
  const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]);
  const x0 = Math.min(...xs) - 300, x1 = Math.max(...xs) + 300, y0 = Math.min(...ys) - 300, y1 = Math.max(...ys) + 300;
  view.cx = (x0 + x1) / 2; view.cy = (y0 + y1) / 2;
  view.s = Math.max(0.03, Math.min(0.5, Math.min(view.w / (x1 - x0), view.h / (y1 - y0))));
  baseDirty = true;
}

// ---------------------------------------------------------------- frame loop
// the 2D map draws only when it changed: a new base (pan, zoom, layers), players to animate, or a telemetry update
let need2d = true;
function frame(now) {
  requestAnimationFrame(frame);
  if (is3d) return;   // map3d.js draws while the 3D view is on
  const moving = [...players.values()].some((e) => e.info.playing);
  if (!baseDirty && !need2d && !moving && follow == null) return;
  need2d = false;
  if (follow != null) {
    const e = players.get(follow);
    if (e?.drawn) {
      const dx = e.drawn.x - view.cx, dy = e.drawn.y - view.cy;
      if (Math.abs(dx) * view.s > 0.5 || Math.abs(dy) * view.s > 0.5) { view.cx += dx * 0.2; view.cy += dy * 0.2; baseDirty = true; }
    }
  }
  if (baseDirty) { baseDirty = false; drawBase(); }
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.drawImage(baseCanvas, 0, 0);
  ctx.setTransform(view.dpr, 0, 0, view.dpr, 0, 0);
  drawPlayers(now);
}

// ---------------------------------------------------------------- 2D / 3D (Phase 11: the real map in 3D, map3d.js)
let view3d = null, is3d = false;
async function setView(three) {
  is3d = three;
  $("v2d").classList.toggle("on", !three);
  $("v3d").classList.toggle("on", three);
  canvas.hidden = three;
  $("map3d").hidden = !three;
  $("hint3d").hidden = !three;
  for (const sel of [".map-tools", ".layers", ".coords"]) document.querySelector(sel).hidden = three;
  if (three) {
    view3d = view3d || await import("./map3d.js");
    await view3d.start();
    const e = follow != null ? players.get(follow) : null;
    if (e?.info.playing) view3d.focus(e.info.x, e.info.y, e.info.z, 350);
  } else {
    view3d?.stop();
    baseDirty = true;
  }
}
$("v2d").onclick = () => { setView(false); try { localStorage.setItem("godeye.view", "2d"); } catch { /* private mode */ } };
$("v3d").onclick = () => {
  // on the public address the 3D map (the game's own models) needs a GodEye login
  if (typeof auth !== "undefined" && auth.map3d === false) { toast("Log in to see the 3D map.", true); document.getElementById("loginBtn")?.click(); return; }
  setView(true); try { localStorage.setItem("godeye.view", "3d"); } catch { /* private mode */ } };
// the last view comes back (or ?view=3d / ?view=2d)
{
  let v = new URLSearchParams(location.search).get("view");
  try { v = v || localStorage.getItem("godeye.view"); } catch { /* private mode */ }
  if (v === "3d") setView(true);
}

// the map's box can start at 0x0 (a hidden tab or pane): fit the view the first time it gets a size
let fitted = false;
new ResizeObserver(() => { resize(); if (!fitted && view.w > 0 && view.h > 0) { fitted = true; fitAll(); } }).observe(canvas);
loadMap();
connect();
renderList();
requestAnimationFrame(frame);
