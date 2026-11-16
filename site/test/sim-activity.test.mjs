import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { activeChannels, statusKey } from "../static/sim/activity.mjs";
import { leaveMode } from "../static/sim/ui.mjs";

const A = (o) => activeChannels({ mode: "sim-wallet", queued: 0, listening: false, qrShown: false, scanning: false, micOn: false, micDbfs: -100, ...o });

test("at rest only the power is on", () => {
  assert.deepEqual([...A({})], ["power"]);
  assert.deepEqual([...A({ mode: "your-wallet" })], ["power"]);
});
test("simulated phone: tones in the line light the cable", () => {
  assert.ok(A({ queued: 100 }).has("line-in"));
});
test("your smartphone: the cable lights only with the microphone on, the signer listening and sound above the threshold", () => {
  assert.ok(!A({ mode: "your-wallet", queued: 100 }).has("line-in"));
  assert.ok(!A({ mode: "your-wallet", micOn: true, listening: true, micDbfs: -70 }).has("line-in"));
  assert.ok(A({ mode: "your-wallet", micOn: true, listening: true, micDbfs: -30 }).has("line-in"));
  assert.ok(A({ mode: "your-wallet", micOn: true }).has("mic"));
  assert.ok(!A({ micOn: true }).has("mic"));
});
test("listening marks the IN port", () => {
  assert.ok(A({ listening: true }).has("listening"));
});
test("a QR on screen emits; the simulated camera reads only while scanning", () => {
  const a = A({ qrShown: true });
  assert.ok(a.has("qr") && !a.has("camera"));
  assert.ok(A({ qrShown: true, scanning: true }).has("camera"));
  assert.ok(!A({ scanning: true }).has("camera"), "no QR, nothing to read");
});
test("your smartphone's camera is not animated: the page cannot know you are scanning", () => {
  const a = A({ mode: "your-wallet", qrShown: true, scanning: true });
  assert.ok(!a.has("camera"));
  assert.equal(statusKey(a), "status.qr");
});
test("the status line names the most telling activity", () => {
  assert.equal(statusKey(A({})), "status.idle");
  assert.equal(statusKey(A({ queued: 10, listening: true })), "status.line_in");
  assert.equal(statusKey(A({ listening: true })), "status.listening");
  assert.equal(statusKey(A({ qrShown: true, scanning: true })), "status.camera");
  assert.equal(statusKey(A({ qrShown: true })), "status.qr");
});
test("leaving a mode empties the line and stops the microphone, without restarting the signer", () => {
  let cleared = 0, resets = 0, stopped = 0;
  const sim = { clearAudio() { cleared++; }, reset() { resets++; } };
  const mic = { stop() { stopped++; } };
  assert.equal(leaveMode({ sim, mic }), null);
  assert.deepEqual([cleared, stopped, resets], [1, 1, 0]);
  assert.equal(leaveMode({ sim, mic: null }), null);
});
test("leaving a mode also silences tones already sent to the speakers", () => {
  let closed = 0;
  leaveMode({ sim: { clearAudio() {} }, mic: null, speaker: { close() { closed++; } } });
  assert.equal(closed, 1);
});
test("with reduced motion nothing moves", () => {
  const css = readFileSync(new URL("../static/style.css", import.meta.url), "utf8");
  const m = /@media \(prefers-reduced-motion: reduce\)\s*\{([\s\S]*?)\n\}/.exec(css);
  assert.ok(m, "no reduced-motion block");
  assert.match(m[1], /\.link[^{]*\{[^}]*animation:\s*none/);
  assert.match(m[1], /\.wave|::before|::after/);
});
test("links meet ports at the label's side facing the other port", async () => {
  const { anchor } = await import("../static/sim/ui.mjs");
  const box = { left: 0, top: 0 };
  const a = { left: 100, right: 140, top: 50, bottom: 70, width: 40, height: 20 };
  const b = { left: 220, right: 260, top: 50, bottom: 70, width: 40, height: 20 };
  assert.deepEqual(anchor(a, b, "h", box), { x: 140, y: 60 });
  assert.deepEqual(anchor(b, a, "h", box), { x: 220, y: 60 });
  const c = { left: 100, right: 140, top: 200, bottom: 220, width: 40, height: 20 };
  assert.deepEqual(anchor(a, c, "v", box), { x: 120, y: 70 });
});
test("the power link never moves", () => {
  const css = readFileSync(new URL("../static/style.css", import.meta.url), "utf8");
  for (const m of css.matchAll(/([^{}]*)\{[^}]*animation:\s*flow/g)) assert.match(m[1], /:not\(\.type-power\)/, m[1]);
});
