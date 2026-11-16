import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { SimWallet } from "../static/sim/sim-wallet.mjs";
import { loadUrTonesCrypto } from "./urtones-helpers.mjs";

const { sha256 } = loadUrTonesCrypto();

function fakeSim(qr, title = "Signed") { return { qrShown: () => qr, lines: () => [title], queued: 0, queueAudio() {} }; }
const el = () => ({ out: { value: "", hidden: true }, status: { textContent: "" } });
const DATA = { pin: "1234", networks: {
  mainnet: { seed: "00000000000000000000000000000000", psbt: "AQID", signatures: [] },
  testnet: { seed: "6aa74bc073a67104570cca7a05839ca5", psbt: "BAUG", signatures: [] } } };

const V = JSON.parse(readFileSync(new URL("../../vectors/psbt-v0.json", import.meta.url), "utf8")).vectors;
const V1 = V.find((v) => v.name === "one input with change");
const S = { "wallet.qr_repo": "🔗 {url}", "wallet.qr_account": "👛 {fp} · {path}", "wallet.qr_xpub": "{xpub}",
  "wallet.no_broadcast": "📡 not sent", "wallet.out": "→ {addr} · {sats} sat", "wallet.change": "↩ {addr} · {sats} sat",
  "wallet.fee": "⛽ {sats} sat", "wallet.sig_ok": "✅ {key}", "wallet.sig_bad": "❌", "wallet.bytes": "{bytes} B" };
const DATA2 = { ...DATA, repo: "https://github.com/ndssigner/dtmf-signer",
  networks: { ...DATA.networks, mainnet: { ...DATA.networks.mainnet, signatures: V1.signatures } } };
const kindSim = (kind) => ({ qrKind: () => kind, qrShown: () => kind !== "none", lines: () => ["x"] });
const mk = (kind, e = el()) => new SimWallet({ sim: kindSim(kind), U: null, sha256, S, data: DATA2, el: e });

test("scanning with no QR on screen says so", () => {
  const w = mk("none");
  assert.equal(w.scan(), "no-qr");
  assert.equal(w.scanning, false);
});
test("the About QR is the repository", () => {
  const w = mk("repo");
  assert.equal(w.scan(), "repo");
  assert.deepEqual(w.last.lines, ["🔗 https://github.com/ndssigner/dtmf-signer"]);
});
test("the account QR is the wallet: fingerprint, path and xpub", () => {
  const w = mk("account");
  w.onEcho("wpkh([73c5da0a/84'/0'/0']xpub6CatWdiZiodmUeTDp8LT5or8nmbKNcuyvz7WyksVFkKB4RHwCD3XyuvPEbvqAQY3rAPshWcMLoP2fMFMKHPJ4ZeZXYVUhLv1VMrjPC7PW6V/<0;1>/*)#abcdefgh");
  assert.equal(w.scan(), "account");
  assert.deepEqual(w.last.lines, ["👛 73c5da0a · 84'/0'/0'", "xpub6CatWdiZ…PW6V"]);
});
test("a signed PSBT QR: nothing collected yet, still reading", () => {
  const w = mk("psbt");
  assert.equal(w.scan(), "waiting");
  assert.equal(w.scanning, true);
});
test("a signed PSBT QR: what it holds, read from its own bytes, and that it is not sent", () => {
  const e = el();
  const w = mk("psbt", e);
  const b64 = V1.psbt, n = Buffer.from(b64, "base64").length;
  for (const l of [`signed-psbt-begin 1 ${n}`, `signed-psbt ${b64}`, "signed-psbt-end 1"]) w.onEcho(l);
  assert.equal(w.scan(), "ok");
  assert.equal(e.out.value, b64);
  assert.deepEqual(w.last.lines, ["→ bc1qkd3…pe9ug · 60000 sat", "↩ bc1q8c6…cp6el · 39000 sat", "⛽ 1000 sat",
    `${n} B`, "❌", "📡 not sent"]);   // the unsigned vector: not the expected signature
  assert.equal(w.last.ok, false);
});
test("a reboot forgets the descriptor", () => {
  const w = mk("account");
  w.onEcho("wpkh([73c5da0a/84'/0'/0']xpub6Cat/<0;1>/*)#x");
  w.onEcho("reboot");
  assert.equal(w.scan(), "waiting");
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
