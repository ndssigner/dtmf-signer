import { test } from "node:test";
import assert from "node:assert/strict";
import { Simulator } from "../static/sim/core.mjs";
import { SKIP, load } from "./wasm-helpers.mjs";

// A stand-in for the module: records what the simulator asks of it.
function fake({ echo = [] } = {}) {
  const heap = new ArrayBuffer(1 << 16);
  const f = {
    HEAP16: new Int16Array(heap), HEAPU8: new Uint8Array(heap),
    advanced: [], fed: [], inits: 0, reboot: 0, echo: [...echo], cur: "",
    UTF8ToString: (p) => (p === 1 ? f.cur : ""),
    _dsw_init() { f.inits++; f.reboot = 0; },
    _dsw_audio_buf: () => 1024,                  // byte offset into the heap
    _dsw_audio(n) { f.fed.push([...f.HEAP16.subarray(512, 512 + n)]); },
    _dsw_advance(ms, cls) { f.advanced.push([ms, cls]); },
    _dsw_take_dirty: () => 0, _dsw_draw: () => 4096, _dsw_contrast: () => 128,
    _dsw_rows: () => 0, _dsw_line: () => 0, _dsw_keys: () => 0,
    _dsw_take_echo() { if (!f.echo.length) return 0; f.cur = f.echo.shift(); return f.cur.length; },
    _dsw_echo: () => 1,
    _dsw_wants_reboot: () => f.reboot,
  };
  return f;
}

test("advance is capped at 250 ms per call", () => {
  const f = fake(), sim = new Simulator(f);
  sim.advance(5000);
  assert.equal(f.advanced.reduce((s, [ms]) => s + ms, 0), 250);
});
test("audio goes out at 16 samples per ms, silence when the queue is empty", () => {
  const f = fake(), sim = new Simulator(f);
  sim.queueAudio(Int16Array.from({ length: 300 }, () => 7));
  sim.advance(32);
  assert.deepEqual(f.fed.map((b) => b.length), [256, 256]);
  assert.ok(f.fed[0].every((v) => v === 7));
  assert.equal(f.fed[1].filter((v) => v === 7).length, 44);
  assert.equal(sim.queued, 0);
});
test("the held button reaches the module", () => {
  const f = fake(), sim = new Simulator(f);
  sim.press(3);
  assert.equal(sim.cls, 3);
  sim.advance(64);                       // held past the minimum press
  sim.release();
  sim.advance(16);
  assert.deepEqual(f.advanced.map(([, c]) => c), [3, 3, 3, 3, 0]);
});
test("echo lines are collected", () => {
  const sim = new Simulator(fake({ echo: ["state home", "fingerprint 73c5da0a"] }));
  assert.deepEqual(sim.advance(16).echo, ["state home", "fingerprint 73c5da0a"]);
});
test("a reboot request restarts it clean", () => {
  const f = fake(), sim = new Simulator(f);
  sim.queueAudio(new Int16Array(1000));
  f.reboot = 1;
  const r = sim.advance(16);
  assert.ok(r.echo.includes("reboot"));
  assert.equal(f.inits, 2);
  assert.equal(sim.queued, 0);
});
test("a wipe (LEFT held 3 s) restarts the real firmware", { skip: SKIP }, async () => {
  const sim = new Simulator(await load());
  sim.advance(500);
  sim.press(1);
  let echo = [];
  for (let i = 0; i < 14; i++) echo = echo.concat(sim.advance(250).echo);
  sim.release();
  for (let i = 0; i < 4; i++) sim.advance(250);
  assert.ok(echo.includes("reboot"), echo.join("|"));
  assert.match(sim.lines().join("\n"), /DTMF-Signer/);
});
test("fractions of a millisecond add up across calls", () => {
  const f = fake(), sim = new Simulator(f);
  sim.advance(16.5);
  sim.advance(16.5);
  assert.equal(f.advanced.reduce((s, [ms]) => s + ms, 0), 33);
  assert.ok(f.advanced.every(([ms]) => Number.isInteger(ms)));
});
test("a click shorter than the debounce still counts: release waits 60 ms of held time", () => {
  const f = fake(), sim = new Simulator(f);
  sim.press(3);
  sim.release();                         // a click: down and up at once
  sim.advance(16);
  sim.advance(16);
  sim.advance(16);
  sim.advance(16);
  sim.advance(16);
  const held = f.advanced.filter(([, c]) => c === 3).reduce((s, [ms]) => s + ms, 0);
  assert.ok(held >= 60 && held <= 64, String(held));
  assert.equal(f.advanced.at(-1)[1], 0);
  assert.equal(sim.cls, 0);
});
test("quick taps in a row stay separate presses, with a gap between them", () => {
  const f = fake(), sim = new Simulator(f);
  sim.press(5); sim.release();
  sim.press(5); sim.release();           // a second tap before the first was let go
  for (let i = 0; i < 20; i++) sim.advance(16);
  const runs = [];                       // [class, ms] runs as the firmware saw them
  for (const [ms, c] of f.advanced) {
    if (runs.length && runs.at(-1)[0] === c) runs.at(-1)[1] += ms; else runs.push([c, ms]);
  }
  const taps = runs.filter(([c]) => c === 5);
  assert.equal(taps.length, 2, JSON.stringify(runs));
  assert.ok(taps.every(([, ms]) => ms >= 60));
  const gap = runs[runs.findIndex(([c]) => c === 5) + 1];
  assert.ok(gap[0] === 0 && gap[1] >= 40, JSON.stringify(runs));
});
test("overlapping presses: releasing both leaves nothing held", () => {
  const f = fake(), sim = new Simulator(f);
  sim.press(3); sim.advance(100);        // OK held
  sim.press(2);                          // UP before OK is let go (rollover, two fingers)
  sim.release(3); sim.release(2);
  for (let i = 0; i < 20; i++) sim.advance(16);
  assert.equal(sim.cls, 0);
  assert.equal(f.advanced.at(-1)[1], 0);
  const okMs = f.advanced.filter(([, c]) => c === 3).reduce((s, [ms]) => s + ms, 0);
  assert.ok(okMs < 1500, "OK became a long press: " + okMs);
});
test("releasing another button than the held one does not release it", () => {
  const f = fake(), sim = new Simulator(f);
  sim.press(3); sim.advance(100);
  sim.release(5);
  sim.advance(16);
  assert.equal(sim.cls, 3);
});
test("releaseAll lets go of everything at once, pending presses too", () => {
  const f = fake(), sim = new Simulator(f);
  sim.press(3); sim.press(2);
  sim.releaseAll();
  for (let i = 0; i < 10; i++) sim.advance(16);
  assert.equal(sim.cls, 0);
  assert.ok(f.advanced.every(([, c]) => c !== 2));
});
test("idle: no button held or waiting", () => {
  const sim = new Simulator(fake());
  assert.equal(sim.idle, true);
  sim.press(3);
  assert.equal(sim.idle, false);
});
