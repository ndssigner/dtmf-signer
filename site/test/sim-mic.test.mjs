import { test } from "node:test";
import assert from "node:assert/strict";
import { resampleTo16k, startMic, levelDbfs, MicSwitch } from "../static/sim/mic.mjs";

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
    resume() { return Promise.resolve(); }
    createBiquadFilter() { return { frequency: {}, connect() {} }; }
    createMediaStreamSource() { return { connect() {} }; }
    close() { closed = true; return Promise.resolve(); }
  }
  class Node { constructor() { this.port = {}; } connect() {} }
  const mic = await startMic({ getUserMedia: async () => ({ getTracks: () => tracks }), AudioContext: Ctx, AudioWorkletNode: Node, onSamples() {} });
  mic.stop();
  assert.ok(tracks.every((t) => t.stopped));
  assert.ok(closed);
});

function fakeAudio(log) {
  class Ctx {
    constructor() { this.sampleRate = 48000; this.state = "suspended"; this.audioWorklet = { addModule: async () => {} }; this.destination = {}; log.push("ctx"); }
    resume() { this.state = "running"; log.push("resume"); return Promise.resolve(); }
    createMediaStreamSource() { return { connect: (n) => log.push("src->" + (n.kind ?? "node")) }; }
    createBiquadFilter() { const f = { kind: "biquad", type: "", frequency: { value: 0 }, connect: (n) => log.push("biquad->" + (n.kind ?? "node")) }; log.push("biquad"); return f; }
    close() { log.push("close"); return Promise.resolve(); }
  }
  class Node { constructor() { this.port = {}; } connect() {} disconnect() {} }
  return { Ctx, Node };
}
const stream = (tracks) => ({ getTracks: () => tracks });
test("the audio is resumed and low-passed before the resampler", async () => {
  const log = [], { Ctx, Node } = fakeAudio(log);
  const ctx = new Ctx();
  await startMic({ getUserMedia: async () => stream([]), audioContext: ctx, AudioWorkletNode: Node, onSamples() {} });
  assert.ok(log.includes("resume"));
  assert.ok(log.includes("src->biquad") && log.includes("biquad->node"), log.join(" "));
});
test("if the audio cannot start, the microphone is released", async () => {
  const t = { stopped: false, stop() { this.stopped = true; } };
  const bad = { audioWorklet: { addModule: async () => { throw new Error("x"); } }, resume: async () => {}, close: async () => {}, createMediaStreamSource() { return { connect() {} }; }, createBiquadFilter() { return { frequency: {}, connect() {} }; } };
  await assert.rejects(startMic({ getUserMedia: async () => stream([t]), audioContext: bad, AudioWorkletNode: class {}, onSamples() {} }));
  assert.ok(t.stopped);
});
test("switching the microphone while it starts: a double click starts it once", async () => {
  let starts = 0, stops = 0, release;
  const sw = new MicSwitch(() => { starts++; return new Promise((r) => { release = () => r({ stop() { stops++; } }); }); });
  const a = sw.toggle(), b = sw.toggle();
  release(); await a; await b;
  assert.equal(starts, 1);
});
test("cancelling while it starts stops it as soon as it is up", async () => {
  let stops = 0, release;
  const sw = new MicSwitch(() => new Promise((r) => { release = () => r({ stop() { stops++; } }); }));
  const a = sw.toggle();
  sw.cancel();
  release(); await a;
  assert.equal(stops, 1);
  assert.equal(sw.on, false);
});
test("toggle twice when on: stops", async () => {
  let stops = 0;
  const sw = new MicSwitch(async () => ({ stop() { stops++; } }));
  await sw.toggle();
  assert.equal(sw.on, true);
  await sw.toggle();
  assert.equal(stops, 1);
  assert.equal(sw.on, false);
});
