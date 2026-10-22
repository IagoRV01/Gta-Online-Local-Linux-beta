// GodEye accounts (Phase 10). users.json next to godeye.json holds name, role and an scrypt hash per user; it is made
// with `node src/users.ts add <name> <role>`. Sessions live in memory (a restart logs everyone out): an HttpOnly,
// SameSite=Strict cookie plus a CSRF token that every POST must echo in X-CSRF-Token.
import { randomBytes, scryptSync, timingSafeEqual } from "node:crypto";
import { existsSync, readFileSync, writeFileSync } from "node:fs";

export const ROLES = ["HACKER", "DISPATCH", "OPERATOR", "ADMIN"] as const;
export type Role = (typeof ROLES)[number];

export interface UserRecord { name: string; role: Role; salt: string; hash: string }
export interface Session { id: string; user: string; role: Role; csrf: string; expires: number }

const SESSION_MS = 12 * 3600 * 1000;
const KEYLEN = 32;

export function hashPassword(password: string, salt = randomBytes(16).toString("hex")): { salt: string; hash: string } {
  return { salt, hash: scryptSync(password, salt, KEYLEN, { N: 16384, r: 8, p: 1 }).toString("hex") };
}

export class Users {
  private file: string;
  private users = new Map<string, UserRecord>();

  constructor(file: string) { this.file = file; this.reload(); }

  reload(): void {
    this.users.clear();
    if (!existsSync(this.file)) return;
    for (const u of JSON.parse(readFileSync(this.file, "utf8")) as UserRecord[])
      if (u && typeof u.name === "string" && ROLES.includes(u.role)) this.users.set(u.name.toLowerCase(), u);
  }

  get count(): number { return this.users.size; }

  save(): void { writeFileSync(this.file, JSON.stringify([...this.users.values()], null, 2) + "\n", { mode: 0o600 }); }

  set(name: string, role: Role, password: string): void {
    this.users.set(name.toLowerCase(), { name, role, ...hashPassword(password) });
  }

  remove(name: string): boolean { return this.users.delete(name.toLowerCase()); }

  list(): { name: string; role: Role }[] { return [...this.users.values()].map((u) => ({ name: u.name, role: u.role })); }

  // the record when the password matches; the same work is done for unknown names (no timing hint)
  check(name: string, password: string): UserRecord | null {
    const u = this.users.get(String(name).toLowerCase());
    const { hash } = hashPassword(String(password), u?.salt ?? "0000000000000000");
    if (!u) return null;
    const a = Buffer.from(hash, "hex"), b = Buffer.from(u.hash, "hex");
    return a.length === b.length && timingSafeEqual(a, b) ? u : null;
  }
}

export class Sessions {
  private byId = new Map<string, Session>();

  create(u: UserRecord): Session {
    const s: Session = { id: randomBytes(32).toString("hex"), user: u.name, role: u.role, csrf: randomBytes(24).toString("hex"), expires: Date.now() + SESSION_MS };
    this.byId.set(s.id, s);
    return s;
  }

  get(cookieHeader: string | undefined): Session | null {
    const m = /(?:^|;\s*)ge_session=([0-9a-f]{64})/.exec(cookieHeader ?? "");
    if (!m) return null;
    const s = this.byId.get(m[1]);
    if (!s) return null;
    if (s.expires < Date.now()) { this.byId.delete(s.id); return null; }
    return s;
  }

  end(s: Session): void { this.byId.delete(s.id); }

  cookie(s: Session | null): string {
    return s ? `ge_session=${s.id}; HttpOnly; SameSite=Strict; Path=/; Max-Age=${SESSION_MS / 1000}`
             : "ge_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0";
  }
}

// failed logins per address: 5 in 5 minutes, then a wait
export class LoginLimiter {
  private fails = new Map<string, number[]>();

  blocked(addr: string): boolean {
    const now = Date.now();
    const f = (this.fails.get(addr) ?? []).filter((t) => now - t < 300000);
    this.fails.set(addr, f);
    return f.length >= 5;
  }

  fail(addr: string): void { (this.fails.get(addr) ?? this.fails.set(addr, []).get(addr)!).push(Date.now()); }
  ok(addr: string): void { this.fails.delete(addr); }
}
