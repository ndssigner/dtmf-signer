// The Centralita's simulator without a page: the telephone around the real
// voice firmware (build/wasm/dtmf-signer-voice.mjs). The line is one node:
// what the signer says, the keypad and the handset microphone, mixed.
import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { VoiceSimulator, keyTone, RATE } from "../static/sim/voice-core.mjs";

export const VOICE = fileURLToPath(new URL("../../build/wasm/dtmf-signer-voice.mjs", import.meta.url));
const SKIP = existsSync(VOICE) || process.env.SIM_REQUIRED === "1"
  ? false : "build/wasm/dtmf-signer-voice.mjs missing: run tools/build-wasm.sh";
const ES = readFileSync(new URL("../../app/voice/packs/es.bin", import.meta.url));

const load = async () => (await import(pathToFileURL(VOICE).href)).default();

// Runs ms of line in 50 ms frames; returns the echo and the earpiece.
function run(vs, ms) {
  const echo = [], ear = [];
  for (let t = 0; t < ms; t += 50) {
    const r = vs.advance(Math.min(50, ms - t));
    echo.push(...r.echo);
    ear.push(r.earpiece);
  }
  const all = new Int16Array(ear.reduce((n, e) => n + e.length, 0));
  let at = 0;
  for (const e of ear) { all.set(e, at); at += e.length; }
  return { echo, earpiece: all };
}
// Until the signer has said nothing for 800 ms (at most ms).
function runQuiet(vs, ms = 60000) {
  const echo = [];
  for (let t = 0; t < ms && vs.quietMs < 800; t += 50) echo.push(...run(vs, 50).echo);
  return echo;
}

test("a telephone key is its pair for 100 ms, each at -10 dBFS", () => {
  const x = keyTone("5", RATE);
  assert.equal(x.length, RATE / 10);
  const peak = Math.max(...x.map(Math.abs));
  const each = 32767 * 10 ** (-10 / 20);
  assert.ok(peak > 1.8 * each && peak <= 2 * each + 1, String(peak));
  assert.throws(() => keyTone("X", RATE));
});

test("keypad press reaches the signer", { skip: SKIP }, async () => {
  const vs = new VoiceSimulator(await load(), "en");
  vs.hook(true);
  const greet = runQuiet(vs);
  assert.ok(greet.includes("say greeting"), greet.join("|"));
  vs.pressKey("#");
  const { echo } = run(vs, 1500);
  assert.ok(echo.includes("state menu-empty"), echo.join("|"));
  assert.ok(vs.keys() > 0, "the LED counts the key");
});

test("own output is on the line", { skip: SKIP }, async () => {
  const vs = new VoiceSimulator(await load(), "en");
  vs.hook(true);
  let checked = 0;
  for (let t = 0; t < 3000; t += 16) {
    vs.advance(16);
    assert.deepEqual(vs.lastIn, vs.lastOut);
    if (vs.lastOut.some((x) => x !== 0)) checked++;
  }
  assert.ok(checked > 20, `${checked} blocks with voice`);
});

test("earpiece carries the voice", { skip: SKIP }, async () => {
  const vs = new VoiceSimulator(await load(), "en");
  assert.ok(run(vs, 500).earpiece.every((x) => x === 0), "on-hook: nothing in the earpiece");
  vs.hook(true);
  const { earpiece } = run(vs, 3000);
  assert.equal(earpiece.length, 3000 * RATE / 1000);
  assert.ok(earpiece.filter((x) => Math.abs(x) > 100).length >= RATE / 2, "half a second of voice");
});

test("a key goes on the line at full level and into the earpiece at -20 dB", { skip: SKIP }, async () => {
  const vs = new VoiceSimulator(await load(), "en");
  vs.hook(true);
  runQuiet(vs);
  vs.pressKey("5");
  let inPeak = 0, earPeak = 0;
  for (let t = 0; t < 96; t += 16) {
    const { earpiece } = vs.advance(16);
    inPeak = Math.max(inPeak, ...vs.lastIn.map((x, i) => Math.abs(x - vs.lastOut[i])));
    earPeak = Math.max(earPeak, ...earpiece.map((x, i) => Math.abs(x - vs.lastOut[i])));
  }
  const tone = Math.max(...keyTone("5", RATE).map(Math.abs));
  assert.ok(Math.abs(inPeak - tone) <= 1, `${inPeak} vs ${tone}`);
  assert.ok(Math.abs(earPeak - tone / 10) <= 2, `${earPeak} vs ${tone / 10}`);
});

test("on-hook the telephone puts nothing on the line", { skip: SKIP }, async () => {
  const vs = new VoiceSimulator(await load(), "en");
  vs.pressKey("1");
  vs.queueMic(keyTone("2", RATE));
  for (let t = 0; t < 400; t += 16) {
    vs.advance(16);
    assert.ok(vs.lastIn.every((x) => x === 0));
  }
});

test("the virtual clock moves at most 250 ms a frame", { skip: SKIP }, async () => {
  const vs = new VoiceSimulator(await load(), "en");
  assert.equal(vs.advance(5000).earpiece.length, 250 * RATE / 1000);
  assert.equal(vs.clock, 250);
});

test("the Spanish pack brings the language list; English alone has none", { skip: SKIP }, async () => {
  const es = new VoiceSimulator(await load(), "es", new Uint8Array(ES));
  es.hook(true);
  const withPack = run(es, 2000).echo;
  assert.ok(withPack.includes("state lang"), withPack.join("|"));
  assert.ok(withPack.includes("say lang_press"));
  const en = new VoiceSimulator(await load(), "en");
  en.hook(true);
  const alone = run(en, 2000).echo;
  assert.ok(!alone.includes("state lang"), alone.join("|"));
  assert.ok(alone.includes("say greeting"));
});

test("a power cut (reset) keeps the pack, and the handset where it is", { skip: SKIP }, async () => {
  const es = new VoiceSimulator(await load(), "es", new Uint8Array(ES));
  es.hook(true);
  run(es, 500);
  es.reset();
  assert.equal(es.offHook, true);
  assert.equal(es.clock, 0);
  assert.ok(run(es, 2000).echo.includes("state lang"));
});

test("the text alternative says what the signer says, never a PIN character", async () => {
  const { Transcript } = await import("../static/sim/voice-ui.mjs");
  const phrases = { en: { lang_press: "For English, press", greeting: "D T M F signer.", pin_ask: "Type your PIN, then hash." },
    es: { lang_press: "Para español, pulsa", greeting: "de te eme efe sáiner." } };
  const t = new Transcript(phrases, { "say.pin_char": "•", "say.tones": "♪" }, ["en", "es"]);
  for (const l of ["state lang", "say lang_press", "say c_1", "say lang_press", "say c_2"]) t.push(l);
  assert.equal(t.push("lang es"), null);
  assert.equal(t.push("state greet"), "For English, press 1 Para español, pulsa 2");
  t.push("say greeting");
  assert.equal(t.end(), "de te eme efe sáiner.");
  for (const l of ["say pin-char", "say pin-char", "ear next", "chars 73c5da0a", "num 60000", "tones 0123", "tones 4567"]) t.push(l);
  assert.equal(t.end(), "• • 73c5da0a 60000 ♪");
});
