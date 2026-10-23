// GodEye video (Phase 13). The camera client's window (Phase 12) becomes live video in the browser:
//   GTA window --FFmpeg gfxcapture--> NVENC H.264 on the chosen GPU --RTSP--> mediamtx (127.0.0.1)
//   mediamtx --WHEP (WebRTC, ~0.2 s)--> browsers on the LAN      mediamtx --LL-HLS (~2-4 s)--> everyone else
// Browsers only ever talk to GodEye: this module relays WHEP signalling and HLS on GodEye's origin. The WebRTC media
// itself flows from mediamtx's UDP port to the browser.
import { spawn, execFileSync, type ChildProcess } from "node:child_process";
import { existsSync } from "node:fs";
import type { IncomingMessage, ServerResponse } from "node:http";

export interface VideoConfig {
  enabled: boolean;
  mediamtx: string;          // mediamtx.exe
  mediamtxConfig: string;    // godeye/video/mediamtx.yml
  ffmpeg: string;            // ffmpeg.exe (8.0+, for gfxcapture)
  gpu: string;               // "RTX 5060 Ti 8GB" | "uuid:GPU-..." | "auto": the card NVENC encodes on
  streams: { path: string; name: string; window: string }[];   // window: regex of the window title to capture
  fps: number;
  bitrate: string;
  whep: string;              // mediamtx's WebRTC HTTP address
  hls: string;               // mediamtx's HLS HTTP address
}

export const VIDEO_DEFAULTS: VideoConfig = {
  enabled: false,
  mediamtx: "",
  mediamtxConfig: "../video/mediamtx.yml",
  ffmpeg: "",
  gpu: "auto",
  streams: [{ path: "cam1", name: "Camera 1", window: "^Grand Theft Auto V$" }],
  fps: 30,
  bitrate: "4M",
  whep: "http://127.0.0.1:8889",
  hls: "http://127.0.0.1:8888",
};

// The NVIDIA card for NVENC, as a UUID for CUDA_VISIBLE_DEVICES: "RTX 5060 Ti 8GB" = words of the name + memory
export function pickGpuUuid(sel: string, log: (m: string) => void): string | null {
  if (!sel || sel === "auto") return null;
  if (sel.startsWith("uuid:")) return sel.slice(5);
  let rows: string[];
  try {
    rows = execFileSync("nvidia-smi", ["--query-gpu=index,name,memory.total,uuid", "--format=csv,noheader,nounits"], { encoding: "utf8" }).trim().split(/\r?\n/);
  } catch { log("video: nvidia-smi not found, NVENC on the default card"); return null; }
  const words = sel.toLowerCase().split(/\s+/).filter(Boolean);
  for (const row of rows) {
    const [, name, mem, uuid] = row.split(",").map((s) => s.trim());
    const gb = Math.round(Number(mem) / 1024);
    const ok = words.every((w) => /^\d+gb$/.test(w) ? parseInt(w) === gb : name.toLowerCase().includes(w));
    if (ok) return uuid;
  }
  log(`video: no NVIDIA card matches "${sel}" (${rows.join(" | ")}): video off`);
  return "";
}

export class Video {
  private cfg: VideoConfig;
  private log: (level: string, m: string) => void;
  private mediamtx: ChildProcess | null = null;
  private captures = new Map<string, { proc: ChildProcess | null; since: number; running: boolean }>();
  private stopping = false;
  private gpuUuid: string | null = null;

  constructor(cfg: VideoConfig, log: (level: string, m: string) => void) { this.cfg = cfg; this.log = log; }

  get streams(): { path: string; name: string; live: boolean }[] {
    return this.cfg.enabled ? this.cfg.streams.map((s) => ({ path: s.path, name: s.name, live: !!this.captures.get(s.path)?.running })) : [];
  }

  start(): void {
    if (!this.cfg.enabled) return;
    if (!existsSync(this.cfg.mediamtx) || !existsSync(this.cfg.ffmpeg)) { this.log("WARN", "video: mediamtx or ffmpeg not found: video off"); this.cfg.enabled = false; return; }
    this.gpuUuid = pickGpuUuid(this.cfg.gpu, (m) => this.log("WARN", m));
    if (this.gpuUuid === "") { this.cfg.enabled = false; return; }
    this.runMediamtx();
    for (const s of this.cfg.streams) this.runCapture(s);
    process.on("exit", () => this.stop());
  }

  stop(): void {
    this.stopping = true;
    for (const c of this.captures.values()) c.proc?.kill();
    this.mediamtx?.kill();
  }

