import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, appendFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { GAME_DEFAULTS, Game, basePrice, eventLines, restoring, today } from "../src/game.ts";
import { Puzzles, routeOk } from "../src/puzzles.ts";
import { Limits, allowed, toCommand } from "../src/actions.ts";

const quiet = () => {};
const fresh = (over = {}) => new Game({ ...GAME_DEFAULTS, ledgerFile: join(mkdtempSync(join(tmpdir(), "ge-game-")), "credits.jsonl"), ...over }, quiet);

test("prices follow the brief; putting things back is free", () => {
  const p = GAME_DEFAULTS.prices;
  assert.equal(basePrice({ action: "traffic", value: "red" }, p), 50);
  assert.equal(basePrice({ action: "alert" }, p), 100);
  assert.equal(basePrice({ action: "wanted", value: "+1" }, p), 150);
  assert.equal(basePrice({ action: "wanted", value: "+2" }, p), 300);
  assert.equal(basePrice({ action: "roadblock" }, p), 250);
  assert.equal(basePrice({ action: "blackout", value: "on" }, p), 500);
  for (const r of [{ action: "traffic", value: "normal" }, { action: "blackout", value: "off" }, { action: "wanted", value: "0" }, { action: "wanted", value: "-2" }]) {
    assert.ok(restoring(r)); assert.equal(basePrice(r, p), 0);
  }
  assert.equal(basePrice({ action: "event", value: "storm" }, p), 600);
});

test("credits: start, daily bonus once a day, charges, not enough credits", () => {
  const g = fresh();
  assert.equal(g.balance("Ana"), 500);
  g.touch("Ana"); g.touch("ana");
  assert.equal(g.balance("Ana"), 750);
  const r = { action: "blackout", value: "on" };
  const q = g.quote("Ana", "HACKER", r);
  assert.equal(q.price, 500); assert.equal(q.puzzle?.kind, "route"); assert.ok(!q.error);
  g.charge("Ana", "HACKER", r, q.price);
  assert.equal(g.balance("Ana"), 250);
  assert.match(g.quote("Ana", "HACKER", r).error ?? "", /not enough credits/);
  assert.ok(!g.quote("Ana", "HACKER", { action: "blackout", value: "off" }).error);   // lights on: always possible
});

test("daily limits count only real uses, per day", () => {
  const g = fresh({ startCredits: 100000 });
  const r = { action: "blackout", value: "on" };
  for (let i = 0; i < 3; i++) { assert.ok(!g.quote("Bo", "HACKER", r).error); g.charge("Bo", "HACKER", r, 500); }
  assert.match(g.quote("Bo", "HACKER", r).error ?? "", /daily limit/);
  g.charge("Bo", "HACKER", { action: "blackout", value: "off" }, 0);
  assert.equal(g.usedToday("Bo", "blackout"), 3);
  assert.equal(g.usedToday("Other", "blackout"), 0);
});

test("ADMIN plays by default; adminPlays false makes it free", () => {
  assert.equal(fresh().quote("Root", "ADMIN", { action: "roadblock", target: 1 }).price, 250);
  const g = fresh({ adminPlays: false });
  const q = g.quote("Root", "ADMIN", { action: "roadblock", target: 1 });
  assert.equal(q.price, 0); assert.equal(q.puzzle, null);
});

test("the ledger survives a restart, a damaged last line included", () => {
  const g = fresh();
  g.touch("Cy");
  g.charge("Cy", "DISPATCH", { action: "alert", target: 1 }, 100);
  g.grant("Cy", 40, "Root");
  const file = g.cfg.ledgerFile;
  appendFileSync(file, '{"at":"2026-10-23T14:02:18Z","user":"Cy","del');   // the power went off mid-write
  const g2 = new Game(g.cfg, quiet);
  assert.equal(g2.balance("Cy"), 500 + 250 - 100 + 40);
  assert.equal(g2.usedToday("Cy", "alert"), 1);
  g2.touch("Cy");   // the bonus is not paid twice
  assert.equal(g2.balance("Cy"), 690);
  assert.ok(readFileSync(file, "utf8").includes(`"day":"${today()}"`));
});

test("income only for open pages and only while someone plays", () => {
  const g = fresh({ dailyBonus: 0 });
  g.touch("Di");
  g.income(false);
  assert.equal(g.balance("Di"), 500);
  g.income(true);
  assert.equal(g.balance("Di"), 510);
});

test("bounties pay when the target dies in time", () => {
  const g = fresh();
  g.addBounty("Ed", 7, 2, 300);
  assert.deepEqual(g.checkBounties(() => 2), []);
  const paid = g.checkBounties((id) => (id === 7 ? 3 : undefined));
  assert.equal(paid.length, 1); assert.equal(paid[0].reward, 450);
  assert.equal(g.balance("Ed"), 950);
  assert.deepEqual(g.checkBounties(() => 9), []);   // paid once
});

test("events: commands, one at a time, price modifiers, restored after a restart", () => {
  assert.equal(eventLines("manhunt", null, ""), null);
  const m = eventLines("manhunt", 3, "Alex\nweather SNOW");
  assert.deepEqual(m?.start.slice(0, 1), ["wanted 3 +3"]);
  assert.ok(m?.start.every((l) => !l.includes("\n")));
  const g = fresh();
  const hh = eventLines("happyhour", null, "")!;
  g.startEvent("happyhour", "Op", null, "", hh.end);
  assert.equal(g.quote("Fa", "HACKER", { action: "blackout", value: "on" }).price, 250);
  const g2 = new Game(g.cfg, quiet);
  assert.equal(g2.event?.key, "happyhour");
  g2.endEvent();
  assert.equal(new Game(g.cfg, quiet).event, null);
});

