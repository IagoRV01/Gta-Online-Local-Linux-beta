// A small RFC 6455 WebSocket server on top of node:http (Node has a WebSocket client built in, but no server).
// Text frames only, payloads up to 64 KB from the browser; ping/pong and close are handled here.
import { createHash } from "node:crypto";
import type { IncomingMessage } from "node:http";
import type { Duplex } from "node:stream";

const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const MAX_IN = 64 * 1024;

export class WsClient {
  private buf = Buffer.alloc(0);
  closed = false;
  readonly address: string;
  onMessage: (text: string) => void = () => {};
  onClose: () => void = () => {};
  private sock: Duplex;

  constructor(sock: Duplex, address: string) {
    this.sock = sock;
    this.address = address;
    sock.on("data", (d: Buffer) => this.read(d));
    sock.on("close", () => this.finish());
    sock.on("error", () => this.finish());
  }

  send(text: string): void {
    if (this.closed) return;
    if ((this.sock as any).writableLength > 4 << 20) { this.close(); return; }   // a stalled browser
    this.frame(0x1, Buffer.from(text, "utf8"));
  }

  close(): void {
    if (this.closed) return;
    this.frame(0x8, Buffer.alloc(0));
    this.sock.end();
    this.finish();
  }

  private finish(): void {
    if (this.closed) return;
    this.closed = true;
    this.onClose();
  }

  private frame(op: number, payload: Buffer): void {
    const n = payload.length;
    const head = n < 126 ? Buffer.from([0x80 | op, n]) : n < 65536 ? Buffer.alloc(4) : Buffer.alloc(10);
    if (n >= 126 && n < 65536) { head[0] = 0x80 | op; head[1] = 126; head.writeUInt16BE(n, 2); }
    else if (n >= 65536) { head[0] = 0x80 | op; head[1] = 127; head.writeBigUInt64BE(BigInt(n), 2); }
    this.sock.write(Buffer.concat([head, payload]));
  }

  private read(d: Buffer): void {
    this.buf = Buffer.concat([this.buf, d]);
    for (;;) {
      if (this.buf.length < 2) return;
      const op = this.buf[0] & 0x0f, masked = (this.buf[1] & 0x80) !== 0;
      let len = this.buf[1] & 0x7f, off = 2;
      if (len === 126) { if (this.buf.length < 4) return; len = this.buf.readUInt16BE(2); off = 4; }
      else if (len === 127) { if (this.buf.length < 10) return; const big = this.buf.readBigUInt64BE(2); len = big > BigInt(MAX_IN) ? MAX_IN + 1 : Number(big); off = 10; }
      if (!masked || len > MAX_IN) { this.close(); return; }   // browsers always mask; nothing big comes from them
      if (this.buf.length < off + 4 + len) return;
      const mask = this.buf.subarray(off, off + 4);
      const data = Buffer.from(this.buf.subarray(off + 4, off + 4 + len));
      for (let i = 0; i < data.length; i++) data[i] ^= mask[i & 3];
      this.buf = this.buf.subarray(off + 4 + len);
      if (op === 0x1) this.onMessage(data.toString("utf8"));
      else if (op === 0x8) { this.close(); return; }
      else if (op === 0x9) this.frame(0xa, data);
    }
  }
}

// Completes the handshake, or answers 400/403 and returns null. Only same-origin pages may connect: another site open in
// the same browser must not be able to read the telemetry.
export function acceptUpgrade(req: IncomingMessage, sock: Duplex, address: string): WsClient | null {
  const key = req.headers["sec-websocket-key"];
  const origin = req.headers["origin"];
  const host = req.headers["host"];
  const bad = (code: string) => { sock.end(`HTTP/1.1 ${code}\r\nConnection: close\r\n\r\n`); return null; };
  if (typeof key !== "string" || (req.headers["upgrade"] ?? "").toLowerCase() !== "websocket") return bad("400 Bad Request");
  if (origin) {
    let ok = false;
    try { ok = new URL(origin).host === host; } catch { ok = false; }
    if (!ok) return bad("403 Forbidden");
  }
  const accept = createHash("sha1").update(key + GUID).digest("base64");
  sock.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  return new WsClient(sock, address);
}
