import { test } from "node:test";
import assert from "node:assert/strict";
import { joaat, loadVehicles, vehicleName, weaponName } from "../src/names.ts";
import { Zones } from "../src/zones.ts";

test("joaat matches GTA's hashes", () => {
  assert.equal(joaat("adder"), 0xb779a091);
  assert.equal(joaat("weapon_pistol"), 0x1b06d571);
  assert.equal(joaat("ADDER"), joaat("adder"));   // case-insensitive like the game
});

test("vehicle and weapon names", () => {
  assert.ok(loadVehicles(new URL("../data/vehicles.txt", import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, "$1")) > 800);
  assert.deepEqual(vehicleName(joaat("sultan")), { model: "sultan", name: "Sultan", cls: "Sports" });
  assert.equal(vehicleName(0x12345678).name, "0x12345678");
  assert.equal(weaponName(joaat("weapon_carbinerifle_mk2")), "Carbinerifle Mk II");
  assert.equal(weaponName(0), "");
});

test("the smallest zone box that holds the point wins", () => {
  const z = new Zones();
  z.load([
    [-1000, -1000, 0, 1000, 1000, 1250, "DOWNT"],
    [100, 100, 0, 200, 200, 1250, "LEGSQU"],
    [100, 100, 500, 200, 200, 600, "OBSERV"],   // high up only
    [-9000, -9000, -100, 9000, 9000, 1000, "OCEANA"],   // dropped: the open sea is the fallback
  ]);
  assert.equal(z.codeAt(150, 150, 30), "LEGSQU");
  assert.equal(z.codeAt(150, 150, 550), "OBSERV");
  assert.equal(z.codeAt(0, 0, 30), "DOWNT");
  assert.equal(z.nameAt(0, 0, 30).name, "Downtown");
  assert.equal(z.codeAt(5000, 5000, 0), "OCEANA");
});
