import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { networkOf, shortAddr, shortKey, seedCheck, psbtCheck, scanCheck } from "../static/sim/checks.mjs";

const V = JSON.parse(readFileSync(new URL("../../vectors/psbt-v0.json", import.meta.url), "utf8")).vectors;
const S = { "wallet.check_seed": "PIN {pin} · {fp}", "wallet.out": "→ {addr} · {sats} sat", "wallet.change": "↩ {addr} · {sats} sat",
  "wallet.fee": "⛽ {sats} sat", "wallet.sig_ok": "✅ {key}", "wallet.sig_bad": "❌", "wallet.bytes": "{bytes} B" };

test("networkOf reads the word the signer shows", () => {
  assert.equal(networkOf(["DTMF-Signer   testnet", "> Load seed"]), "testnet");
  assert.equal(networkOf(["  Network   mainnet"]), "mainnet");
  assert.equal(networkOf(["Welcome"]), null);
});
test("addresses keep a prefix and a suffix; keys too", () => {
  assert.equal(shortAddr("bc1qkd33yck74lg0kaq4tdcmu3hk4yruhjayxpe9ug"), "bc1qkd3…pe9ug");
  assert.equal(shortKey("0330d54fd0dd420a6e5f8d3624f5f3482cae350f79d5f0753bf5beef9c2d91af3c"), "0330d54f…af3c");
});
test("the seed check names the PIN and the fingerprint", () => {
  assert.deepEqual(seedCheck({ fingerprint: "73c5da0a" }, "1234", S), ["PIN 1234 · 73c5da0a"]);
});
test("the PSBT check lists outputs, change and fee", () => {
  const net = { fee: 1000, outputs: [{ amount: 60000, address: "bc1qkd33yck74lg0kaq4tdcmu3hk4yruhjayxpe9ug", kind: "external" },
    { amount: 39000, address: "bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el", kind: "change" }] };
  assert.deepEqual(psbtCheck(net, S), ["→ bc1qkd3…pe9ug · 60000 sat", "↩ bc1q8c6…cp6el · 39000 sat", "⛽ 1000 sat"]);
});
test("scan check passes with the expected signature", () => {
  const v = V.find((x) => x.name === "one input with change");
  const bytes = Buffer.from(v.signatures[0].pubkey + v.signatures[0].sig, "hex");
  const r = scanCheck({ base64: bytes.toString("base64"), bytes: bytes.length }, v, S);
  assert.equal(r.ok, true);
  assert.deepEqual(r.lines, [`${bytes.length} B`, "✅ 0330d54f…af3c"]);
});
test("scan check fails on a foreign signature", () => {
  const v = V.find((x) => x.name === "one input with change");
  const other = V.find((x) => x.name.startsWith("testnet"));
  const bytes = Buffer.from(other.signatures[0].pubkey + other.signatures[0].sig, "hex");
  const r = scanCheck({ base64: bytes.toString("base64"), bytes: bytes.length }, v, S);
  assert.equal(r.ok, false);
  assert.equal(r.lines.at(-1), "❌");
});
