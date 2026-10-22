import { test } from "node:test";
import assert from "node:assert/strict";
import { Limits, allowed, cleanText, toCommand } from "../src/actions.ts";
import { Sessions, Users, hashPassword } from "../src/auth.ts";

const playing = (id: number) => id === 1;

test("roles", () => {
  assert.ok(allowed("DISPATCH", "wanted"));
  assert.ok(!allowed("DISPATCH", "traffic"));
  assert.ok(allowed("HACKER", "traffic") && allowed("HACKER", "blackout"));
  assert.ok(!allowed("HACKER", "roadblock"));
  assert.ok(allowed("OPERATOR", "weather") && !allowed("OPERATOR", "wanted"));
  assert.ok(allowed("OPERATOR", "camera") && allowed("HACKER", "camera") && !allowed("DISPATCH", "camera"));
  assert.ok(allowed("ADMIN", "roadblock") && allowed("ADMIN", "traffic"));
  assert.ok(!allowed("ADMIN", "shutdown"));
});

test("commands are built only from checked values", () => {
  assert.equal(toCommand({ action: "wanted", target: 1, value: "+2" }, playing).line, "wanted 1 +2");
  assert.equal(toCommand({ action: "wanted", target: 1, value: "0" }, playing).line, "wanted 1 0");
  assert.ok(toCommand({ action: "wanted", target: 1, value: "6" }, playing).error);
  assert.ok(toCommand({ action: "wanted", target: 2, value: "+1" }, playing).error);           // not in the world
  assert.ok(toCommand({ action: "wanted", target: "1; time 0 0" as any, value: "+1" }, playing).error);
  assert.equal(toCommand({ action: "traffic", value: "red", seconds: 60 }, playing).line, "traffic red 60");
  assert.equal(toCommand({ action: "traffic", value: "normal", seconds: 60 }, playing).line, "traffic normal 0");
  assert.ok(toCommand({ action: "traffic", value: "purple" }, playing).error);
  assert.ok(toCommand({ action: "traffic", value: "red", seconds: 9999 }, playing).error);
  assert.equal(toCommand({ action: "weather", value: "RAIN" }, playing).line, "weather RAIN");
  assert.ok(toCommand({ action: "weather", value: "RAIN\ntime 0 0" }, playing).error);
  assert.equal(toCommand({ action: "time", value: "night" }, playing).line, "time 0 0");
  assert.equal(toCommand({ action: "notify", target: "all", text: "hello\nwanted 1 5" }, playing).line, "notify all hello wanted 1 5");
  assert.equal(toCommand({ action: "alert", target: 1 }, playing).line, "alert 1 Dispatch has your description.");
  assert.equal(cleanText("a\u0000b\r\nc"), "a b c");
});

test("camera commands", () => {
  assert.equal(toCommand({ action: "camera", camera: "all", value: "cctv", text: "CAM_002" }, playing).line, "camera all cctv CAM_002");
  assert.equal(toCommand({ action: "camera", camera: 5, value: "heli", target: 1 }, playing).line, "camera 5 heli 1");
  assert.ok(toCommand({ action: "camera", camera: "all", value: "cctv", text: "CAM 1; time 0 0" }, playing).error);
  assert.ok(toCommand({ action: "camera", camera: "all", value: "orbit", target: 1 }, playing).error);
  assert.ok(toCommand({ action: "camera", camera: "all", value: "follow", target: 2 }, playing).error);   // not in the world
});

test("cooldowns and the per-minute cap", () => {
  const l = new Limits();
  assert.ok(l.check("ana", "DISPATCH", "roadblock").ok);
  l.used("ana", "DISPATCH", "roadblock");
  const c = l.check("ana", "DISPATCH", "roadblock");
  assert.ok(!c.ok && c.waitS! > 50);
  assert.ok(l.check("ana", "DISPATCH", "wanted").ok);   // other actions are free
  assert.ok(l.check("bob", "DISPATCH", "roadblock").ok);  // other users too
  for (let i = 0; i < 20; i++) l.used("cid", "ADMIN", "notify");
  assert.ok(!l.check("cid", "ADMIN", "weather").ok);      // 20 a minute
});

test("passwords and sessions", () => {
  const { salt, hash } = hashPassword("correct horse");
  assert.equal(hashPassword("correct horse", salt).hash, hash);
  assert.notEqual(hashPassword("wrong", salt).hash, hash);
  const users = new Users("does-not-exist.json");
  users.set("Ana", "DISPATCH", "correct horse");
  assert.equal(users.check("ana", "correct horse")?.role, "DISPATCH");
  assert.equal(users.check("ana", "nope"), null);
  assert.equal(users.check("nobody", "x"), null);
  const sessions = new Sessions();
  const s = sessions.create(users.check("Ana", "correct horse")!);
  assert.equal(sessions.get(`a=1; ge_session=${s.id}`)?.user, "Ana");
  assert.equal(sessions.get("ge_session=" + "0".repeat(64)), null);
  sessions.end(s);
  assert.equal(sessions.get(`ge_session=${s.id}`), null);
});
