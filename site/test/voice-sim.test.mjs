// The PEX's simulator without a page: the telephone around the real
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

test("speex vector decodes to the same PCM in wasm", { skip: SKIP }, async () => {
  // The firmware's decoder, in the simulator, against vectors/speex-v0.json:
  // the same SHA-256 that tests/c/test_speex.c checks on the host.
  const m = await load();
  assert.equal(m._dvw_speex_selftest(), 1);
});

test("keypad press reaches the signer", { skip: SKIP }, async () => {
  const vs = new VoiceSimulator(await load(), "en");
  vs.hook(true);
  const greet = runQuiet(vs);
  assert.ok(greet.includes("say greeting"), greet.join("|"));
  assert.ok(greet.includes("state menu-empty"), greet.join("|"));   // the menu follows the name
  vs.pressKey("1");
  const { echo } = run(vs, 1500);
  assert.ok(echo.includes("state seed-how"), echo.join("|"));
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
  const t = new Transcript(phrases, { "say.pin_char": "•", "say.tones": "♪ tones" }, ["en", "es"]);
  for (const l of ["state lang", "say lang_press", "say c_1", "say lang_press", "say c_2"]) t.push(l);
  assert.equal(t.push("lang es"), null);
  for (const l of ["say lang_press", "say c_1", "say lang_press", "say c_2"]) t.push(l);   // the list again, unasked
  assert.equal(t.push("state greet"), "For English, press 1 Para español, pulsa 2 For English, press 1 Para español, pulsa 2");
  t.push("say greeting");
  assert.equal(t.end(), "de te eme efe sáiner.");
  for (const l of ["say pin-char", "say pin-char", "ear next", "chars 73c5da0a", "num 60000", "tones 0123", "tones 4567"]) t.push(l);
  assert.equal(t.end(), "• • ♪ 73c5da0a 60000 ♪ tones");
});

test("the bubble shows a motif as a note, and the longer help as its text", async () => {
  const { Transcript } = await import("../static/sim/voice-ui.mjs");
  const S = { "say.pin_char": "•", "say.tones": "♪ tones" };
  const t = new Transcript({ en: { choose_option: "Choose an option.", choose_option_long: "Choose an option and press its key." } }, S, ["en"]);
  t.push("ear send");
  assert.equal(t.end(), "♪");
  for (const l of ["ear option", "say choose_option"]) t.push(l);
  assert.equal(t.end(), "♪ Choose an option.");
  t.push("say choose_option_long");
  assert.equal(t.end(), "Choose an option and press its key.");
});
test("a motif lights the voice cable, not the tones", () => {
  let t = true;
  t = tonesAfter(t, "ear receive");
  assert.equal(t, false);
  const a = VA({ speaking: true, tonesOut: t });
  assert.ok(a.on.has("voice") && !a.on.has("tones"));
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
  for (const k of ["1", "9"]) { vs.pressKey(k); echo.push(...run(vs, 6000).echo); }
  echo.push(...run(vs, 60000).echo);   // About spells the version, then each pack
  const at = echo.indexOf("chars es");
  assert.ok(at >= 0, echo.join("|"));
  assert.ok(echo.slice(at).includes(`chars ${want}`), echo.slice(at).join("|"));
});

// ---- who runs the wallet: this computer stands in for the handset with yours ----
import { voiceActivity, leaveVoiceMode, MIC_BACKLOG_MS, tonesAfter, placeBubble } from "../static/sim/voice-ui.mjs";
import { wireWho } from "../static/sim/ui.mjs";

const VA = (o) => voiceActivity({ who: "sim", offHook: true, micQueued: 0, keysQueued: 0, speaking: false, tonesOut: false,
  walletListening: false, micOn: false, micDbfs: -100, quietMs: Infinity, ...o });

