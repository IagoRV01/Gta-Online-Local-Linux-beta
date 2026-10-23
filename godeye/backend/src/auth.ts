// GodEye accounts (Phase 10). users.json next to godeye.json holds name, role and an scrypt hash per user. Accounts are
// made on the page (sign-up; the very first one, from the server's own network, becomes the ADMIN), in the ADMIN's
// control panel, or with `node src/users.ts add <name> <role>`. Sessions live in memory (a restart logs everyone out):
// an HttpOnly, SameSite=Strict cookie plus a CSRF token that every POST must echo in X-CSRF-Token.
import { randomBytes, scryptSync, timingSafeEqual } from "node:crypto";
import { existsSync, readFileSync, writeFileSync } from "node:fs";

export const ROLES = ["HACKER", "DISPATCH", "OPERATOR", "ADMIN"] as const;
export type Role = (typeof ROLES)[number];

export interface UserRecord { name: string; role: Role; salt: string; hash: string; created?: string; lastLogin?: string }

export const NAME_RULE = /^[A-Za-z0-9_.-]{2,24}$/;
export function checkNewAccount(name: unknown, password: unknown): string | null {
  if (typeof name !== "string" || !NAME_RULE.test(name)) return "names are 2-24 letters, digits, _ . -";
  if (typeof password !== "string" || password.length < 8 || password.length > 200) return "passwords are 8-200 characters";
  return null;
}
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
    const old = this.users.get(name.toLowerCase());
    this.users.set(name.toLowerCase(), { name, role, ...hashPassword(password), created: old?.created ?? new Date().toISOString(), lastLogin: old?.lastLogin });
  }

  has(name: string): boolean { return this.users.has(String(name).toLowerCase()); }
  get(name: string): UserRecord | undefined { return this.users.get(String(name).toLowerCase()); }

  // a new account (sign-up or the control panel): the name must be free
  create(name: string, role: Role, password: string): string | null {
    const bad = checkNewAccount(name, password);
    if (bad) return bad;
    if (this.has(name)) return "that name is taken";
    this.set(name, role, password);
    this.save();
    return null;
  }

  setRole(name: string, role: Role): boolean {
    const u = this.get(name);
    if (!u || !ROLES.includes(role)) return false;
    u.role = role; this.save(); return true;
  }

  setPassword(name: string, password: string): string | null {
    const u = this.get(name);
    if (!u) return "no such user";
    if (typeof password !== "string" || password.length < 8 || password.length > 200) return "passwords are 8-200 characters";
    Object.assign(u, hashPassword(password)); this.save(); return null;
  }

  touchLogin(name: string): void { const u = this.get(name); if (u) { u.lastLogin = new Date().toISOString(); this.save(); } }

  admins(): number { return [...this.users.values()].filter((u) => u.role === "ADMIN").length; }

  remove(name: string): boolean { return this.users.delete(name.toLowerCase()); }

  list(): { name: string; role: Role; created?: string; lastLogin?: string }[] {
    return [...this.users.values()].map((u) => ({ name: u.name, role: u.role, created: u.created, lastLogin: u.lastLogin }));
  }

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

  // after a role change or a deletion: open sessions follow (a new role) or end (role null)
  updateUser(name: string, role: Role | null): void {
    for (const [id, s] of this.byId)
      if (s.user.toLowerCase() === name.toLowerCase()) { if (role) s.role = role; else this.byId.delete(id); }
  }

  // `secure`: on the public (https) address the cookie is never sent over plain http
  cookie(s: Session | null, secure = false): string {
    const flags = "HttpOnly; SameSite=Strict; Path=/" + (secure ? "; Secure" : "");
    return s ? `ge_session=${s.id}; ${flags}; Max-Age=${SESSION_MS / 1000}` : `ge_session=; ${flags}; Max-Age=0`;
  }
}

// new accounts per address: 3 an hour (sign-up spam)
export class SignupLimiter {
  private made = new Map<string, number[]>();
  allowed(addr: string): boolean {
    const now = Date.now(), m = (this.made.get(addr) ?? []).filter((t) => now - t < 3600000);
    this.made.set(addr, m);
    return m.length < 3;
  }
  made1(addr: string): void { (this.made.get(addr) ?? this.made.set(addr, []).get(addr)!).push(Date.now()); }
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
