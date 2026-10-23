// GodEye's satellite map, step 1: render the 3D map straight down, 1 m a pixel, one 1024 m block per PNG.
//   node tools/godeye-sat/render.mjs [godeye/data/map3d] [godeye/data/sat/blocks] [only bx,by ...]
// Blocks already rendered are skipped, and the browser is restarted every 40 blocks (textures pile up in GPU memory),
// so it can simply be run again until it says "done".
// It serves sat.html, three.js and the map3d files on a local port, and drives a headless Edge (or Chrome) over the
// DevTools protocol. Step 2 is tiles.py, which cuts the blocks into the 2D map's zoom levels.
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, "../..");
const MAP3D = resolve(process.argv[2] ?? join(ROOT, "godeye/data/map3d"));
const OUT = resolve(process.argv[3] ?? join(ROOT, "godeye/data/sat/blocks"));
const ONLY = process.argv.slice(4).map((s) => s.split(",").map(Number));
const BROWSERS = ["C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe", "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe", "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe"];
const browser = BROWSERS.find(existsSync);
if (!browser) { console.error("no Edge or Chrome found"); process.exit(1); }
mkdirSync(OUT, { recursive: true });

// ---- the files the page needs, and nothing else
const server = createServer((req, res) => {
  const path = decodeURIComponent(new URL(req.url, "http://x").pathname);
  let file = null, type = "application/octet-stream";
  if (path === "/") { file = join(HERE, "sat.html"); type = "text/html; charset=utf-8"; }
  else if (path === "/three.module.min.js") { file = join(ROOT, "godeye/frontend/vendor/three/three.module.min.js"); type = "text/javascript"; }
  else if (/^\/map3d\/[A-Za-z0-9_.-]+$/.test(path)) { file = join(MAP3D, path.slice(7)); if (path.endsWith(".json")) type = "application/json"; }
  if (!file || !existsSync(file) || !statSync(file).isFile()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { "Content-Type": type });
  res.end(readFileSync(file));
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const port = server.address().port;

// ---- the headless browser, over the DevTools protocol
const profile = mkdtempSync(join(tmpdir(), "godeye-sat-"));
const proc = spawn(browser, ["--headless=new", "--remote-debugging-port=9334", `--user-data-dir=${profile}`, "--window-size=1100,1100",
  "--force-device-scale-factor=1", "--use-angle=d3d11", "--ignore-gpu-blocklist", `http://127.0.0.1:${port}/`], { stdio: "ignore" });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let target;
for (let i = 0; i < 80 && !target; i++) { await sleep(250); try { target = (await (await fetch("http://127.0.0.1:9334/json")).json()).find((t) => t.type === "page"); } catch {} }
if (!target) { console.error("the browser did not start"); proc.kill(); process.exit(1); }
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener("open", r));
let id = 0;
const pending = new Map();
ws.addEventListener("message", (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } });
const cdp = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
async function run(expr) {
  const r = await cdp("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true });
  if (r.result?.exceptionDetails) throw new Error(r.result.exceptionDetails.exception?.description ?? "page error");
  return r.result?.result?.value;
}
for (let i = 0; i < 80; i++) { if (await run("!!window.satReady").catch(() => false)) break; await sleep(250); }
console.log("map", JSON.stringify(await run("sat.init()")));

// ---- every block with a tile, and the blocks around those (coasts and big models spill over)
let blocks = ONLY;
if (!blocks.length) {
  const set = new Set();
  for (const [x, y] of await run("sat.cells()")) for (let dx = -1; dx <= 1; dx++) for (let dy = -1; dy <= 1; dy++) set.add(`${x + dx},${y + dy}`);
  blocks = [...set].map((s) => s.split(",").map(Number));
}
blocks.sort((a, b) => a[1] - b[1] || a[0] - b[0]);   // row by row: neighbouring tiles stay loaded
const todo = blocks.filter(([bx, by]) => ONLY.length || !existsSync(join(OUT, `b_${bx}_${by}.png`)));
console.log(`${blocks.length} blocks, ${todo.length} to render`);
const t0 = Date.now();
let n = 0;
for (const [bx, by] of todo.slice(0, 40)) {
  let url;
  try { url = await run(`sat.render(${bx}, ${by})`); }
  catch (e) { console.error(`block ${bx},${by}: ${String(e.message).slice(0, 300)}`); proc.kill(); process.exit(2); }
  writeFileSync(join(OUT, `b_${bx}_${by}.png`), Buffer.from(url.slice(url.indexOf(",") + 1), "base64"));
  n++;
  if (n % 10 === 0) console.log(`${n}/${todo.length} blocks, ${Math.round((Date.now() - t0) / 1000)} s`);
}
console.log(`rendered ${n} blocks in ${Math.round((Date.now() - t0) / 1000)} s into ${OUT}${todo.length <= 40 ? "; done" : "; run again for the rest"}`);
ws.close(); proc.kill(); server.close();
process.exit(0);
