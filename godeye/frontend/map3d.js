// GodEye 3D (Phase 11): the real map, streamed in tiles made from the player's own game files by tools/godeye-map
// (map3d). Far tiles hold the game's SLOD1 models; near the camera a tile swaps them for its LOD models. Players are
// drawn from the same telemetry as the 2D map (app.js globals: players, selected, colorOf, sampleAt).
import * as THREE from "./vendor/three/three.module.min.js";

const BASE = "data/map3d/";
let renderer, scene, camera, root, waterGroup, playerGroup, canvas, index = null, running = false;
let hemi, sun, sky, seaMat, waterMat, maxAniso = 4;   // the light and sky follow the game's clock and weather
const emitMats = new Set();         // window lights: brighter at night, off in a blackout
const tiles = new Map();            // "x,y" -> {info, far: {group, layer1: []} | null, near: group | null, loading}
const materials = new Map();        // tex key + kind -> material
const texCache = new Map();         // tex key -> THREE.DataTexture (loading or loaded)
const markers = new Map();          // player id -> {group, label}
const ver = () => (index?.built ? "?v=" + index.built : "");   // a new export never mixes with cached old tiles
const orbit = { tx: 0, ty: -900, tz: 30, dist: 5200, yaw: -0.35, pitch: 0.95 };
let lastStream = 0, inflight = 0;

// ---------------------------------------------------------------- data
const HALF = (() => {   // half float -> float lookup for the UVs
  const t = new Float32Array(65536);
  for (let h = 0; h < 65536; h++) {
    const s = h & 0x8000 ? -1 : 1, e = (h >> 10) & 31, f = h & 1023;
    t[h] = e === 0 ? s * f * 2 ** -24 : e === 31 ? (f ? NaN : s * Infinity) : s * (1 + f / 1024) * 2 ** (e - 15);
  }
  return t;
})();

