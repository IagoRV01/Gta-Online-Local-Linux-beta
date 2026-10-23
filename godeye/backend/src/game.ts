// GodEye as an asymmetric web game (Phase 14): Hack Credits, daily limits, bounties and timed events, for people who
// play without GTA. All of it is fictional and stays inside GodEye and the game server.
// Credits live in an append-only ledger (one JSON line per change, flushed to disk each time): balances, today's uses
// and a running event are rebuilt from it at start, so a crash or a power cut loses nothing.
import { closeSync, existsSync, fsyncSync, mkdirSync, openSync, readFileSync, writeSync } from "node:fs";
import { dirname } from "node:path";
import { cleanText, type ActionRequest } from "./actions.ts";
import type { Role } from "./auth.ts";
import type { PuzzleRule } from "./puzzles.ts";

export interface GameConfig {
  enabled: boolean;
  ledgerFile: string;
  startCredits: number;
  dailyBonus: number;               // the first visit of each day
  incomePerMinute: number;          // while the page is open and someone is in the world
  prices: Record<string, number>;   // wanted: per star added
  daily: Record<string, number>;    // uses a day per action (restoring things is never limited)
  puzzles: Record<string, PuzzleRule>;
  bountyMinutes: number;            // a DISPATCH "+n wanted" pays back price × bountyRate when the target dies in time
  bountyRate: number;
  adminPlays: boolean;              // false: ADMIN acts free, without puzzles or daily limits
}

export const GAME_DEFAULTS: GameConfig = {
  enabled: true,
  ledgerFile: "credits.jsonl",
  startCredits: 500,
  dailyBonus: 250,
  incomePerMinute: 10,
  prices: { traffic: 50, alert: 100, wanted: 150, roadblock: 250, blackout: 500, weather: 25, time: 25, notify: 10, camera: 0 },
  daily: { blackout: 3, roadblock: 5, wanted: 10, traffic: 10, alert: 10, event: 5 },
  puzzles: {
    traffic: { kind: "sequence", level: 1 },
    wanted: { kind: "sequence", level: 2 },
    roadblock: { kind: "route", level: 1 },
    blackout: { kind: "route", level: 2 },
  },
  bountyMinutes: 3,
  bountyRate: 1.5,
  adminPlays: true,
};

// OPERATOR events: a few server commands now, a few when the time is up. {t} = the target's id, {name} = their name.
export interface EventDef { label: string; blurb: string; cost: number; minutes: number; target?: boolean; start: string[]; end: string[]; priceX?: number; bountyX?: number }
export const EVENTS: Record<string, EventDef> = {
  storm: {
    label: "Electrical storm", blurb: "Thunder and a citywide blackout", cost: 600, minutes: 5,
    start: ["weather THUNDER", "blackout on", "notify all An electrical storm has knocked out the city's power."],
    end: ["blackout off", "weather auto", "notify all The power is back on."],
  },
  gridlock: {
    label: "Gridlock", blurb: "Every traffic light red", cost: 400, minutes: 3,
    start: ["traffic red 180", "notify all Hackers have taken over the traffic lights."],
    end: ["traffic normal 0"],
  },
  manhunt: {
    label: "Manhunt", blurb: "3 stars on a player; bounties pay double", cost: 500, minutes: 5, target: true, bountyX: 2,
    start: ["wanted {t} +3", "alert {t} Citywide manhunt: every unit is looking for you.", "notify all Manhunt: {name} is wanted citywide."],
    end: ["notify all The manhunt is over."],
  },
  happyhour: {
    label: "Happy hour", blurb: "Every hack costs half", cost: 300, minutes: 10, priceX: 0.5,
    start: [], end: [],
  },
};

// putting things back costs nothing, needs no puzzle and has no daily limit
export function restoring(r: ActionRequest): boolean {
  return (r.action === "traffic" && r.value === "normal") || (r.action === "blackout" && r.value === "off") || (r.action === "event" && r.value === "stop")
    || (r.action === "wanted" && !/^\+/.test(String(r.value ?? "")));
}

export function basePrice(r: ActionRequest, prices: Record<string, number>): number {
  if (r.action === "event") return EVENTS[String(r.value)]?.cost ?? 0;
  if (restoring(r)) return 0;
  if (r.action === "wanted") return (prices.wanted ?? 0) * Number(/^\+([1-5])$/.exec(String(r.value))?.[1] ?? 0);
  return prices[r.action] ?? 0;
}

