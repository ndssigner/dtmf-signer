import { test } from "node:test";
import assert from "node:assert/strict";
import { seedAudio, psbtAudio, SignedCollector } from "../static/sim/wallet.mjs";
import { Simulator } from "../static/sim/core.mjs";
import { loadUrTones } from "./urtones-helpers.mjs";

const U = loadUrTones();
const ZERO = "00000000000000000000000000000000";

test("seed audio is cable-paced 16 kHz Int16 with the UR frame's tones", () => {
  const frame = U.urToFrame(U.seedToUR(new Uint8Array(16), "1234"));
  const a = seedAudio(U, ZERO, "1234");
  assert.ok(a instanceof Int16Array);
  assert.equal(a.length, 2 * 6400 + frame.length * 960);
  assert.ok(Math.max(...a.subarray(6400, 7000)) > 1000);
});
test("PSBT audio plays every part, twice by default", () => {
  const psbt = new Uint8Array(372).fill(5);
  const { urs } = U.psbtToURs(psbt, 100, 0);
  const once = psbtAudio(U, Buffer.from(psbt).toString("base64"), 16000, 1);
  const twice = psbtAudio(U, Buffer.from(psbt).toString("base64"));
  assert.ok(urs.length > 1);
  assert.ok(twice.length > 1.9 * once.length);
});
test("the collector joins a signed PSBT and ignores an aborted one", () => {
  const c = new SignedCollector();
  for (const l of ["state ready", "signed-psbt-begin 1 3", "signed-psbt AQID", "signed-psbt-end 1"]) c.push(l);
  assert.deepEqual(c.result, { id: 1, base64: "AQID", bytes: 3 });
  const d = new SignedCollector();
  for (const l of ["signed-psbt-begin 2 3", "signed-psbt AQID", "signed-psbt-abort 2"]) d.push(l);
  assert.equal(d.result, null);
  const e = new SignedCollector();
  for (const l of ["signed-psbt-begin 3 9", "signed-psbt AQID", "signed-psbt-end 3"]) e.push(l);
  assert.equal(e.result, null, "wrong size");
});
test("sending twice just queues more audio", () => {
  const f = { HEAP16: new Int16Array(8192), _dsw_init() {} };
  const sim = new Simulator(f);
  const a = seedAudio(U, ZERO, "1234");
  sim.queueAudio(a);
  sim.queueAudio(a);
  assert.equal(sim.queued, 2 * a.length);
});
