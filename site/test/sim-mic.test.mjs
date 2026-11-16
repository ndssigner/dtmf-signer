import { test } from "node:test";
import assert from "node:assert/strict";
import { resampleTo16k, startMic, levelDbfs } from "../static/sim/mic.mjs";

const tone = (n, rate, hz) => Float32Array.from({ length: n }, (_, i) => 0.5 * Math.sin(2 * Math.PI * hz * i / rate));
const crossings = (x) => { let c = 0; for (let i = 1; i < x.length; i++) if ((x[i - 1] < 0) !== (x[i] < 0)) c++; return c; };

test("48 kHz to 16 kHz keeps a 1 kHz tone", () => {
  const out = resampleTo16k(tone(4800, 48000, 1000), 48000, { pos: 0, last: 0 });
  assert.ok(Math.abs(out.length - 1600) <= 1, String(out.length));
  assert.ok(Math.abs(crossings(out) - 200) <= 2, String(crossings(out)));
  assert.ok(out instanceof Int16Array);
});
test("blocks join without a gap or a jump", () => {
  const x = tone(960, 44100, 770);
  const whole = resampleTo16k(x, 44100, { pos: 0, last: 0 });
  const st = { pos: 0, last: 0 };
  const parts = [...resampleTo16k(x.subarray(0, 480), 44100, st), ...resampleTo16k(x.subarray(480), 44100, st)];
  assert.ok(Math.abs(parts.length - whole.length) <= 1);
  for (let i = 0; i < Math.min(parts.length, whole.length); i++) assert.ok(Math.abs(parts[i] - whole[i]) <= 2, String(i));
});
test("level of silence and of a loud tone", () => {
  assert.ok(levelDbfs(new Int16Array(160)) <= -80);
  assert.ok(levelDbfs(Int16Array.from({ length: 160 }, (_, i) => (i % 2 ? 16000 : -16000))) > -10);
});
test("a denied microphone rejects with mic:denied", async () => {
  await assert.rejects(startMic({ getUserMedia: () => Promise.reject(new Error("NotAllowedError")), AudioContext: class {}, onSamples() {} }), /mic:denied/);
});
test("no getUserMedia rejects with mic:unsupported", async () => {
  await assert.rejects(startMic({ getUserMedia: undefined, AudioContext: class {}, onSamples() {} }), /mic:unsupported/);
});
test("stop() stops every track and closes the audio", async () => {
  const tracks = [{ stopped: false, stop() { this.stopped = true; } }, { stopped: false, stop() { this.stopped = true; } }];
  let closed = false;
  class Ctx {
    constructor() { this.sampleRate = 48000; this.audioWorklet = { addModule: async () => {} }; }
    createMediaStreamSource() { return { connect() {} }; }
    close() { closed = true; return Promise.resolve(); }
  }
  class Node { constructor() { this.port = {}; } connect() {} }
  const mic = await startMic({ getUserMedia: async () => ({ getTracks: () => tracks }), AudioContext: Ctx, AudioWorkletNode: Node, onSamples() {} });
  mic.stop();
  assert.ok(tracks.every((t) => t.stopped));
  assert.ok(closed);
});