function decodeBC1(buf) {
  const dv = new DataView(buf);
  const w = dv.getUint16(0, true), h = dv.getUint16(2, true);
  const out = new Uint8Array(w * h * 4);
  const bw = (w + 3) >> 2, bh = (h + 3) >> 2;
  const pal = new Uint8Array(16);
  for (let by = 0; by < bh; by++)
    for (let bx = 0; bx < bw; bx++) {
      const o = 8 + (by * bw + bx) * 8;
      const c0 = dv.getUint16(o, true), c1 = dv.getUint16(o + 2, true), bits = dv.getUint32(o + 4, true);
      const r0 = ((c0 >> 11) & 31) * 255 / 31, g0 = ((c0 >> 5) & 63) * 255 / 63, b0 = (c0 & 31) * 255 / 31;
      const r1 = ((c1 >> 11) & 31) * 255 / 31, g1 = ((c1 >> 5) & 63) * 255 / 63, b1 = (c1 & 31) * 255 / 31;
      pal.set([r0, g0, b0, 255, r1, g1, b1, 255]);
      if (c0 > c1) pal.set([(2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, 255, (r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, 255], 8);
      else pal.set([(r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255, 0, 0, 0, 0], 8);
      for (let k = 0; k < 16; k++) {
        const x = bx * 4 + (k & 3), y = by * 4 + (k >> 2);
        if (x >= w || y >= h) continue;
        const p = ((bits >>> (k * 2)) & 3) * 4, q = (y * w + x) * 4;
        out[q] = pal[p]; out[q + 1] = pal[p + 1]; out[q + 2] = pal[p + 2]; out[q + 3] = pal[p + 3];
      }
    }
  return { w, h, data: out };
}

function texture(key) {
  let t = texCache.get(key);
  if (t) return t;
  t = new THREE.DataTexture(new Uint8Array([150, 150, 150, 255]), 1, 1);
  t.needsUpdate = true;
  texCache.set(key, t);
  fetch(BASE + "tex/" + key + ".bc1" + ver()).then((r) => (r.ok ? r.arrayBuffer() : null)).then((buf) => {
    if (!buf) return;
    const { w, h, data } = decodeBC1(buf);
    t.image = { data, width: w, height: h };
    t.wrapS = t.wrapT = THREE.RepeatWrapping;
    t.magFilter = THREE.LinearFilter;
    t.minFilter = THREE.LinearMipmapLinearFilter;
    t.generateMipmaps = true;
    t.colorSpace = THREE.SRGBColorSpace;
    t.anisotropy = maxAniso;
    t.needsUpdate = true;
  }).catch(() => {});
  return t;
}

// how a batch is drawn comes from its shader family (tools/godeye-map): solid, cut-out, decal, tree, glass, light
// panel or water. Older exports only say alpha or not.
function material(key, kind) {
  const k = key + "|" + kind;
  let m = materials.get(k);
  if (m) return m;
  if (key === "@water" || kind === "water") m = waterMat;   // the game's own water surfaces (rivers, ponds, pools)
  else if (kind === "emit") {   // light panels laid over the walls: their black adds nothing, their windows glow
    m = new THREE.MeshBasicMaterial({ color: 0xffe2b0, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, opacity: 0.1, side: THREE.DoubleSide });
    emitMats.add(m);
  } else if (kind === "glass") {
    m = new THREE.MeshPhongMaterial({ color: 0x9fbccc, specular: 0x8899aa, shininess: 80, transparent: true, opacity: 0.45, depthWrite: false, side: THREE.DoubleSide });
  } else {
    m = new THREE.MeshLambertMaterial({ color: key ? 0xffffff : 0x9a9a92, side: THREE.DoubleSide });
    if (kind === "cut" || kind === "tree" || kind === "decal") { m.alphaTest = 0.5; m.alphaToCoverage = true; }
    if (kind === "decal") { m.polygonOffset = true; m.polygonOffsetFactor = -1; m.polygonOffsetUnits = -4; }
  }
  if (key && key !== "@water" && kind !== "water") m.map = texture(key);
  materials.set(k, m);
  return m;
}

// one tile file -> a group of meshes (one per batch); `layer1` collects the meshes the near file replaces
function parseTile(buf) {
  const dv = new DataView(buf);
  if (dv.getUint32(0, true) !== 0x314d4547) throw new Error("not a GodEye tile");
  const hlen = dv.getUint32(4, true);
  const head = JSON.parse(new TextDecoder().decode(new Uint8Array(buf, 8, hlen)));
  const vOff = 8 + hlen;
  const nv = head.batches.reduce((a, b) => Math.max(a, b.v0 + b.vn), 0);
  const ni = head.batches.reduce((a, b) => Math.max(a, b.i0 + b.in), 0);
  const raw16 = new Uint16Array(buf, vOff, nv * 7);
  const raw8 = new Int8Array(buf, vOff, nv * 14);
  const pos = new Uint16Array(nv * 3), uv = new Float32Array(nv * 2), nrm = new Float32Array(nv * 3);
  // the tile is scaled unevenly (xy by the tile span, z by its height range); normals are stored in world space, so
  // they are pre-scaled here for the normal matrix (the inverse scale) to bring them back
  const kz = head.scale[2] / head.scale[0];
  for (let i = 0; i < nv; i++) {
    const o = i * 7;
    pos[i * 3] = raw16[o]; pos[i * 3 + 1] = raw16[o + 1]; pos[i * 3 + 2] = raw16[o + 2];
    uv[i * 2] = HALF[raw16[o + 3]]; uv[i * 2 + 1] = HALF[raw16[o + 4]];
    const nx = raw8[i * 14 + 10], ny = raw8[i * 14 + 11], nz = raw8[i * 14 + 12] * kz, l = Math.hypot(nx, ny, nz) || 1;
    nrm[i * 3] = nx / l; nrm[i * 3 + 1] = ny / l; nrm[i * 3 + 2] = nz / l;
  }
  let iOff = vOff + nv * 14;
  if (iOff % 4) iOff += 4 - (iOff % 4);
  const idx = new Uint16Array(buf, iOff, ni);
  const group = new THREE.Group();
  group.position.set(head.origin[0], head.origin[1], head.origin[2]);
  group.scale.set(head.scale[0], head.scale[1], head.scale[2]);
  group.matrixAutoUpdate = false;
  group.updateMatrix();
  const layer1 = [];
  for (const b of head.batches) {
    const g = new THREE.BufferGeometry();
    g.setAttribute("position", new THREE.BufferAttribute(pos.subarray(b.v0 * 3, (b.v0 + b.vn) * 3), 3, true));
    g.setAttribute("uv", new THREE.BufferAttribute(uv.subarray(b.v0 * 2, (b.v0 + b.vn) * 2), 2));
    g.setAttribute("normal", new THREE.BufferAttribute(nrm.subarray(b.v0 * 3, (b.v0 + b.vn) * 3), 3));
    g.setIndex(new THREE.BufferAttribute(idx.slice(b.i0, b.i0 + b.in), 1));
    g.computeBoundingSphere();
    const mesh = new THREE.Mesh(g, material(b.tex, b.kind ?? (b.alpha ? "cut" : "")));
    if (b.kind === "emit" || b.kind === "glass") mesh.renderOrder = 2;   // after the walls they lie on
    mesh.matrixAutoUpdate = false;
    group.add(mesh);
    if (b.layer === 1) layer1.push(mesh);
  }
  return { group, layer1 };
}

async function loadTile(t, kind) {
  t.loading = (t.loading || 0) + 1;
  inflight++;
  try {
    const r = await fetch(`${BASE}t_${t.info.x}_${t.info.y}.${kind}.bin${ver()}`);
    if (!r.ok) return;
    const parsed = parseTile(await r.arrayBuffer());
    if (kind === "far") { t.far = parsed; root.add(parsed.group); }
    else { t.near = parsed.group; root.add(parsed.group); }
    applyNear(t);
  } catch (e) { console.warn("tile", t.info.x, t.info.y, kind, e); }
  finally { t.loading--; inflight--; }
}

function applyNear(t) {
  const swap = !!(t.near && t.nearWanted);
  if (t.near) t.near.visible = swap;
  if (t.far) for (const m of t.far.layer1) m.visible = !swap;
}

function dispose(group) {
  if (!group) return;
  group.removeFromParent();
  group.traverse((o) => o.geometry?.dispose());
}

// which tiles are wanted: far ones round the point looked at, near ones when the camera is low and close
function stream() {
  if (!index) return;
  const cx = orbit.tx, cy = orbit.ty;
  const farR = Math.max(4000, orbit.dist * 2.2), nearR = orbit.dist < 2200 ? 1500 : 0;
  const order = [];
  for (const t of tiles.values()) {
    const mx = (t.info.x + 0.5) * index.tile, my = (t.info.y + 0.5) * index.tile;
    const d = Math.hypot(mx - cx, my - cy);
    t.nearWanted = d < nearR && t.info.near > 0;
    if (d < farR && !t.far && !t.loading) order.push([d, t, "far"]);
    if (t.nearWanted && !t.near && !t.loading) order.push([d * 0.5, t, "near"]);
    if (t.near && d > nearR + 1200) { dispose(t.near); t.near = null; }
    if (t.far && d > farR + 3000) { dispose(t.far.group); t.far = null; }
    applyNear(t);
  }
  order.sort((a, b) => a[0] - b[0]);
  for (const [, t, kind] of order) { if (inflight >= 4) break; loadTile(t, kind); }
}

// ---------------------------------------------------------------- scene
function build() {
  canvas = document.getElementById("map3d");
  renderer = new THREE.WebGLRenderer({ canvas, antialias: true, logarithmicDepthBuffer: true });
  renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.NeutralToneMapping;   // keeps the colours; ACES washed the map out
  renderer.toneMappingExposure = 1.05;
  maxAniso = Math.min(8, renderer.capabilities.getMaxAnisotropy());
  scene = new THREE.Scene();
  scene.background = new THREE.Color(0x9ec4e8);
  scene.fog = new THREE.Fog(0x9ec4e8, 6000, 16000);
  camera = new THREE.PerspectiveCamera(55, 1, 1, 42000);
  camera.up.set(0, 0, 1);
  hemi = new THREE.HemisphereLight(0xdfefff, 0x6b5a45, 1.6);
  scene.add(hemi);
  sun = new THREE.DirectionalLight(0xfff2dd, 1.8);
  sun.position.set(-3000, -2000, 5000);
  scene.add(sun);
  // the sky: a dome shaded from the horizon to the zenith, following the camera
  const dome = new THREE.SphereGeometry(20000, 32, 16);
  dome.rotateX(Math.PI / 2);   // poles on z
  dome.setAttribute("color", new THREE.BufferAttribute(new Float32Array(dome.attributes.position.count * 3), 3));
  sky = new THREE.Mesh(dome, new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.BackSide, fog: false, depthWrite: false }));
  sky.renderOrder = -1;
  scene.add(sky);
  seaMat = new THREE.MeshPhongMaterial({ color: 0x1f6d9c, specular: 0x6f8fa8, shininess: 60, transparent: true, opacity: 0.9 });
  waterMat = new THREE.MeshPhongMaterial({ color: 0x3a7da3, specular: 0x6f8fa8, shininess: 60, transparent: true, opacity: 0.85, side: THREE.DoubleSide });
  root = new THREE.Group();
  scene.add(root);
  waterGroup = new THREE.Group();
  scene.add(waterGroup);
  const sea = new THREE.Mesh(new THREE.PlaneGeometry(40000, 40000), seaMat);
  sea.position.set(0, 0, -0.3);
  waterGroup.add(sea);
  playerGroup = new THREE.Group();
  scene.add(playerGroup);
}

// a backdrop under the models: the lowest ground of each 25 m cell (map.json's zmin), 3 m lower still, so the few spots
// the game's far models leave open show earth instead of the sea below; it never rises above real ground
async function addBackdrop() {
  let g;
  try { g = (await (await fetch("data/map.json", { cache: "no-cache" })).json()).grid; } catch { return; }
  if (!g?.zmin) return;
  const W = g.w, H = g.h;
  const geo = new THREE.PlaneGeometry((W - 1) * g.cell, (H - 1) * g.cell, W - 1, H - 1);
  const p = geo.attributes.position;
  for (let j = 0; j < H; j++)
    for (let i = 0; i < W; i++) {
      const k = j * W + i;
      p.setZ((H - 1 - j) * W + i, g.z[k] < 0 ? -6 : g.zmin[k] - 3);   // PlaneGeometry rows run from +y to -y
    }
  geo.computeVertexNormals();
  const m = new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ color: 0x7a6e52 }));
  m.position.set(g.x0 + g.cell / 2 + ((W - 1) * g.cell) / 2, g.y0 + g.cell / 2 + ((H - 1) * g.cell) / 2, 0);
  scene.add(m);
}

