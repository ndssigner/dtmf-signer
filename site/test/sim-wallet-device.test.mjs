import { test } from "node:test";
import assert from "node:assert/strict";
import { SimWallet } from "../static/sim/sim-wallet.mjs";

function fakeSim(qr, title = "Signed") { return { qrShown: () => qr, lines: () => [title], queued: 0, queueAudio() {} }; }
const el = () => ({ out: { value: "", hidden: true }, status: { textContent: "" } });
const DATA = { pin: "1234", networks: {
  mainnet: { seed: "00000000000000000000000000000000", psbt: "AQID", signatures: [] },
  testnet: { seed: "6aa74bc073a67104570cca7a05839ca5", psbt: "BAUG", signatures: [] } } };

test("scanning with no QR on screen says so", () => {
  const w = new SimWallet({ sim: fakeSim(false), U: null, data: DATA, el: el() });
  assert.equal(w.scan(), "no-qr");
});
test("a QR but nothing signed yet: still reading", () => {
  const w = new SimWallet({ sim: fakeSim(true), U: null, data: DATA, el: el() });
  assert.equal(w.scan(), "waiting");
});
test("after the signed PSBT, scanning shows it", () => {
  const e = el();
  const w = new SimWallet({ sim: fakeSim(true), U: null, data: DATA, el: e });
  for (const l of ["signed-psbt-begin 1 3", "signed-psbt AQID", "signed-psbt-end 1"]) w.onEcho(l);
  assert.equal(w.scan(), "ok");
  assert.equal(e.out.value, "AQID");
  assert.equal(e.out.hidden, false);
});
test("a QR that is not a signed PSBT (the account) is not taken for one", () => {
  const e = el();
  const w = new SimWallet({ sim: fakeSim(true, "Account"), U: null, data: DATA, el: e });
  for (const l of ["signed-psbt-begin 1 3", "signed-psbt AQID", "signed-psbt-end 1"]) w.onEcho(l);
  assert.equal(w.scan(), "other-qr");
  assert.equal(e.out.hidden, true);
  assert.equal(w.scanning, false, "nothing to read: the camera stays off");
});

test("sendSeed uses the selected network's seed", () => {
  const seen = [];
  const U = { seedToUR: (e) => { seen.push(Buffer.from(e).toString("hex")); return "ur"; }, urToFrame: () => "f", render: () => new Float32Array(0) };
  const w = new SimWallet({ sim: fakeSim(false), U, data: DATA, el: el() });
  w.sendSeed();
  w.setNetwork("testnet");
  w.sendSeed();
  assert.deepEqual(seen, [DATA.networks.mainnet.seed, DATA.networks.testnet.seed]);
  assert.equal(w.pair(), DATA.networks.testnet);
});
test("unknown network never warns", () => {
  const w = new SimWallet({ sim: fakeSim(false, "Welcome"), U: null, data: DATA, el: el() });
  w.watch();
  assert.equal(w.signerNet, null);
  assert.equal(w.mismatch(), false);
});
test("a testnet signer and a mainnet wallet mismatch", () => {
  const w = new SimWallet({ sim: fakeSim(false, "DTMF-Signer   testnet"), U: null, data: DATA, el: el() });
  w.watch();
  assert.equal(w.mismatch(), true);
  w.setNetwork("testnet");
  assert.equal(w.mismatch(), false);
});
test("the signer's network is remembered on screens that do not show it", () => {
  let title = "DTMF-Signer   testnet";
  const sim = { qrShown: () => false, lines: () => [title] };
  const w = new SimWallet({ sim, U: null, data: DATA, el: el() });
  w.watch();
  title = "Waiting for tones";
  w.watch();
  assert.equal(w.signerNet, "testnet");
});
test("network change clears the check", () => {
  const w = new SimWallet({ sim: fakeSim(false), U: null, data: DATA, el: el() });
  w.last = { lines: ["x"] };
  w.setNetwork("testnet");
  assert.equal(w.last, null);
});
