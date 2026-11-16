// What a PSBT (version 0) says, read from its bytes: each output's amount and
// address, which outputs are change (a BIP-32 derivation from our wallet's
// fingerprint, the key or a Taproot key), and the
// fee when every input gives its amount. For the simulated wallet to show what
// it scanned. sha256 is passed in (ur-tones' crypto) for base58 addresses.

class Reader {
  constructor(b) { this.b = b; this.i = 0; }
  byte() { if (this.i >= this.b.length) throw new Error("psbt: truncated"); return this.b[this.i++]; }
  bytes(n) { if (this.i + n > this.b.length) throw new Error("psbt: truncated"); const s = this.b.subarray(this.i, this.i + n); this.i += n; return s; }
  u32() { const s = this.bytes(4); return s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] * 2 ** 24); }
  u64() { const lo = this.u32(), hi = this.u32(); return hi * 2 ** 32 + lo; }
  varint() {
    const n = this.byte();
    if (n < 0xfd) return n;
    if (n === 0xfd) { const s = this.bytes(2); return s[0] | (s[1] << 8); }
    if (n === 0xfe) return this.u32();
    return this.u64();
  }
  get done() { return this.i >= this.b.length; }
}

// A transaction's inputs (their previous outputs) and outputs; the witness
// form is accepted too (for a non_witness_utxo).
function readTx(bytes) {
  const r = new Reader(bytes);
  r.u32();
  let nIn = r.varint(), witness = false;
  if (nIn === 0) { r.byte(); witness = true; nIn = r.varint(); }
  const ins = [];
  for (let k = 0; k < nIn; k++) { r.bytes(32); const vout = r.u32(); r.bytes(r.varint()); r.u32(); ins.push({ vout }); }
  const outs = [];
  for (let k = r.varint(); k > 0; k--) { const amount = r.u64(); outs.push({ amount, script: r.bytes(r.varint()) }); }
  if (witness) for (let k = 0; k < nIn; k++) for (let n = r.varint(); n > 0; n--) r.bytes(r.varint());
  return { ins, outs };
}

// A map: [{ type, key, value }] up to its 0x00 separator.
function readMap(r) {
  const out = [];
  for (;;) {
    const kl = r.varint();
    if (kl === 0) return out;
    const key = r.bytes(kl);
    out.push({ type: key[0], key, value: r.bytes(r.varint()) });
  }
}

// ---- addresses ----
const BECH = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
function polymod(values) {
  const G = [0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3];
  let chk = 1;
  for (const v of values) {
    const top = chk >>> 25;
    chk = ((chk & 0x1ffffff) << 5) ^ v;
    for (let i = 0; i < 5; i++) if ((top >>> i) & 1) chk ^= G[i];
  }
  return chk >>> 0;
}
function segwit(hrp, version, program) {
  const data = [version];
  let acc = 0, bits = 0;
  for (const b of program) { acc = (acc << 8) | b; bits += 8; while (bits >= 5) { bits -= 5; data.push((acc >>> bits) & 31); } }
  if (bits) data.push((acc << (5 - bits)) & 31);
  const exp = [...hrp].map((c) => c.charCodeAt(0) >> 5).concat([0], [...hrp].map((c) => c.charCodeAt(0) & 31));
  const mod = polymod(exp.concat(data, [0, 0, 0, 0, 0, 0])) ^ (version === 0 ? 1 : 0x2bc830a3);
  const sum = [0, 1, 2, 3, 4, 5].map((i) => (mod >>> (5 * (5 - i))) & 31);
  return hrp + "1" + data.concat(sum).map((d) => BECH[d]).join("");
}
const B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
function base58check(version, payload, sha256) {
  const body = Uint8Array.from([version, ...payload]);
  const all = Uint8Array.from([...body, ...sha256(sha256(body)).slice(0, 4)]);
  let n = 0n;
  for (const b of all) n = n * 256n + BigInt(b);
  let s = "";
  while (n > 0n) { s = B58[Number(n % 58n)] + s; n /= 58n; }
  for (const b of all) { if (b !== 0) break; s = "1" + s; }
  return s;
}
function address(s, testnet, sha256) {
  const hrp = testnet ? "tb" : "bc";
  if (s.length === 22 && s[0] === 0x00 && s[1] === 20) return segwit(hrp, 0, s.subarray(2));
  if (s.length === 34 && s[0] === 0x00 && s[1] === 32) return segwit(hrp, 0, s.subarray(2));
  if (s.length === 34 && s[0] === 0x51 && s[1] === 32) return segwit(hrp, 1, s.subarray(2));
  if (s.length === 25 && s[0] === 0x76 && s[1] === 0xa9 && s[2] === 20 && s[23] === 0x88 && s[24] === 0xac)
    return base58check(testnet ? 0x6f : 0x00, s.subarray(3, 23), sha256);
  if (s.length === 23 && s[0] === 0xa9 && s[1] === 20 && s[22] === 0x87) return base58check(testnet ? 0xc4 : 0x05, s.subarray(2, 22), sha256);
  return null;
}
const hex = (b) => [...b].map((x) => x.toString(16).padStart(2, "0")).join("");

// The fingerprint (hex) a derivation entry starts from: PSBT_OUT_BIP32_DERIVATION
// (0x02) holds it first; PSBT_OUT_TAP_BIP32_DERIVATION (0x07) after its leaf hashes.
function derivedFrom(e) {
  if (e.type === 0x02) return hex(e.value.subarray(0, 4));
  if (e.type === 0x07) {
    const r = new Reader(e.value);
    r.bytes(32 * r.varint());
    return hex(r.bytes(4));
  }
  return null;
}

// { outputs: [{ amount, address, change, script? }], fee: number | null }
// fingerprint: our wallet's (hex); an output is change only when derived from it.
export function decodePsbt(bytes, testnet, sha256, fingerprint) {
  const r = new Reader(bytes);
  if (hex(r.bytes(5)) !== "70736274ff") throw new Error("psbt: not a PSBT");
  const tx = readMap(r).find((e) => e.type === 0x00 && e.key.length === 1);
  if (!tx) throw new Error("psbt: no unsigned transaction");
  const { ins, outs } = readTx(tx.value);
  let inSum = 0, known = true;
  for (const input of ins) {
    const m = readMap(r);
    const w = m.find((e) => e.type === 0x01), full = m.find((e) => e.type === 0x00);
    if (w) inSum += new Reader(w.value).u64();
    else if (full) inSum += readTx(full.value).outs[input.vout]?.amount ?? NaN;
    else known = false;
  }
  const outputs = outs.map((o) => {
    const m = readMap(r);
    const a = address(o.script, testnet, sha256);
    const out = { amount: o.amount, address: a, change: m.some((e) => derivedFrom(e) === fingerprint) };
    if (a === null) out.script = hex(o.script);
    return out;
  });
  const fee = known && Number.isFinite(inSum) ? inSum - outs.reduce((s, o) => s + o.amount, 0) : null;
  if (fee !== null && fee < 0) return { outputs, fee: null };   // a broken PSBT: no fee to show
  return { outputs, fee };
}