test("your wallet: the microphone lights the air in only when on and above the threshold", () => {
  const base = { who: "yours" };
  assert.ok(!VA(base).on.has("air-in"));
  assert.ok(!VA({ ...base, micOn: true, micDbfs: -70 }).on.has("air-in"));
  const a = VA({ ...base, micOn: true, micDbfs: -30 });
  assert.ok(a.on.has("air-in"));
  assert.ok(a.on.has("line"));
  assert.equal(VA({ ...base, micOn: true }).status, "status.mic");
});
test("speaking words lights the cable to your ear, not the wallet's; data tones light the wallet's", () => {
  for (const who of ["sim", "yours"]) {
    const words = VA({ who, speaking: true });
    assert.ok(words.on.has("voice") && !words.on.has("tones"), who);
    assert.equal(words.status, "status.speaking");
    const tones = VA({ who, speaking: true, tonesOut: true });
    assert.ok(tones.on.has("tones") && !tones.on.has("voice"), who);
    assert.equal(tones.status, "status.tones");
    const quiet = VA({ who, tonesOut: true });
    assert.ok(!quiet.on.has("tones") && !quiet.on.has("voice"), `${who}: silence carries nothing`);
  }
});
test("the simulated wallet listening keeps its cable busy; yours cannot be seen listening", () => {
  assert.ok(VA({ walletListening: true }).on.has("tones"));
  assert.equal(VA({ walletListening: true }).status, "status.air_out");
  assert.ok(!VA({ who: "yours", walletListening: true }).on.has("tones"));
  assert.ok(!VA({ walletListening: true, offHook: false }).on.has("tones"), "on the hook nothing sounds");
});
test("the earpiece plays data tones from a tones echo until the next words", () => {
  let t = false;
  for (const [line, want] of [["state tones", false], ["tones 0123", true], ["state tones", true], ["tones 4567", true],
    ["say again_hint", false], ["num 3", false], ["tones 89", true], ["chars ab", false], ["tones 1", true], ["ear ok", false]]) {
    t = tonesAfter(t, line);
    assert.equal(t, want, line);
  }
});
test("leaving who runs the wallet stops the microphone, empties the handset microphone, the dialling and the listening", () => {
  let stopped = 0, unheard = 0;
  const vs = { cleared: 0, dialStop: 0, clearMic() { this.cleared++; }, stopDialling() { this.dialStop++; } };
  leaveVoiceMode({ vs, mic: { stop() { stopped++; } }, wallet: { stopListening() { unheard++; } } });
  assert.deepEqual([vs.cleared, vs.dialStop, stopped, unheard], [1, 1, 1, 1]);
  leaveVoiceMode({ vs, mic: null });
  assert.equal(vs.cleared, 2);
});
function fakeButton(who, pressed) {
  const b = new EventTarget();
  const attrs = { "aria-pressed": String(pressed) };
  return Object.assign(b, { dataset: { who }, getAttribute: (k) => attrs[k], setAttribute: (k, v) => { attrs[k] = v; } });
}
test("switching who runs the wallet stops everything (the PEX: mic, handset-mic queue, dialling, listening)", () => {
  const n = { clearMic: 0, stopDialling: 0, mic: 0, listen: 0 };
  const vs = { clearMic() { n.clearMic++; }, stopDialling() { n.stopDialling++; } };
  const mic = { stop() { n.mic++; } }, wallet = { stopListening() { n.listen++; } };
  const buttons = [fakeButton("sim", true), fakeButton("yours", false)];
  const panels = [{ dataset: { panel: "sim" }, hidden: false }, { dataset: { panel: "yours" }, hidden: true }];
  const w = wireWho({ buttons, panels, leave: () => leaveVoiceMode({ vs, mic, wallet }) });
  buttons[1].dispatchEvent(new Event("click"));
  assert.deepEqual(n, { clearMic: 1, stopDialling: 1, mic: 1, listen: 1 });
  assert.equal(w.who, "yours");
  buttons[0].dispatchEvent(new Event("click"));
  assert.deepEqual(n, { clearMic: 2, stopDialling: 2, mic: 2, listen: 2 });
  const ui = readFileSync(new URL("../static/sim/voice-ui.mjs", import.meta.url), "utf8");
  assert.match(ui, /wireWho\(\{[\s\S]*?leaveVoiceMode\(\{ vs, mic: [^}]*\}, wallet \}\)/, "the page wires the selector to leaveVoiceMode");
});
test("the microphone never runs the line far behind: the backlog cap is a few seconds", () => {
  assert.ok(MIC_BACKLOG_MS >= 1000 && MIC_BACKLOG_MS <= 3000);
});

test("your wallet: the speakers carry the earpiece only (the microphone is never played back: it would howl)", async () => {
  const { speakerMix } = await import("../static/sim/voice-ui.mjs");
  const ear = Int16Array.from([100, -100]), room = Int16Array.from([50, 50]);
  assert.deepEqual([...speakerMix("sim", ear, room)], [150, -50]);
  assert.deepEqual([...speakerMix("yours", ear, room)], [100, -100]);
});
test("your wallet: ⏩ (simulated wallet only) never mutes the speakers", async () => {
  const { speakersOn } = await import("../static/sim/voice-ui.mjs");
  assert.equal(speakersOn({ who: "yours", sound: true, fast: true }), true);
  assert.equal(speakersOn({ who: "sim", sound: true, fast: true }), false);
  assert.equal(speakersOn({ who: "yours", sound: false, fast: false }), false);
});
test("your wallet: while the signer speaks (and 1 s after), the microphone hearing its echo does not light the air in", () => {
  const base = { who: "yours", micOn: true, micDbfs: -30 };
  assert.ok(!VA({ ...base, speaking: true, quietMs: 0 }).on.has("air-in"));
  assert.ok(!VA({ ...base, speaking: false, quietMs: 500 }).on.has("air-in"));
  assert.ok(VA({ ...base, speaking: false, quietMs: 1200 }).on.has("air-in"));
});