function addWater() {
  const mat = waterMat;
  for (const [x0, y0, x1, y1, z] of index.water) {
    const m = new THREE.Mesh(new THREE.PlaneGeometry(x1 - x0, y1 - y0), mat);
    m.position.set((x0 + x1) / 2, (y0 + y1) / 2, z);
    waterGroup.add(m);
  }
}

// ---------------------------------------------------------------- the game's clock and weather
// keyframes over the day: hour, zenith, horizon, sun, ambient, night (window lights)
const DAY = [
  [0, 0x0b1430, 0x2a3866, 0.45, 0.95, 1], [5, 0x0b1430, 0x2a3866, 0.45, 0.95, 1],
  [6.2, 0x3b5d92, 0xf2a36e, 0.9, 0.95, 0.6], [7.6, 0x4f93dc, 0xbcd8f0, 2.0, 1.5, 0],
  [17.4, 0x4f93dc, 0xbcd8f0, 2.0, 1.5, 0], [19, 0x3d4f86, 0xf0905c, 0.9, 0.95, 0.5],
  [20.4, 0x0b1430, 0x2a3866, 0.45, 0.95, 1], [24, 0x0b1430, 0x2a3866, 0.45, 0.95, 1],   // a map stays readable at night
];
const GREY = { CLOUDS: 0.25, OVERCAST: 0.55, RAIN: 0.7, THUNDER: 0.8, CLEARING: 0.35, SMOG: 0.4, FOGGY: 0.6, SNOW: 0.5, SNOWLIGHT: 0.4, BLIZZARD: 0.85, XMAS: 0.45, HALLOWEEN: 0.5 };
const FOG = { FOGGY: [600, 3500], SMOG: [2500, 9000], RAIN: [3500, 11000], THUNDER: [3000, 10000], BLIZZARD: [800, 4000], SNOW: [3000, 10000], OVERCAST: [5000, 14000] };
const c1 = new THREE.Color(), c2 = new THREE.Color(), cg = new THREE.Color(0x9aa3ad), cTop = new THREE.Color(), cHor = new THREE.Color();
let skyKey = "";

