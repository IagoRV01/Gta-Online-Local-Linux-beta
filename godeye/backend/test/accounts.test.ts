import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { Sessions, SignupLimiter, Users, checkNewAccount } from "../src/auth.ts";
import { SIGNUP_ROLES, SettingsStore, tail } from "../src/panel.ts";

const dir = () => mkdtempSync(join(tmpdir(), "ge-acc-"));

test("new accounts: names, passwords, taken names in any case", () => {
  assert.ok(checkNewAccount("x", "long-enough"));
  assert.ok(checkNewAccount("bad name", "long-enough"));
  assert.ok(checkNewAccount("Good_Name", "short"));
  assert.equal(checkNewAccount("Good_Name", "long-enough"), null);
  const u = new Users(join(dir(), "users.json"));
  assert.equal(u.create("Ana", "HACKER", "ana-password"), null);
  assert.match(u.create("ANA", "DISPATCH", "ana-password") ?? "", /taken/);
  assert.equal(u.check("ana", "ana-password")?.role, "HACKER");
  assert.ok(u.get("Ana")?.created);
});

test("roles, passwords, deletion, saved to disk", () => {
  const f = join(dir(), "users.json");
  const u = new Users(f);
  u.create("Boss", "ADMIN", "boss-password");
  u.create("Cy", "DISPATCH", "cy-password");
  assert.equal(u.admins(), 1);
  assert.ok(u.setRole("cy", "OPERATOR"));
  assert.equal(u.setPassword("Cy", "short"), "passwords are 8-200 characters");
  assert.equal(u.setPassword("Cy", "new-cy-password"), null);
  u.touchLogin("Cy");
  const again = new Users(f);
  assert.equal(again.check("Cy", "new-cy-password")?.role, "OPERATOR");
  assert.ok(again.get("cy")?.lastLogin);
  assert.ok(again.remove("cy")); again.save();
  assert.equal(new Users(f).has("Cy"), false);
  assert.ok(!JSON.parse(readFileSync(f, "utf8")).some((x: any) => /password/.test(JSON.stringify(Object.keys(x)))));
});

test("open sessions follow a role change and end on deletion", () => {
  const s = new Sessions();
  const a = s.create({ name: "Di", role: "HACKER", salt: "", hash: "" });
  s.updateUser("di", "OPERATOR");
  assert.equal(s.get(`ge_session=${a.id}`)?.role, "OPERATOR");
  s.updateUser("DI", null);
  assert.equal(s.get(`ge_session=${a.id}`), null);
});

test("sign-up limit: 3 accounts an hour per address", () => {
  const l = new SignupLimiter();
  for (let i = 0; i < 3; i++) { assert.ok(l.allowed("1.2.3.4")); l.made1("1.2.3.4"); }
  assert.ok(!l.allowed("1.2.3.4"));
  assert.ok(l.allowed("5.6.7.8"));
});

test("settings: saved, cleaned, ADMIN never offered at sign-up", () => {
  const f = join(dir(), "settings.json");
  const d = { registration: "open" as const, signupRoles: SIGNUP_ROLES, publicMap3d: "login" as const };
  const s = new SettingsStore(f, d);
  assert.deepEqual(s.value, d);
  s.update({ registration: "closed", signupRoles: ["HACKER", "ADMIN", "HACKER"] as any, publicMap3d: "open" });
  const again = new SettingsStore(f, d);
  assert.deepEqual(again.value, { registration: "closed", signupRoles: ["HACKER"], publicMap3d: "open" });
  assert.equal(again.update({ registration: "maybe" as any }).registration, "closed");
});

test("log tail: last lines only", () => {
  const f = join(dir(), "x.log");
  writeFileSync(f, Array.from({ length: 500 }, (_, i) => "line " + i).join("\n") + "\n");
  const t = tail(f, 10);
  assert.equal(t.length, 10);
  assert.equal(t[9], "line 499");
  assert.match(tail(join(dir(), "none.log"))[0], /no file/);
});