  private runMediamtx(): void {
    const p = spawn(this.cfg.mediamtx, [this.cfg.mediamtxConfig], { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
    this.mediamtx = p;
    p.stdout?.on("data", (d: Buffer) => { for (const l of d.toString().split(/\r?\n/)) if (/ ERR | WAR /.test(l)) this.log("WARN", "mediamtx: " + l.slice(20)); });
    p.on("exit", (code) => {
      if (this.stopping) return;
      this.log("WARN", `video: mediamtx exited (${code}); restarting in 5 s`);
      setTimeout(() => this.runMediamtx(), 5000);
    });
    this.log("INFO", `video: mediamtx started (WebRTC ${this.cfg.whep}, HLS ${this.cfg.hls})`);
  }

  // FFmpeg captures the camera window (Windows Graphics Capture: works while the window is on another virtual desktop)
  // and encodes it with NVENC on the chosen card. The window may not exist yet (GTA loading): retried every 10 s.
  private runCapture(s: { path: string; name: string; window: string }): void {
    const st = this.captures.get(s.path) ?? { proc: null, since: 0, running: false };
    this.captures.set(s.path, st);
    const src = `gfxcapture=window_title='${s.window.replace(/'/g, "")}':capture_cursor=0:max_framerate=${this.cfg.fps}:width=1280:height=720:resize_mode=scale_aspect`;
    const args = ["-hide_banner", "-loglevel", "warning", "-f", "lavfi", "-i", src, "-vf", "hwdownload,format=bgra",
      "-c:v", "h264_nvenc", "-preset", "p4", "-tune", "ll", "-zerolatency", "1", "-bf", "0", "-g", String(this.cfg.fps),
      "-profile:v", "baseline", "-b:v", this.cfg.bitrate, "-maxrate", this.cfg.bitrate, "-bufsize", this.cfg.bitrate,
      "-f", "rtsp", "-rtsp_transport", "tcp", `rtsp://127.0.0.1:8554/${s.path}`];
    const env = { ...process.env };
    if (this.gpuUuid) env.CUDA_VISIBLE_DEVICES = this.gpuUuid;   // only this FFmpeg: NVENC sees just the chosen card
    args.splice(args.indexOf("-f", 4), 0, "-progress", "pipe:1", "-stats_period", "2");   // frame counts on stdout
    const p = spawn(this.cfg.ffmpeg, args, { stdio: ["ignore", "pipe", "pipe"], windowsHide: true, env });
    st.proc = p;
    st.since = Date.now();
    let err = "", frames = 0, lastFrameAt = Date.now();
    p.stderr?.on("data", (d: Buffer) => { err = (err + d.toString()).slice(-600); });
    p.stdout?.on("data", (d: Buffer) => {   // live once frames flow; a capture that stops delivering them is restarted
      const m = /frame=(\d+)\s*$/m.exec(d.toString());
      if (!m || Number(m[1]) === frames) return;
      frames = Number(m[1]);
      lastFrameAt = Date.now();
      if (!st.running && st.proc === p) { st.running = true; this.log("INFO", `video: ${s.name} is live (${s.path})`); }
    });
    const live = setInterval(() => {
      if (Date.now() - lastFrameAt > 12000) { this.log("WARN", `video: ${s.name} delivers no frames: restarting the capture`); p.kill(); }
    }, 4000);
    p.on("exit", (code) => {
      clearInterval(live);
      const wasLive = st.running;
      st.running = false;
      st.proc = null;
      if (this.stopping) return;
      if (wasLive) this.log("WARN", `video: ${s.name} stopped (${code}): ${err.trim().split(/\r?\n/).pop() ?? ""}`);
      setTimeout(() => this.runCapture(s), wasLive ? 2000 : 10000);   // no window yet: try again later
    });
  }

  // ---- the browser side, on GodEye's origin. WHEP: POST an SDP offer, get the answer; PATCH/DELETE the session.
  // HLS: the playlist, its parts and segments.
  handle(req: IncomingMessage, res: ServerResponse, path: string, headers: Record<string, string>): boolean {
    const m = /^\/video\/([a-z0-9]{1,16})\/(whep(?:\/[0-9a-f-]{8,64})?|hls\.min\.js|[A-Za-z0-9_.-]{1,80}\.(?:m3u8|mp4|m4s))$/.exec(path);
    if (!m || !this.cfg.enabled || !this.cfg.streams.some((s) => s.path === m[1])) return false;
    const isWhep = m[2].startsWith("whep");
    if (isWhep && !["POST", "PATCH", "DELETE"].includes(req.method ?? "")) { res.writeHead(405, headers); res.end(); return true; }
    if (!isWhep && req.method !== "GET") { res.writeHead(405, headers); res.end(); return true; }
    const query = (req.url ?? "").includes("?") ? (req.url ?? "").slice((req.url ?? "").indexOf("?")) : "";
    const target = (isWhep ? this.cfg.whep : this.cfg.hls) + `/${m[1]}/${m[2]}` + (isWhep ? "" : query);
    const chunks: Buffer[] = [];
    let size = 0;
    req.on("data", (c: Buffer) => { size += c.length; if (size <= 64 * 1024) chunks.push(c); });
    req.on("end", async () => {
      if (size > 64 * 1024) { res.writeHead(413, headers); res.end(); return; }
      try {
        const r = await fetch(target, {
          method: req.method,
          headers: { "content-type": String(req.headers["content-type"] ?? (isWhep ? "application/sdp" : "")) },
          body: req.method === "GET" || req.method === "DELETE" ? undefined : Buffer.concat(chunks),
        });
        const out: Record<string, string> = { ...headers, "Content-Type": r.headers.get("content-type") ?? "application/octet-stream" };
        const loc = r.headers.get("location");   // the WHEP session: back through us
        if (loc) out["Location"] = `/video/${m[1]}/whep/${loc.split("/").pop()}`;
        if (!isWhep) out["Cache-Control"] = path.endsWith(".m3u8") ? "no-cache" : "max-age=60";
        res.writeHead(r.status, out);
        res.end(Buffer.from(await r.arrayBuffer()));
      } catch {
        res.writeHead(503, { ...headers, "Content-Type": "text/plain" });
        res.end("video server offline\n");
      }
    });
    return true;
  }
}