// the server commands of an event, with the target filled in; null when the target is missing or not in the world
export function eventLines(key: string, target: number | null, name: string): { start: string[]; end: string[] } | null {
  const e = EVENTS[key];
  if (!e || (e.target && target == null)) return null;
  const fill = (l: string) => l.replaceAll("{t}", String(target)).replaceAll("{name}", cleanText(name).slice(0, 24));
  return { start: e.start.map(fill), end: e.end.map(fill) };
}

export const today = (t = new Date()) => t.toLocaleDateString("sv-SE");   // YYYY-MM-DD, local time

interface Entry { at: string; t?: "event" | "eventEnd"; user?: string; delta?: number; why?: string; action?: string; day?: string; [k: string]: unknown }
export interface ActiveEvent { id: string; key: string; label: string; by: string; target: number | null; targetName: string; startsAt: number; endsAt: number; end: string[]; priceX: number; bountyX: number }
interface Bounty { user: string; target: number; deaths: number; until: number; reward: number }

export class Game {
  readonly cfg: GameConfig;
  private log: (level: string, m: string) => void;
  private balances = new Map<string, number>();   // by lower-case name
  private names = new Map<string, string>();
  private used = new Map<string, number>();       // day|user|action -> uses
  private bonusDay = new Map<string, string>();
  private seen = new Map<string, number>();
  private bounties: Bounty[] = [];
  event: ActiveEvent | null = null;

  constructor(cfg: GameConfig, log: (level: string, m: string) => void) {
    this.cfg = cfg;
    this.log = log;
    if (!existsSync(cfg.ledgerFile)) return;
    let n = 0;
    for (const line of readFileSync(cfg.ledgerFile, "utf8").split("\n")) {
      if (!line.trim()) continue;
      let e: Entry;
      try { e = JSON.parse(line); } catch { this.log("WARN", `game: skipping a damaged ledger line`); continue; }
      this.apply(e);
      n++;
    }
    this.log("INFO", `game: ${n} ledger entries, ${this.balances.size} players${this.event ? `, event "${this.event.label}" running` : ""}`);
  }

  private apply(e: Entry): void {
    if (e.t === "event") { this.event = e as unknown as ActiveEvent; return; }
    if (e.t === "eventEnd") { if (this.event?.id === e.id) this.event = null; return; }
    if (!e.user) return;
    const k = e.user.toLowerCase();
    this.names.set(k, e.user);
    this.balances.set(k, (this.balances.get(k) ?? 0) + (e.delta ?? 0));
    if (e.why === "daily" && e.day) this.bonusDay.set(k, e.day);
    if (e.action && e.day) this.used.set(`${e.day}|${k}|${e.action}`, (this.used.get(`${e.day}|${k}|${e.action}`) ?? 0) + 1);
  }

  // one line, on disk before the change counts
  private write(e: Entry): void {
    mkdirSync(dirname(this.cfg.ledgerFile), { recursive: true });
    const fd = openSync(this.cfg.ledgerFile, "a", 0o600);
    try { writeSync(fd, JSON.stringify(e) + "\n"); fsyncSync(fd); } finally { closeSync(fd); }
    this.apply(e);
  }

  private credit(user: string, delta: number, why: string, extra: Partial<Entry> = {}): void {
    if (why !== "start") this.ensure(user);
    this.write({ at: new Date().toISOString(), user, delta, why, ...extra });
  }

  private ensure(user: string): void {
    if (!this.balances.has(user.toLowerCase())) this.credit(user, this.cfg.startCredits, "start");
  }

  balance(user: string): number { this.ensure(user); return this.balances.get(user.toLowerCase()) ?? 0; }

  free(role: Role): boolean { return role === "ADMIN" && !this.cfg.adminPlays; }

  // a visit: counts for the income, and the first one of the day pays the daily bonus
  touch(user: string): void {
    this.ensure(user);
    const k = user.toLowerCase();
    this.seen.set(k, Date.now());
    const d = today();
    if (this.cfg.dailyBonus > 0 && this.bonusDay.get(k) !== d) this.credit(user, this.cfg.dailyBonus, "daily", { day: d });
  }

  priceX(): number { return this.event?.priceX ?? 1; }

  price(r: ActionRequest, role: Role): number {
    if (this.free(role)) return 0;
    const p = basePrice(r, this.cfg.prices);
    return r.action === "event" ? p : Math.round(p * this.priceX());
  }

  usedToday(user: string, action: string): number { return this.used.get(`${today()}|${user.toLowerCase()}|${action}`) ?? 0; }

