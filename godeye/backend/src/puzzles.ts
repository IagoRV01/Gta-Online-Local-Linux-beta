// GodEye hacking minigames (Phase 14). A fictional puzzle stands between some actions and the game: the backend makes
// it, keeps the answer, and checks what the browser sends back. Each puzzle is single-use, timed, and bound to one
// user and one exact action, so solving one cannot unlock anything else.
import { randomBytes, randomInt } from "node:crypto";

export type PuzzleKind = "sequence" | "route";
export interface PuzzleRule { kind: PuzzleKind; level: 1 | 2 | 3 }

// what the browser sees: never the answer
export interface SequenceView { kind: "sequence"; title: string; terms: (number | null)[]; options: number[] }
export interface RouteView { kind: "route"; title: string; w: number; h: number; links: [number, number][]; locked: number[]; start: number; end: number }
export type PuzzleView = (SequenceView | RouteView) & { id: string; seconds: number };

interface Pending { user: string; key: string; expires: number; check: (answer: unknown) => boolean }

// ---- sequence: "POLICE NETWORK · CONNECT 4 → 7 → ?" — the missing term of a number pattern, one of four options
function makeSequence(level: number): { view: Omit<SequenceView, "kind" | "title">; answer: number } {
  const n = 5;
  let s: number[];
  const r = randomInt(level === 1 ? 2 : 3);
  if (level === 1) {
    const a = randomInt(1, 20), k = randomInt(2, 10);
    s = Array.from({ length: n }, (_, i) => a + k * i);                      // +k
  } else if (level === 2) {
    if (r === 0) { const a = randomInt(1, 15), p = randomInt(2, 8), q = randomInt(2, 8); s = [a]; for (let i = 1; i < n; i++) s.push(s[i - 1] + (i % 2 ? p : q)); }  // +p +q
    else if (r === 1) { const a = randomInt(1, 6), m = randomInt(2, 4); s = [a]; for (let i = 1; i < n; i++) s.push(s[i - 1] * m); }                          // ×m
    else { const a = randomInt(1, 10), d = randomInt(1, 5); s = [a]; for (let i = 1; i < n; i++) s.push(s[i - 1] + d * i); }                                 // +d +2d +3d
  } else {
    if (r === 0) { s = [randomInt(1, 6), randomInt(2, 9)]; for (let i = 2; i < n; i++) s.push(s[i - 1] + s[i - 2]); }                                        // a+b
    else if (r === 1) { const m = randomInt(2, 4), c = randomInt(-3, 4); s = [randomInt(2, 6)]; for (let i = 1; i < n; i++) s.push(s[i - 1] * m + c); }      // ×m+c
    else { const o = randomInt(0, 9); s = Array.from({ length: n }, (_, i) => (i + 2) * (i + 2) + o); }                                                       // squares
  }
  const hole = level === 3 ? randomInt(1, n) : n - 1;   // the hardest level hides a middle term
  const answer = s[hole];
  const opts = new Set<number>([answer]);
  for (const d of [1, -1, 2, -2, 3, 5, -5, 10, -4, 4]) {
    if (opts.size >= 4) break;
    const v = answer + d * (randomInt(2) ? 1 : randomInt(1, 4));
    if (v >= 0) opts.add(v);
  }
  for (let v = answer + 7; opts.size < 4; v++) opts.add(v);
  const options = [...opts].sort((a, b) => a - b);
  return { view: { terms: s.map((v, i) => (i === hole ? null : v)), options }, answer };
}

// ---- route: "TRAFFIC GRID" — click a path of linked nodes from IN (left) to OUT (right), around the locked ones
function makeRoute(level: number): { view: Omit<RouteView, "kind" | "title">; links: Set<string>; locked: Set<number>; start: number; end: number; w: number } {
  const w = level === 1 ? 4 : 5, h = level === 3 ? 5 : 4;
  for (;;) {
    const links = new Set<string>();
    const key = (a: number, b: number) => (a < b ? `${a}-${b}` : `${b}-${a}`);
    // a random spanning tree (every node reachable), then a few extra links
    const seen = new Set<number>([0]), stack = [0];
    while (stack.length) {
      const c = stack[stack.length - 1];
      const nb = neighbours(c, w, h).filter((x) => !seen.has(x));
      if (!nb.length) { stack.pop(); continue; }
      const x = nb[randomInt(nb.length)];
      links.add(key(c, x)); seen.add(x); stack.push(x);
    }
    for (let i = 0; i < w * h / 3; i++) {
      const a = randomInt(w * h), nb = neighbours(a, w, h);
      links.add(key(a, nb[randomInt(nb.length)]));
    }
    const start = randomInt(h) * w, end = randomInt(h) * w + w - 1;
    const locked = new Set<number>();
    for (let i = 0; i < level + 1; i++) { const c = randomInt(w * h); if (c !== start && c !== end) locked.add(c); }
    const d = shortest(start, end, links, locked, w, h);
    if (d < 0 || d < w) continue;   // no way through, or too easy
    return {
      view: { w, h, links: [...links].map((k) => k.split("-").map(Number) as [number, number]), locked: [...locked], start, end },
      links, locked, start, end, w,
    };
  }
}