test("event action: OPERATOR only, checked", () => {
  const playing = (id: number) => id === 1;
  assert.ok(allowed("OPERATOR", "event") && !allowed("HACKER", "event") && !allowed("DISPATCH", "event"));
  assert.equal(toCommand({ action: "event", value: "storm" }, playing).line, "event storm");
  assert.equal(toCommand({ action: "event", value: "manhunt", target: 1 }, playing).line, "event manhunt 1");
  assert.ok(toCommand({ action: "event", value: "manhunt", target: 2 }, playing).error);
  assert.ok(toCommand({ action: "event", value: "storm; blackout on" }, playing).error);
});

test("minigames: bound to the user and the action, single use", () => {
  const pz = new Puzzles();
  for (let i = 0; i < 50; i++) {
    const v = pz.make("Gu", "traffic red 120", { kind: "sequence", level: ((i % 3) + 1) as 1 | 2 | 3 });
    assert.equal(v.kind, "sequence");
    if (v.kind !== "sequence") continue;
    assert.equal(v.terms.filter((t) => t === null).length, 1);
    assert.equal(new Set(v.options).size, 4);
    const tries = v.options.map((o) => ({ o, res: pz.solve("Gu", "traffic red 120", v.id, o) }));
    assert.equal(tries.filter((t) => t.res === "ok").length, 0 + (tries[0].res === "ok" ? 1 : 0));   // first answer uses it up
  }
  const v = pz.make("Gu", "blackout on", { kind: "sequence", level: 1 });
  assert.equal(pz.solve("Other", "blackout on", v.id, 0), "unknown");
  assert.equal(pz.solve("Gu", "traffic red 120", v.id, 0), "unknown");
});

test("minigames: the right answer opens, a wrong one does not", () => {
  const pz = new Puzzles();
  const v = pz.make("Ha", "traffic red 60", { kind: "sequence", level: 1 });
  if (v.kind !== "sequence") throw new Error("kind");
  const known = v.terms.filter((t): t is number => t !== null);
  const step = known[1] - known[0];
  const answer = known[known.length - 1] + step;   // level 1: +k, the last term is missing
  assert.ok(v.options.includes(answer));
  const wrong = v.options.find((o) => o !== answer)!;
  const v2 = pz.make("Ha", "traffic red 60", { kind: "sequence", level: 1 });
  assert.equal(pz.solve("Ha", "traffic red 60", v.id, answer), "unknown");   // replaced by the newer puzzle
  if (v2.kind !== "sequence") throw new Error("kind");
  const k2 = v2.terms.filter((t): t is number => t !== null);
  const a2 = k2[k2.length - 1] + (k2[1] - k2[0]);
  assert.ok(wrong !== answer);
  assert.equal(pz.solve("Ha", "traffic red 60", v2.id, v2.options.find((o) => o !== a2)), "wrong");
  const v3 = pz.make("Ha", "traffic red 60", { kind: "sequence", level: 1 });
  if (v3.kind !== "sequence") throw new Error("kind");
  const k3 = v3.terms.filter((t): t is number => t !== null);
  assert.equal(pz.solve("Ha", "traffic red 60", v3.id, k3[k3.length - 1] + (k3[1] - k3[0])), "ok");
});

test("routes: always solvable, and only real paths count", () => {
  const pz = new Puzzles();
  for (let i = 0; i < 40; i++) {
    const v = pz.make("Iz", "roadblock 1", { kind: "route", level: ((i % 3) + 1) as 1 | 2 | 3 });
    if (v.kind !== "route") throw new Error("kind");
    const links = new Set(v.links.map(([a, b]) => `${Math.min(a, b)}-${Math.max(a, b)}`));
    const locked = new Set(v.locked);
    // breadth-first search the way a player would click it
    const prev = new Map<number, number>([[v.start, -1]]), q = [v.start];
    while (q.length) {
      const c = q.shift()!;
      for (const [a, b] of v.links) {
        const x = a === c ? b : b === c ? a : -1;
        if (x < 0 || prev.has(x) || locked.has(x)) continue;
        prev.set(x, c); q.push(x);
      }
    }
    assert.ok(prev.has(v.end), "a way through exists");
    const path: number[] = [];
    for (let c = v.end; c !== -1; c = prev.get(c)!) path.unshift(c);
    const p = { links, locked, start: v.start, end: v.end };
    assert.ok(routeOk(path, p));
    assert.ok(!routeOk([v.start, v.end], p) || links.has(`${Math.min(v.start, v.end)}-${Math.max(v.start, v.end)}`));
    assert.ok(!routeOk([...path, path[path.length - 2]], p));
    assert.equal(pz.solve("Iz", "roadblock 1", v.id, path), "ok");
  }
});

test("a failed minigame locks the action", () => {
  const l = new Limits();
  const until = l.block("Jo", "traffic", 15);
  assert.ok(until > Date.now() + 14000);
  assert.ok(!l.check("Jo", "HACKER", "traffic").ok);
  assert.ok(l.check("Jo", "HACKER", "blackout").ok);
});