  // what an action needs before it may run: its price, maybe a puzzle; or why it cannot
  quote(user: string, role: Role, r: ActionRequest): { price: number; puzzle: PuzzleRule | null; error?: string } {
    const price = this.price(r, role);
    if (this.free(role) || restoring(r)) return { price, puzzle: null };
    const limit = this.cfg.daily[r.action];
    if (limit != null && this.usedToday(user, r.action) >= limit) return { price, puzzle: null, error: `daily limit reached: ${limit} a day` };
    if (price > this.balance(user)) return { price, puzzle: null, error: `not enough credits: ${price} needed, ${this.balance(user)} left` };
    return { price, puzzle: this.cfg.puzzles[r.action] ?? null };
  }

  // the action ran: pay for it and count it
  charge(user: string, role: Role, r: ActionRequest, price: number): void {
    if (this.free(role)) return;
    const counted = !restoring(r) && this.cfg.daily[r.action] != null;
    if (price === 0 && !counted) return;
    this.credit(user, -price, r.action, counted ? { action: r.action, day: today() } : {});
  }

  grant(user: string, amount: number, by: string): void { this.credit(user, amount, `grant by ${by}`); }

  // every minute: everyone with the page open (seen in the last 2 minutes) earns, while someone is in the world
  income(someonePlaying: boolean): void {
    if (!someonePlaying || this.cfg.incomePerMinute <= 0) return;
    const now = Date.now();
    for (const [k, t] of this.seen) if (now - t < 120000) this.credit(this.names.get(k) ?? k, this.cfg.incomePerMinute, "income");
  }

  // ---- bounties: DISPATCH bets that the police get their target
  addBounty(user: string, target: number, deaths: number, price: number): void {
    if (price <= 0) return;
    const reward = Math.round(price * this.cfg.bountyRate * (this.event?.bountyX ?? 1));
    this.bounties.push({ user, target, deaths, until: Date.now() + this.cfg.bountyMinutes * 60000, reward });
  }

  // with each telemetry update: a target who died pays; old bounties lapse. Returns the paid ones.
  checkBounties(deathsOf: (id: number) => number | undefined): { user: string; target: number; reward: number }[] {
    const now = Date.now(), paid: { user: string; target: number; reward: number }[] = [];
    this.bounties = this.bounties.filter((b) => {
      const d = deathsOf(b.target);
      if (d != null && d > b.deaths) { this.credit(b.user, b.reward, "bounty", { target: b.target }); paid.push(b); return false; }
      return b.until > now;
    });
    return paid;
  }

  // ---- events
  startEvent(key: string, by: string, target: number | null, targetName: string, end: string[]): ActiveEvent {
    const e = EVENTS[key], now = Date.now();
    const ev: ActiveEvent = { id: `${now.toString(36)}${key}`, key, label: e.label, by, target, targetName, startsAt: now, endsAt: now + e.minutes * 60000, end, priceX: e.priceX ?? 1, bountyX: e.bountyX ?? 1 };
    this.write({ at: new Date().toISOString(), t: "event", ...ev });
    return ev;
  }

  endEvent(): void { if (this.event) this.write({ at: new Date().toISOString(), t: "eventEnd", id: this.event.id }); }

  publicEvent(): object | null {
    const e = this.event;
    return e ? { key: e.key, label: e.label, by: e.by, targetName: e.targetName, endsAt: e.endsAt, priceX: e.priceX, bountyX: e.bountyX } : null;
  }

  board(roleOf: (user: string) => Role | undefined): { user: string; role: string; credits: number }[] {
    return [...this.balances].map(([k, c]) => ({ user: this.names.get(k) ?? k, role: roleOf(k) ?? "", credits: c }))
      .filter((x) => x.role).sort((a, b) => b.credits - a.credits).slice(0, 10);
  }

  // what the page shows the logged-in user
  status(user: string, role: Role, can: string[]): object {
    const prices: Record<string, number> = {}, left: Record<string, number> = {};
    for (const a of can) {
      prices[a] = this.free(role) ? 0 : Math.round(basePrice({ action: a, value: a === "wanted" ? "+1" : "" }, this.cfg.prices) * (a === "event" ? 1 : this.priceX()));
      const lim = this.cfg.daily[a];
      if (lim != null && !this.free(role)) left[a] = Math.max(0, lim - this.usedToday(user, a));
    }
    const events = can.includes("event") ? Object.entries(EVENTS).map(([key, e]) => ({ key, label: e.label, blurb: e.blurb, cost: this.free(role) ? 0 : e.cost, minutes: e.minutes, target: !!e.target })) : [];
    return {
      credits: this.balance(user), prices, left, free: this.free(role),
      puzzles: this.free(role) ? [] : Object.keys(this.cfg.puzzles).filter((a) => can.includes(a)),
      income: this.cfg.incomePerMinute, events,
    };
  }
}
