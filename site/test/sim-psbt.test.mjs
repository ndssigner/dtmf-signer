// The simulated wallet reads what it scanned: the outputs, the change and the
// fee of the signed PSBT itself, checked against the vectors' own figures.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { decodePsbt } from "../static/sim/psbt.mjs";
import { loadUrTonesCrypto } from "./urtones-helpers.mjs";

const { sha256 } = loadUrTonesCrypto();
const V = JSON.parse(readFileSync(new URL("../../vectors/psbt-v0.json", import.meta.url), "utf8")).vectors;
const bytesOf = (name) => Buffer.from(V.find((v) => v.name === name).psbt, "base64");

test("one input with change: two outputs, the change marked, the fee", () => {
  const r = decodePsbt(bytesOf("one input with change"), false, sha256);
  assert.deepEqual(r.outputs, [
    { amount: 60000, address: "bc1qkd33yck74lg0kaq4tdcmu3hk4yruhjayxpe9ug", change: false },
    { amount: 39000, address: "bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el", change: true }]);
  assert.equal(r.fee, 1000);
});
test("testnet addresses", () => {
  const r = decodePsbt(bytesOf("testnet account m/84'/1'/0'"), true, sha256);
  assert.equal(r.outputs[0].address, "tb1qkd33yck74lg0kaq4tdcmu3hk4yruhjayv8zk8m");
  assert.equal(r.outputs[1].address, "tb1qnss6dq8nvkeyv7vjpcvshx22ud8u8cywyx9g24");
  assert.equal(r.fee, 1000);
});
test("every kind of address the vectors hold, and a script without one", () => {
  for (const name of ["outputs to P2TR, P2SH and OP_RETURN", "BIP-39 passphrase", "two inputs with non_witness_utxo"]) {
    const v = V.find((x) => x.name === name);
    const r = decodePsbt(bytesOf(name), false, sha256);
    assert.equal(r.fee, Number(v.fee), name);
    v.outputs.forEach((o, i) => {
      assert.equal(r.outputs[i].amount, o.amount, name);
      if (o.address) assert.equal(r.outputs[i].address, o.address, name);
      else { assert.equal(r.outputs[i].address, null); assert.equal(r.outputs[i].script, o.script_hex); }
      assert.equal(r.outputs[i].change, o.kind === "change", `${name} #${i}`);
    });
  }
});
test("without the inputs' amounts the fee is unknown", () => {
  // A PSBT by hand: one input, one P2WPKH output of 1000 sat, empty maps.
  const tx = Buffer.from("02000000" + "01" + "11".repeat(32) + "00000000" + "00" + "fdffffff" +
    "01" + "e803000000000000" + "16" + "0014" + "22".repeat(20) + "00000000", "hex");
  const psbt = Buffer.concat([Buffer.from("70736274ff", "hex"), Buffer.from([1, 0, tx.length]), tx, Buffer.from([0, 0, 0])]);
  const r = decodePsbt(psbt, false, sha256);
  assert.equal(r.fee, null);
  assert.equal(r.outputs[0].amount, 1000);
});
