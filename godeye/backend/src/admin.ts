// The game server's admin API (127.0.0.1 only): newline-delimited JSON over TCP, authenticated with the shared secret
// that LocalOnlineServer.exe keeps in its admin_secret_file. Reconnects on its own: GodEye can start before the server.
import { EventEmitter } from "node:events";
import { readFileSync } from "node:fs";
import { Socket, createConnection } from "node:net";

export interface AdminOptions { host: string; port: number; secretFile: string; retryMs?: number }

export class AdminLink extends EventEmitter {
  private sock: Socket | null = null;
  private buf = "";
  private timer: NodeJS.Timeout | null = null;
  private nextId = 1;
  private pending = new Map<number, (ok: boolean) => void>();
  connected = false;
  private opts: AdminOptions;

  constructor(opts: AdminOptions) { super(); this.opts = opts; }

  start(): void { this.connect(); }

  private secret(): string {
    try { return readFileSync(this.opts.secretFile, "utf8").split(/\r?\n/)[0].trim(); } catch { return ""; }
  }

  private connect(): void {
    this.timer = null;
    const secret = this.secret();
    if (!secret) { this.emit("log", `no admin secret in ${this.opts.secretFile} yet (the server creates it on its first start)`); this.retry(); return; }
    const s = createConnection({ host: this.opts.host, port: this.opts.port });
    this.sock = s;
    s.setEncoding("utf8");
    s.setNoDelay(true);
    s.on("connect", () => { s.write(JSON.stringify({ t: "auth", secret }) + "\n"); });
    s.on("data", (d: string) => {
      this.buf += d;
      if (this.buf.length > 4 << 20) { s.destroy(new Error("line too long")); return; }
      let nl: number;
      while ((nl = this.buf.indexOf("\n")) >= 0) {
        const line = this.buf.slice(0, nl);
        this.buf = this.buf.slice(nl + 1);
        if (!line) continue;
        let msg: any;
        try { msg = JSON.parse(line); } catch { this.emit("log", "bad line from the server"); continue; }
        if (msg.t === "hello") { this.connected = true; this.emit("up", msg); }
        else if (msg.t === "cmdr") { this.pending.get(msg.id)?.(!!msg.ok); this.pending.delete(msg.id); }
        else this.emit("msg", msg);
      }
    });
    const down = () => {
      if (this.sock !== s) return;
      this.sock = null; this.buf = "";
      for (const cb of this.pending.values()) cb(false);
      this.pending.clear();
      if (this.connected) { this.connected = false; this.emit("down"); }
      this.retry();
    };
    s.on("error", () => {});
    s.on("close", down);
  }

  private retry(): void {
    if (!this.timer) this.timer = setTimeout(() => this.connect(), this.opts.retryMs ?? 2000);
  }

  requestFull(): void { this.sock?.write(JSON.stringify({ t: "full" }) + "\n"); }

  // one of the server.cmd admin commands; resolves to the server's verdict
  command(line: string): Promise<boolean> {
    if (!this.sock || !this.connected) return Promise.resolve(false);
    const id = this.nextId++;
    return new Promise((resolve) => {
      this.pending.set(id, resolve);
      this.sock!.write(JSON.stringify({ t: "cmd", id, line }) + "\n");
      setTimeout(() => { if (this.pending.delete(id)) resolve(false); }, 5000);
    });
  }
}