function neighbours(c: number, w: number, h: number): number[] {
  const x = c % w, y = (c / w) | 0, out: number[] = [];
  if (x > 0) out.push(c - 1);
  if (x < w - 1) out.push(c + 1);
  if (y > 0) out.push(c - w);
  if (y < h - 1) out.push(c + w);
  return out;
}

function shortest(a: number, b: number, links: Set<string>, locked: Set<number>, w: number, h: number): number {
  const dist = new Map<number, number>([[a, 0]]), q = [a];
  while (q.length) {
    const c = q.shift()!;
    if (c === b) return dist.get(c)!;
    for (const x of neighbours(c, w, h)) {
      if (dist.has(x) || locked.has(x) || !links.has(c < x ? `${c}-${x}` : `${x}-${c}`)) continue;
      dist.set(x, dist.get(c)! + 1); q.push(x);
    }
  }
  return -1;
}

export function routeOk(path: unknown, p: { links: Set<string>; locked: Set<number>; start: number; end: number }): boolean {
  if (!Array.isArray(path) || path.length < 2 || path.length > 64) return false;
  if (path[0] !== p.start || path[path.length - 1] !== p.end) return false;
  const used = new Set<number>();
  for (let i = 0; i < path.length; i++) {
    const c = path[i];
    if (!Number.isInteger(c) || used.has(c) || p.locked.has(c)) return false;
    used.add(c);
    if (i && !p.links.has(path[i - 1] < c ? `${path[i - 1]}-${c}` : `${c}-${path[i - 1]}`)) return false;
  }
  return true;
}

const TITLES: Record<PuzzleKind, string[]> = {
  sequence: ["POLICE NETWORK", "DISPATCH RELAY", "CCTV UPLINK"],
  route: ["TRAFFIC GRID", "POWER GRID", "SIGNAL ROUTER"],
};
const SECONDS: Record<number, number> = { 1: 20, 2: 25, 3: 30 };

export class Puzzles {
  private pending = new Map<string, Pending>();

  // a new puzzle for `user` to unlock `key` (the exact action); it replaces any open one of theirs
  make(user: string, key: string, rule: PuzzleRule, title?: string): PuzzleView {
    this.sweep();
    for (const [id, p] of this.pending) if (p.user === user) this.pending.delete(id);
    const id = randomBytes(12).toString("hex");
    const seconds = SECONDS[rule.level] ?? 25;
    const name = title ?? TITLES[rule.kind][randomInt(TITLES[rule.kind].length)];
    let view: PuzzleView, check: (a: unknown) => boolean;
    if (rule.kind === "sequence") {
      const s = makeSequence(rule.level);
      view = { id, seconds, kind: "sequence", title: name, ...s.view };
      check = (a) => Number(a) === s.answer;
    } else {
      const r = makeRoute(rule.level);
      view = { id, seconds, kind: "route", title: name, ...r.view };
      check = (a) => routeOk(a, r);
    }
    // a little slack for the round trip; the browser shows `seconds`
    this.pending.set(id, { user, key, expires: Date.now() + (seconds + 3) * 1000, check });
    return view;
  }

  // "ok" | "wrong" | "expired" | "unknown": a puzzle is used up by any answer
  solve(user: string, key: string, id: unknown, answer: unknown): "ok" | "wrong" | "expired" | "unknown" {
    const p = this.pending.get(String(id));
    if (!p || p.user !== user || p.key !== key) return "unknown";
    this.pending.delete(String(id));
    if (p.expires < Date.now()) return "expired";
    return p.check(answer) ? "ok" : "wrong";
  }

  private sweep(): void {
    const now = Date.now();
    for (const [id, p] of this.pending) if (p.expires < now) this.pending.delete(id);
  }
}
