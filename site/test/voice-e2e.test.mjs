// The Centralita end to end on the real voice firmware, driven like a
// person with a tone telephone: the public test seed 00…00 typed on the
// keypad, the vector's PSBT played by a wallet next to the handset (air
// pace), the review heard item by item, a hook flash, and the signed PSBT
// played into the earpiece, where the wallet decodes it with ur-tones'
// JavaScript Listener (never from the echo).
import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { VoiceSimulator, RATE } from "../static/sim/voice-core.mjs";
import { psbtAudio, EarDecoder } from "../static/sim/wallet.mjs";
import { loadUrTones } from "./urtones-helpers.mjs";

const VOICE = fileURLToPath(new URL("../../build/wasm/dtmf-signer-voice.mjs", import.meta.url));
const SKIP = existsSync(VOICE) || process.env.SIM_REQUIRED === "1"
  ? false : "build/wasm/dtmf-signer-voice.mjs missing: run tools/build-wasm.sh";
const VECTORS = JSON.parse(readFileSync(new URL("../../vectors/psbt-v0.json", import.meta.url), "utf8")).vectors;

test("a keypad seed, a PSBT by the handset, a hook flash: the earpiece carries the signature", { skip: SKIP, timeout: 900000 }, async () => {
  const V = VECTORS.find((v) => v.name === "one input with change");
  const U = loadUrTones();
  const vs = new VoiceSimulator(await (await import(pathToFileURL(VOICE).href)).default(), "en");
  const log = [];
  let ear = null;          // the wallet, listening at the earpiece
  const run = (ms) => {
    for (let t = 0; t < ms; t += 50) {
      const r = vs.advance(Math.min(50, ms - t));
      log.push(...r.echo);
      ear?.push(r.earpiece);
    }
  };
  const quiet = () => { for (let t = 0; t < 60000 && vs.quietMs < 800; t += 50) run(50); };
  const has = (line, from) => log.indexOf(line, from) >= 0;
  const until = (line, from, ms) => {
    for (let t = 0; t < ms && !has(line, from); t += 250) run(250);
    assert.ok(has(line, from), `no "${line}" in: ${log.slice(from).join("|")}`);
  };
  // A person typing: the dialler's 100 ms tone, then the next key.
  const type = (keys) => { for (const k of keys) { vs.pressKey(k); run(300); } };

  vs.hook(true);
  until("say greeting", 0, 3000);
  until("state menu-empty", 0, 2000);          // the menu follows the name
  quiet();
  type("1");                                   // load seed
  until("state seed-how", 0, 2000);
  quiet();
  type("2");                                   // by the keypad
  until("state seed-keypad", 0, 2000);
  run(500);
  type(U.seedToKeypad(new Uint8Array(16)));    // *0000…0003#, no PIN
  until("state keypad-pin", 0, 3000);
  quiet();
  type("#");                                   // no PIN
  until("fingerprint 73c5da0a", 0, 30000);
  quiet();
  type("#");                                   // the fingerprint is right
  until("state menu-ready", 0, 30000);
  quiet();

  let mark = log.length;
  type("1");                                   // sign
  until("state psbt-listen", mark, 2000);
  quiet();
  vs.queueMic(psbtAudio(U, V.psbt, RATE, 2, "air"));
  until("state review", mark, 900000);
  vs.clearMic();                               // the wallet stops once it is heard

  mark = log.length;
  for (let i = 0; i < 40 && !has("state confirm", mark); i++) { quiet(); type("#"); }
  quiet();
  assert.ok(has("say sign_hook", mark));

  mark = log.length;
  vs.hook(false); run(500); vs.hook(true);     // a hook flash: the confirmation
  until("state hold-near", mark, 20000);
  assert.ok(has("hook-flash", mark));
  quiet();

  ear = new EarDecoder(U, RATE);
  mark = log.length;
  type("#");                                   // play it
  until("say sent", mark, 900000);
  quiet();
  ear.flush();
  const got = ear.result;
  assert.ok(got, "the wallet decoded nothing from the earpiece");
  assert.equal(got.type, "crypto-psbt");
  const hex = Buffer.from(got.base64, "base64").toString("hex");
  for (const s of V.signatures) {
    assert.ok(hex.includes(s.pubkey), "pubkey");
    assert.ok(hex.includes(s.sig), "signature");
  }
});