function updateSky() {
  const w = typeof worldNow !== "undefined" ? worldNow : null;
  const h = w ? (w.hour + w.minute / 60) % 24 : 13;
  const weather = w?.weather ?? "CLEAR";
  const key = `${Math.round(h * 12)}|${weather}|${!!w?.blackout}`;   // five game minutes is fine enough
  if (key === skyKey) return;
  skyKey = key;
  let a = DAY[0], b = DAY[1];
  for (let i = 0; i < DAY.length - 1; i++) if (h >= DAY[i][0] && h <= DAY[i + 1][0]) { a = DAY[i]; b = DAY[i + 1]; break; }
  const f = b[0] > a[0] ? (h - a[0]) / (b[0] - a[0]) : 0;
  const mix = (x, y) => x + (y - x) * f;
  const grey = GREY[weather] ?? 0, night = mix(a[5], b[5]);
  cTop.copy(c1.setHex(a[1])).lerp(c2.setHex(b[1]), f).lerp(cg, grey * (1 - night) * 0.8);
  cHor.copy(c1.setHex(a[2])).lerp(c2.setHex(b[2]), f).lerp(cg, grey * (1 - night));
  if (night > 0.5) cHor.multiplyScalar(1 - grey * 0.4);   // a cloudy night has no glow on the horizon
  // the dome: horizon colour low down, zenith colour above
  const pos = sky.geometry.attributes.position, col = sky.geometry.attributes.color;
  for (let i = 0; i < pos.count; i++) {
    const t = Math.min(1, Math.max(0, pos.getZ(i) / 20000 * 2.2));
    c1.copy(cHor).lerp(cTop, t);
    col.setXYZ(i, c1.r, c1.g, c1.b);
  }
  col.needsUpdate = true;
  scene.background.copy(cHor);
  const [fn, ff] = FOG[weather] ?? [9000, 22000];
  scene.fog.color.copy(cHor);
  scene.fog.near = fn; scene.fog.far = ff;
  // the sun: rises in the east (+x) at six, sets in the west at six; a dim moon light at night
  const ang = ((h - 6) / 12) * Math.PI;
  const up = Math.sin(ang);
  if (up > 0) sun.position.set(Math.cos(ang) * 5000, -2500, Math.max(400, up * 6000));
  else sun.position.set(-2000, 1500, 5000);
  sun.intensity = mix(a[3], b[3]) * (1 - grey * 0.65);
  sun.color.set(up > 0 && up < 0.35 ? 0xffc890 : up > 0 ? 0xfff2dd : 0x9fb4ff);
  hemi.intensity = mix(a[4], b[4]) * (1 - grey * 0.25);
  hemi.color.copy(cTop).lerp(c1.setHex(0xffffff), 0.55);
  seaMat.color.setHex(0x1f6d9c).lerp(cHor, 0.15).multiplyScalar(1 - night * 0.6);
  waterMat.color.setHex(0x3a7da3).lerp(cHor, 0.2).multiplyScalar(1 - night * 0.6);
  for (const m of [seaMat, waterMat]) m.specular.setHex(0x6f8fa8).multiplyScalar(1 - night * 0.8);   // no sun glare at night
  // window lights: faint by day, full at night, gone in a blackout
  const glow = w?.blackout ? 0 : 0.12 + night * 0.88;
  for (const m of emitMats) { m.opacity = glow; m.visible = glow > 0; }
}

