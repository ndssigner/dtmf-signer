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

// Which 16 ms blocks carry a key on the line (the line less the signer's own
// output), for ms.
function keyBlocks(vs, ms) {
  const on = [];
  for (let t = 0; t < ms; t += 16) {
    vs.advance(16);
    on.push(vs.lastIn.some((x, i) => Math.abs(x - vs.lastOut[i]) > 100));
  }
  return on;
}

test("Type code dials as a person's fingers: each key 100 ms, the next 300 ms after", { skip: SKIP }, async () => {
  const vs = new VoiceSimulator(await load(), "en");
  vs.dial("*12#");
  assert.equal(vs.keysQueued, 0, "on-hook: nothing dialled");
  vs.hook(true);
  runQuiet(vs);
  vs.dial("*12#");
  const on = keyBlocks(vs, 1400);
  const starts = [], lengths = [];
  on.forEach((b, i) => {
    if (b && !on[i - 1]) { starts.push(i * 16); lengths.push(0); }
    if (b) lengths[lengths.length - 1] += 16;
  });
  assert.equal(starts.length, 4, on.map(Number).join(""));
  for (let i = 1; i < 4; i++) assert.ok(Math.abs(starts[i] - starts[i - 1] - 300) <= 16, String(starts));
  for (const l of lengths) assert.ok(l >= 96 && l <= 112, String(lengths));
  assert.ok(vs.keys() >= 1, "the keypad's keys reached the signer");
});

test("Type code stops at a second click, and when the handset goes down", { skip: SKIP }, async () => {
  const vs = new VoiceSimulator(await load(), "en");
  vs.hook(true);
  vs.dial("0123456789");
  vs.advance(200);
  vs.stopDialling();
  assert.equal(vs.keysQueued, 0);
  assert.ok(keyBlocks(vs, 400).every((b) => !b));
  vs.dial("0123456789");
  vs.hook(false);
  assert.equal(vs.keysQueued, 0);
});

test("About speaks each pack's fingerprint: 16 hex characters of its SHA-256", { skip: SKIP }, async () => {
  const { createHash } = await import("node:crypto");
  const want = createHash("sha256").update(ES).digest("hex").slice(0, 16);
  const vs = new VoiceSimulator(await load(), "es", new Uint8Array(ES));
  vs.hook(true);
  run(vs, 300);
  const echo = [];
  for (const k of ["1", "#", "9"]) { vs.pressKey(k); echo.push(...run(vs, 6000).echo); }
  echo.push(...run(vs, 60000).echo);   // About spells the version, then each pack
  const at = echo.indexOf("chars es");
  assert.ok(at >= 0, echo.join("|"));
  assert.ok(echo.slice(at).includes(`chars ${want}`), echo.slice(at).join("|"));
});

// ---- Your wallet: this computer stands in for the handset ----
import { voiceActivity, leaveVoiceMode, MIC_BACKLOG_MS } from "../static/sim/voice-ui.mjs";

test("your wallet: the microphone lights the air in only when on and above the threshold", () => {
  const base = { mode: "your-wallet", offHook: true, micQueued: 0, keysQueued: 0, speaking: false, walletListening: false, micOn: false, micDbfs: -100 };
  assert.ok(!voiceActivity(base).on.has("air-in"));
  assert.ok(!voiceActivity({ ...base, micOn: true, micDbfs: -70 }).on.has("air-in"));
  const a = voiceActivity({ ...base, micOn: true, micDbfs: -30 });
  assert.ok(a.on.has("air-in"));
  assert.ok(a.on.has("line"));
  assert.equal(voiceActivity({ ...base, micOn: true }).status, "status.mic");
  assert.ok(voiceActivity({ ...base, speaking: true }).on.has("air-out"), "the speakers carry the earpiece to your device");
});
test("simulated wallet: unchanged — the air out lights only while the wallet listens", () => {
  const base = { mode: "sim-wallet", offHook: true, micQueued: 0, keysQueued: 0, speaking: true, walletListening: false };
  assert.ok(!voiceActivity(base).on.has("air-out"));
  assert.ok(voiceActivity({ ...base, walletListening: true }).on.has("air-out"));
});
test("leaving a mode stops the microphone, empties the handset microphone and the dialling", () => {
  let stopped = 0;
  const vs = { cleared: 0, dialStop: 0, clearMic() { this.cleared++; }, stopDialling() { this.dialStop++; } };
  leaveVoiceMode({ vs, mic: { stop() { stopped++; } } });
  assert.equal(vs.cleared, 1);
  assert.equal(vs.dialStop, 1);
  assert.equal(stopped, 1);
  leaveVoiceMode({ vs, mic: null });
  assert.equal(vs.cleared, 2);
});
test("the microphone never runs the line far behind: the backlog cap is a few seconds", () => {
  assert.ok(MIC_BACKLOG_MS >= 1000 && MIC_BACKLOG_MS <= 3000);
});

test("your wallet: the speakers carry the earpiece only (the microphone is never played back: it would howl)", async () => {
  const { speakerMix } = await import("../static/sim/voice-ui.mjs");
  const ear = Int16Array.from([100, -100]), room = Int16Array.from([50, 50]);
  assert.deepEqual([...speakerMix("sim-wallet", ear, room)], [150, -50]);
  assert.deepEqual([...speakerMix("your-wallet", ear, room)], [100, -100]);
});
test("your wallet: ⏩ (simulated wallet only) never mutes the speakers", async () => {
  const { speakersOn } = await import("../static/sim/voice-ui.mjs");
  assert.equal(speakersOn({ mode: "your-wallet", sound: true, fast: true }), true);
  assert.equal(speakersOn({ mode: "sim-wallet", sound: true, fast: true }), false);
  assert.equal(speakersOn({ mode: "your-wallet", sound: false, fast: false }), false);
});
