// GodEye's public address (e.g. godeye.example.com). Every request goes through a private Cloudflare Tunnel to the GodEye
// backend on the server PC (VPC service binding GODEYE). Requests are marked public, so the backend applies its public
// rules: viewing is open, the 3D map and the controls need a GodEye login. While GodEye is closed the tunnel is down
// and visitors get a short "GodEye is off" page instead of an error.
const OFF_PAGE = `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>GodEye</title><style>
:root{--bg:#070b10;--text:#d6e2ee;--muted:#7d8fa3;--accent:#39d0ff}
@media (prefers-color-scheme: light){:root{--bg:#f2f5f8;--text:#14202b;--muted:#5b6b7b;--accent:#0a84b8}}
body{margin:0;min-height:100vh;display:grid;place-items:center;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,sans-serif;padding:16px;box-sizing:border-box}
main{text-align:center;max-width:420px}h1{font:700 18px ui-monospace,Consolas,monospace;letter-spacing:.3em;color:var(--accent);margin:0 0 12px}
p{color:var(--muted);margin:0}</style></head><body><main><h1>GOD EYE</h1>
<p>GodEye is closed right now. It is on while its owner has it open; try again later.</p></main></body></html>`;

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
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
      return new Response(OFF_PAGE, { status: 503, headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" } });
    }
  },
};