// ---------------------------------------------------------------- players
function label(text, color) {
  const c = document.createElement("canvas");
  c.width = 256; c.height = 64;
  const g = c.getContext("2d");
  g.font = "600 30px system-ui, sans-serif";
  g.textAlign = "center"; g.textBaseline = "middle";
  g.lineWidth = 6; g.strokeStyle = "rgba(4,8,12,.85)"; g.strokeText(text, 128, 32);
  g.fillStyle = color; g.fillText(text, 128, 32);
  const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(c), depthTest: false, transparent: true }));
  s.renderOrder = 10;
  return s;
}

function updatePlayers(now) {
  const t = now - 150;
  const seen = new Set();
  for (const [id, e] of players) {
    if (!e.info.playing) continue;
    const p = sampleAt(e, t);
    if (!p) continue;
    seen.add(id);
    let mk = markers.get(id);
    if (!mk) {
      const col = colorOf(id);
      const group = new THREE.Group();
      const arrow = new THREE.Mesh(new THREE.ConeGeometry(1, 3, 12), new THREE.MeshBasicMaterial({ color: col, depthTest: false }));
      arrow.renderOrder = 9;   // the cone points along +y (north, flat with z up); turned to the heading below
      const pin = new THREE.Mesh(new THREE.CylinderGeometry(0.15, 0.15, 1, 6), new THREE.MeshBasicMaterial({ color: col, depthTest: false }));
      pin.rotation.x = Math.PI / 2;
      pin.renderOrder = 9;
      group.add(arrow, pin);
      mk = { group, arrow, pin, name: e.info.name, label: label(e.info.name, col) };
      group.add(mk.label);
      playerGroup.add(group);
      markers.set(id, mk);
    }
    // markers keep a steady size on screen: scaled with the distance to the camera
    const z = e.info.z ?? 0;
    const dist = camera.position.distanceTo(new THREE.Vector3(p.x, p.y, z));
    const k = Math.max(0.6, dist / 130);
    mk.group.position.set(p.x, p.y, z);
    mk.arrow.position.set(0, 0, 4 * k);
    mk.arrow.scale.setScalar(k * 1.4);
    mk.arrow.rotation.set(0, 0, p.h * Math.PI / 180);   // GTA heading: counter-clockwise from north
    mk.pin.scale.set(k, 4 * k, k);
    mk.pin.position.set(0, 0, 2 * k);
    mk.label.position.set(0, 0, 9 * k);
    mk.label.scale.set(16 * k, 4 * k, 1);
    mk.group.visible = true;
  }
  for (const [id, mk] of markers) if (!seen.has(id)) { mk.group.removeFromParent(); markers.delete(id); }
}

