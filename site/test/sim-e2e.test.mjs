// The real firmware, driven like a person would: the test wallet sends the
// public BIP-39 test seed with PIN 1234, then the vector's PSBT; the review
// is read page by page and OK held; the signed PSBT must carry exactly the
// vector's signature (the same steps as tests/c/test_app.c).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { Simulator } from "../static/sim/core.mjs";
import { seedAudio, psbtAudio, SignedCollector } from "../static/sim/wallet.mjs";
import { SKIP, load } from "./wasm-helpers.mjs";
import { loadUrTones } from "./urtones-helpers.mjs";

const VECTORS = JSON.parse(readFileSync(new URL("../../vectors/psbt-v0.json", import.meta.url), "utf8")).vectors;
const OK = 3, DOWN = 4, RIGHT = 5, UP = 2;

// The whole path on the real firmware, for one network: the network set in
// the menu, the seed with PIN 1234, its fingerprint, the PSBT read page by
// page, OK held, and the signed PSBT from the echo.
async function signFlow({ net, vector, fingerprint }) {
  const V = VECTORS.find((v) => v.name === vector);
  const U = loadUrTones();
  const sim = new Simulator(await load());
  const signed = new SignedCollector();
  const text = () => sim.lines().join("\n");
  const run = (ms) => { for (let t = 0; t < ms; t += 250) for (const l of sim.advance(Math.min(250, ms - t)).echo) signed.push(l); };
  const tap = (cls) => { sim.press(cls); run(80); sim.release(); run(400); };
  const hold = (cls) => { sim.press(cls); run(1700); sim.release(); run(400); };
  const until = (re, ms) => { for (let t = 0; t < ms && !re.test(text()); t += 250) run(250); assert.match(text(), re); };

  run(500);
  tap(OK);                                   // past the welcome
  until(/Load seed/, 1000);
  if (net === "testnet") {
    tap(DOWN);                               // Network
    tap(RIGHT);                              // mainnet -> testnet
    assert.match(text(), /Network\s+\S\s*testnet/);
    tap(UP);                                 // back to Load seed
  }
  assert.match(text(), new RegExp(`DTMF-Signer\\s+${net}`));
  tap(OK);
  until(/Waiting for tones/, 1000);
  sim.queueAudio(seedAudio(U, V.entropy, "1234"));
  until(/PIN/, 30000);
  for (const ch of "1234") {
    for (let i = 0; i < 200 && String.fromCharCode(sim.m._dsw_picker_cell()) !== ch; i++) tap(i % 10 === 9 ? DOWN : RIGHT);
    assert.equal(String.fromCharCode(sim.m._dsw_picker_cell()), ch);
    tap(OK);
  }
  hold(OK);                                  // ends the PIN
  until(/Seed fingerprint/, 20000);
  assert.match(text(), new RegExp(fingerprint));
  tap(OK);
  until(/Passphrase\?/, 2000);
  tap(OK);                                   // none
  until(/Wallet fingerprint/, 20000);
  tap(OK);
  until(/Sign PSBT/, 2000);
  for (let i = 0; i < 8 && !/^>?\s*Sign PSBT/m.test(text()); i++) tap(UP);
  tap(OK);
  until(/by cable/, 2000);
  sim.queueAudio(psbtAudio(U, V.psbt));
  until(/Send to/, 240000);
  run(500);
  for (let i = 0; i < 20 && !/Hold OK: sign/.test(text()); i++) tap(DOWN);
  assert.match(text(), /Hold OK: sign/);
  tap(OK);                                   // arms the review
  hold(OK);
  until(/Signed/, 20000);
  run(2000);
  assert.equal(sim.qrKind(), "psbt");
  const r = signed.result;
  assert.ok(r, "no signed PSBT in the echo");
  const hex = Buffer.from(r.base64, "base64").toString("hex");
  for (const s of V.signatures) {
    assert.ok(hex.includes(s.pubkey), "pubkey");
    assert.ok(hex.includes(s.sig), "signature");
  }
}

test("a seed and a PSBT sent by the test wallet come back signed", { skip: SKIP }, () =>
  signFlow({ net: "mainnet", vector: "one input with change", fingerprint: "73c5da0a" }));

test("a testnet signer signs the testnet pair", { skip: SKIP }, () =>
  signFlow({ net: "testnet", vector: "testnet account m/84'/1'/0'", fingerprint: "8b218e81" }));