// ---- the earpiece's transcript: a speech bubble above the telephone ----
const R = (left, top, width, height) => ({ left, top, width, height, right: left + width, bottom: top + height });
test("the bubble sits above the telephone, inside the bench, its tail over the earpiece (wide)", () => {
  const bench = R(100, 50, 1100, 700), phone = R(400, 230, 260, 380), ear = R(650, 260, 20, 80);
  const bubble = R(0, 0, 300, 90);
  const { left, top, tail } = placeBubble(bubble, ear, phone, bench);
  assert.ok(left >= 0 && left + 300 <= 1100, String(left));
  assert.ok(top >= 0 && top + 90 <= phone.top - bench.top, `${top}: above the telephone`);
  assert.equal(left + tail, ear.left + ear.width / 2 - bench.left, "the tail over the earpiece");
  assert.ok(tail >= 12 && tail <= 300 - 12);
});
test("the bubble still fits a narrow bench, the tail still over the earpiece", () => {
  const bench = R(16, 0, 343, 2400), phone = R(16, 700, 343, 330), ear = R(150, 1018, 70, 24);
  const bubble = R(0, 0, 327, 110);
  const { left, top, tail } = placeBubble(bubble, ear, phone, bench);
  assert.ok(left >= 0 && left + 327 <= 343, String(left));
  assert.ok(top + 110 <= phone.top - bench.top);
  assert.equal(left + tail, 185 - 16);
  // An earpiece near the bench's edge: the bubble stays in, the tail moves along it (never off it).
  const edge = placeBubble(bubble, R(330, 1018, 40, 24), phone, bench);
  assert.ok(edge.left >= 0 && edge.left + 327 <= 343);
  assert.ok(edge.tail >= 12 && edge.tail <= 327 - 12);
  // Elements work as well as rects.
  const el = (r) => ({ getBoundingClientRect: () => r });
  assert.deepEqual(placeBubble(el(bubble), el(ear), el(phone), el(bench)), placeBubble(bubble, ear, phone, bench));
});
test("on a narrow bench the bubble moves aside so the telephone cable stays visible", () => {
  // As at 375 px: the RJ11 port on the telephone's top edge at the left, its
  // cable coming down from the signer; the earpiece lower right.
  const bench = R(16, 541, 343, 1523), phone = R(30, 1068, 314, 347);
  const line = R(70, 1060, 40, 18), ear = R(205, 1405, 70, 20);
  const bubble = R(0, 0, 201, 69);
  const { left, tail } = placeBubble(bubble, ear, phone, bench, 8, [line]);
  assert.ok(left + bench.left >= line.right + 8, `${left + bench.left}: clear of the cable at ${line.right}`);
  assert.ok(left + 201 <= 343, "inside the bench");
  assert.ok(tail >= 12 && tail <= 201 - 12);
  // Wide: the RJ11 on the telephone's side edge, its cable not above it: nothing moves.
  const wb = R(100, 50, 1100, 700), wp = R(400, 230, 260, 380), we = R(650, 260, 20, 80);
  const side = R(390, 400, 20, 40), wbub = R(0, 0, 300, 90);
  assert.deepEqual(placeBubble(wbub, we, wp, wb, 8, [side]), placeBubble(wbub, we, wp, wb));
});
test("stacked, the bubble sits flush with the telephone's right edge, clear of its cable", () => {
  const bench = R(16, 541, 343, 1523), phone = R(30, 1068, 314, 347);
  const line = R(57, 1056, 43, 24), ear = R(162, 1405, 82, 20);
  const bubble = R(0, 0, 200, 69);
  const { left, tail } = placeBubble(bubble, ear, phone, bench, 8, [line], "right");
  assert.equal(left + 200, phone.left + phone.width - bench.left, "flush right");
  assert.ok(left + bench.left >= line.left + line.width, "the cable shows");
  assert.ok(tail >= 12 && tail <= 200 - 12);
});