// ---------------------------------------------------------------- camera
function placeCamera() {
  const cp = Math.cos(orbit.pitch), sp = Math.sin(orbit.pitch);
  camera.position.set(orbit.tx - Math.sin(orbit.yaw) * cp * orbit.dist, orbit.ty - Math.cos(orbit.yaw) * cp * orbit.dist, orbit.tz + sp * orbit.dist);
  camera.lookAt(orbit.tx, orbit.ty, orbit.tz);
}

function bindInput() {
  const pts = new Map();
  let pinch = null;
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());
  canvas.addEventListener("pointerdown", (e) => { canvas.setPointerCapture(e.pointerId); pts.set(e.pointerId, { x: e.clientX, y: e.clientY, b: e.button, shift: e.shiftKey }); });
  const up = (e) => { pts.delete(e.pointerId); pinch = null; };
  canvas.addEventListener("pointerup", up);
  canvas.addEventListener("pointercancel", up);
  canvas.addEventListener("pointermove", (e) => {
    const p = pts.get(e.pointerId);
    if (!p) return;
    const dx = e.clientX - p.x, dy = e.clientY - p.y;
    p.x = e.clientX; p.y = e.clientY;
    if (pts.size === 2) {
      const [a, b] = [...pts.values()];
      const d = Math.hypot(a.x - b.x, a.y - b.y);
      if (pinch) orbit.dist = Math.min(14000, Math.max(30, orbit.dist * pinch / d));
      pinch = d;
      return;
    }
    if (p.b === 2 || p.shift) {   // pan along the ground
      const k = orbit.dist / 900;
      const s = Math.sin(orbit.yaw), c = Math.cos(orbit.yaw);
      orbit.tx -= (dx * c - dy * s) * k;
      orbit.ty -= (-dx * s - dy * c) * k;
      follow = null;
    } else {
      orbit.yaw += dx * 0.005;
      orbit.pitch = Math.min(1.5, Math.max(0.08, orbit.pitch + dy * 0.004));
    }
  });
  canvas.addEventListener("wheel", (e) => { e.preventDefault(); orbit.dist = Math.min(14000, Math.max(30, orbit.dist * Math.exp(e.deltaY * 0.0012))); }, { passive: false });
}

