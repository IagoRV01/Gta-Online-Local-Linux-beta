// GodEye's public address (e.g. godeye.example.com). Every request goes through a private Cloudflare Tunnel to the GodEye
// backend on the server PC (VPC service binding GODEYE). Requests are marked public, so the backend applies its public
// rules: viewing is open, the 3D map and the controls need a GodEye login. While GodEye is closed the tunnel is down
// and visitors get a short "GodEye is off" page instead of an error.
// An optional mascot on that page (wrangler.toml [vars]): OFF_SPRITE_URL, a strip of OFF_SPRITE_FRAMES square frames,
// shown four times larger, with OFF_SPRITE_CREDIT under it (its artists and license).
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
function offPage(env) {
  const n = Math.max(1, Math.min(16, parseInt(env.OFF_SPRITE_FRAMES ?? "1", 10) || 1));
  const sprite = env.OFF_SPRITE_URL && /^https:\/\//.test(env.OFF_SPRITE_URL)
    ? `<div class="s" role="img" aria-label="asleep"></div>` : "";
  const css = sprite ? `.s{width:96px;height:96px;margin:0 auto 16px;background:url("${esc(env.OFF_SPRITE_URL)}") 0 0/${n * 96}px 96px no-repeat;image-rendering:pixelated;animation:z ${(n * 0.55).toFixed(2)}s steps(${n}) infinite}@keyframes z{to{background-position:-${n * 96}px 0}}small{display:block;margin-top:18px;color:var(--muted);font-size:11px}` : "";
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>GodEye</title><style>
:root{--bg:#070b10;--text:#d6e2ee;--muted:#7d8fa3;--accent:#39d0ff}
@media (prefers-color-scheme: light){:root{--bg:#f2f5f8;--text:#14202b;--muted:#5b6b7b;--accent:#0a84b8}}
body{margin:0;min-height:100vh;display:grid;place-items:center;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,sans-serif;padding:16px;box-sizing:border-box}
main{text-align:center;max-width:420px}h1{font:700 18px ui-monospace,Consolas,monospace;letter-spacing:.3em;color:var(--accent);margin:0 0 12px}
p{color:var(--muted);margin:0}${css}</style></head><body><main>${sprite}<h1>GOD EYE</h1>
<p>GodEye is closed right now. It is on while its owner has it open; try again later.</p>${sprite && env.OFF_SPRITE_CREDIT ? `<small>${esc(env.OFF_SPRITE_CREDIT)}</small>` : ""}</main></body></html>`;
}

// /__status: whether GodEye is open, for a button on another site (e.g. a "GodEye" link with a live dot). Only the
// origins in STATUS_ORIGINS (wrangler.toml [vars], comma-separated) may read it from a page.
async function status(req, env) {
  const origin = req.headers.get("origin") ?? "";
  const allowed = String(env.STATUS_ORIGINS ?? "").split(",").map((s) => s.trim()).filter(Boolean);
  const headers = { "content-type": "application/json", "cache-control": "no-store" };
  if (allowed.includes(origin)) { headers["access-control-allow-origin"] = origin; headers["vary"] = "origin"; }
  let open = false, game = false;
  try {
    const r = await env.GODEYE.fetch(new Request("http://godeye/api/health", { headers: { "x-godeye-public": "1" } }));
    if (r.ok) { const h = await r.json(); open = !!h.ok; game = !!h.server; }
  } catch { /* closed */ }
  return new Response(JSON.stringify({ open, game }), { headers });
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    if (url.pathname === "/__status" && req.method === "GET") return status(req, env);
    const headers = new Headers(req.headers);
    for (const h of ["x-godeye-public", "x-forwarded-host", "x-forwarded-proto"]) headers.delete(h);   // never trust a client's
    headers.set("x-godeye-public", "1");
    headers.set("x-forwarded-host", url.host);
    headers.set("x-forwarded-proto", "https");
    const target = new URL(req.url);
    target.protocol = "http:";   // the backend listens on plain http behind the tunnel
    try {
      const res = await env.GODEYE.fetch(new Request(target, { method: req.method, headers, body: req.body, redirect: "manual" }));
      if (res.status === 502 || res.status === 503 || res.status === 530) throw new Error("origin down");
      return res;
    } catch {
      if (req.headers.get("upgrade") === "websocket" || url.pathname.startsWith("/api/") || url.pathname.startsWith("/data/"))
        return new Response(JSON.stringify({ error: "GodEye is closed" }), { status: 503, headers: { "content-type": "application/json" } });
      return new Response(offPage(env), { status: 503, headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" } });
    }
  },
};
