// What the simulated wallet tells you to check on the signer, in a few short
// lines: after the seed (PIN, fingerprint), after the PSBT (outputs, change,
// fee) and after scanning the signed PSBT (its size, the expected signature).
// S holds the page's texts; {name} in them is filled in here.

export const fill = (text, params) => text.replace(/\{(\w+)\}/g, (m, k) => (k in params ? String(params[k]) : m));

// The network the signer shows ("mainnet" or "testnet" on its menu and
// settings), or null when the screen does not say.
export function networkOf(lines) {
  for (const l of lines) {
    const m = /\b(mainnet|testnet)\b/.exec(l);
    if (m) return m[1];
  }
  return null;
}

export const shortAddr = (a) => `${a.slice(0, 7)}…${a.slice(-5)}`;
export const shortKey = (hex) => `${hex.slice(0, 8)}…${hex.slice(-4)}`;
export const shortXpub = (x) => `${x.slice(0, 12)}…${x.slice(-4)}`;

// The account descriptor the signer echoes with its account QR:
// wpkh([fingerprint/path]xpub/<0;1>/*)#checksum. null for any other line.
export function parseDescriptor(line) {
  const m = /^\w+\(\[([0-9a-f]{8})\/([^\]]+)\]([1-9A-HJ-NP-Za-km-z]+)/.exec(line);
  return m ? { fingerprint: m[1], path: m[2], xpub: m[3] } : null;
}

export function seedCheck(net, pin, S) {
  return [fill(S["wallet.check_seed"], { pin, fp: net.fingerprint })];
}

// One line per output (the change marked), then the fee.
export function outputLines(outputs, fee, S) {
  const lines = outputs.map((o) => fill(S[o.kind === "change" || o.change ? "wallet.change" : "wallet.out"],
    { addr: o.address ? shortAddr(o.address) : `script ${o.script.slice(0, 8)}…`, sats: o.amount }));
  if (fee !== null && fee !== undefined) lines.push(fill(S["wallet.fee"], { sats: fee }));
  return lines;
}

export function psbtCheck(net, S) {
  return outputLines(net.outputs, net.fee, S);
}

// result: { base64, bytes } as the wallet collected it. ok when every
// signature of the pair (public key and signature) is in it.
export function scanCheck(result, net, S) {
  const bin = atob(result.base64);
  let hex = "";
  for (let i = 0; i < bin.length; i++) hex += bin.charCodeAt(i).toString(16).padStart(2, "0");
  const ok = net.signatures.length > 0 && net.signatures.every((s) => hex.includes(s.pubkey) && hex.includes(s.sig));
  const lines = [fill(S["wallet.bytes"], { bytes: result.bytes })];
  lines.push(ok ? fill(S["wallet.sig_ok"], { key: shortKey(net.signatures[0].pubkey) }) : S["wallet.sig_bad"]);
  return { ok, lines };
}