function resize3d() {
  const r = canvas.getBoundingClientRect();
  if (r.width < 1 || r.height < 1) return;
  renderer.setSize(r.width, r.height, false);
  camera.aspect = r.width / r.height;
  camera.updateProjectionMatrix();
}

function frame(now) {
  if (!running) return;
  if (follow != null) {
    const e = players.get(follow);
    const p = e && sampleAt(e, now - 150);
    if (p) { orbit.tx += (p.x - orbit.tx) * 0.15; orbit.ty += (p.y - orbit.ty) * 0.15; orbit.tz += ((e.info.z ?? orbit.tz) - orbit.tz) * 0.15; }
  }
  placeCamera();
  sky.position.copy(camera.position);
  updateSky();
  updatePlayers(now);
  renderer.render(scene, camera);
  requestAnimationFrame(frame);
}

// ---------------------------------------------------------------- entry points used by app.js
export async function start() {
  if (!renderer) {
    build();
    bindInput();
    new ResizeObserver(resize3d).observe(canvas);
  }
  if (!index) {   // again after a refusal: the visitor may have logged in since
    document.getElementById("nomap3d").hidden = true;
    try {
      const r = await fetch(BASE + "index.json", { cache: "no-cache" });
      if (r.status === 401) { document.getElementById("nomap3d").textContent = "Log in to see the 3D map."; throw new Error("login"); }
      index = await r.json();
      for (const info of index.tiles) tiles.set(info.x + "," + info.y, { info, far: null, near: null, loading: 0, nearWanted: false });
      addWater();
      addBackdrop();
    } catch {
      document.getElementById("nomap3d").hidden = false;
    }
  }
  running = true;
  // tiles stream on a timer, not per frame: frames stop in a background tab, loading should not
  if (!lastStream) lastStream = setInterval(() => { if (running) { placeCamera(); stream(); } }, 300);
  resize3d();
  requestAnimationFrame(frame);
}

export function stop() { running = false; }

export function focus(x, y, z, dist = 400) { orbit.tx = x; orbit.ty = y; orbit.tz = z; orbit.dist = dist; }

export function stats() {
  let far = 0, near = 0, meshes = 0;
  for (const t of tiles.values()) { if (t.far) { far++; meshes += t.far.group.children.length; } if (t.near) { near++; meshes += t.near.children.length; } }
  return { far, near, meshes, textures: texCache.size, calls: renderer?.info.render.calls, tris: renderer?.info.render.triangles, orbit: { ...orbit } };
}

// for debugging from the console: the scene, the water and the tiles
export function debug() { return { THREE, scene, waterGroup, tiles, camera }; }
