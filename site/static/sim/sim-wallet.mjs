// The simulated wallet: a test wallet that sends a seed and a PSBT through
// its headphone output, and "scans" the signer's QR with its camera. It has
// one test pair (seed + PSBT) per network and follows the network the signer
// shows. Scanning: the firmware says which QR is on screen (dsw_qr_kind);
// what the account and the signed PSBT hold comes from the signer's public
// echo, which carries the same bytes as the QR (tests/c/test_app.c checks
// they match); the image itself is not decoded. Nothing is ever broadcast.
// Next to a telephone's handset (the Centralita) it plays at air pace and
// listens to the earpiece itself: ur-tones' Listener on the samples, never
// the echo.
import { seedAudio, psbtAudio, SignedCollector, EarDecoder } from "./wallet.mjs";
import { RATE } from "./core.mjs";
import { networkOf, parseDescriptor, outputLines, scanCheck, shortXpub, fill } from "./checks.mjs";
import { decodePsbt } from "./psbt.mjs";

const fromBase64 = (b64) => Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));

export class SimWallet {
  constructor({ sim, U, sha256, S = {}, data, el }) {
    Object.assign(this, { sim, U, sha256, S, data, el });
    this.collector = new SignedCollector();
    this.descriptor = null;    // the account the signer last echoed
    this.scanning = false;
    this.net = "mainnet";
    this.signerNet = null;     // the last network the signer's screen showed
    this.last = null;          // what the last action says to check: { lines, ok? }
  }
  pair() { return this.data.networks[this.net]; }
  setNetwork(net) { this.net = net; this.last = null; }
  // Call on each frame: remembers the network the signer shows.
  watch() { this.signerNet = networkOf(this.sim.lines()) ?? this.signerNet; }
  mismatch() { return this.signerNet !== null && this.signerNet !== this.net; }
  sendSeed() { return this.U ? seedAudio(this.U, this.pair().seed, this.data.pin) : new Int16Array(0); }
  sendPsbt() { return this.U ? psbtAudio(this.U, this.pair().psbt) : new Int16Array(0); }
  // By air, next to the handset: the seed needs a PIN of 12 or more there.
  sendSeedAir() { return seedAudio(this.U, this.pair().seed, this.data.pin, RATE, "air"); }
  sendPsbtAir() { return psbtAudio(this.U, this.pair().psbt, RATE, 2, "air"); }
  // The seed as the keypad types it ("*", 4 digits a word, "#"; no PIN).
  keypadCode() { return this.U.seedToKeypad(Uint8Array.from(this.pair().seed.match(/../g), (b) => parseInt(b, 16))); }
  // Listening at the earpiece: hear() each block of it; "listening", or
  // "ok" once a whole signed PSBT is in (also put in el.out; this.last says
  // what to check), or "other" for another message (the account).
  listen() { this.ear = new EarDecoder(this.U, RATE); this.last = null; }
  stopListening() { this.ear = null; }
  get listening() { return Boolean(this.ear); }
  hear(samples) {
    if (!this.ear) return null;
    this.ear.push(samples);
    const r = this.ear.result;
    if (!r) return "listening";
    this.ear = null;
    if (r.type !== "crypto-psbt") {
      this.last = { lines: [fill(this.S["wallet.bytes"], { bytes: r.bytes })] };
      return "other";
    }
    this.last = this.signedCheck(r);
    return "ok";
  }
  onEcho(line) {
    if (line === "reboot") {
      this.collector = new SignedCollector();
      this.signerNet = null;
      this.descriptor = null;
      return;
    }
    const d = parseDescriptor(line);
    if (d) this.descriptor = d;
    else this.collector.push(line);
  }
  // "no-qr", "repo", "account", "waiting" (its content not there yet) or
  // "ok" (a signed PSBT, also put in el.out). this.last says what it holds.
  scan() {
    const S = this.S, kind = this.sim.qrKind();
    this.last = null;
    if (kind === "none") { this.scanning = false; return "no-qr"; }
    this.scanning = true;
    if (kind === "repo") {
      this.last = { lines: [fill(S["wallet.qr_repo"], { url: this.data.repo })] };
      return "repo";
    }
    if (kind === "account") {
      const d = this.descriptor;
      if (!d) return "waiting";
      this.last = { lines: [fill(S["wallet.qr_account"], { fp: d.fingerprint, path: d.path }), fill(S["wallet.qr_xpub"], { xpub: shortXpub(d.xpub) })] };
      return "account";
    }
    const r = this.collector.result;
    if (!r) return "waiting";
    this.last = this.signedCheck(r);
    return "ok";
  }
  // What a signed PSBT ({ base64, bytes }) holds and whether it carries the
  // expected signature; shown in el.out too.
  signedCheck(r) {
    const S = this.S;
    // Addresses in the signer's network (it signed in it), else the wallet's.
    let held = [];
    try {
      const { outputs, fee } = decodePsbt(fromBase64(r.base64), (this.signerNet ?? this.net) === "testnet", this.sha256,
        this.descriptor?.fingerprint ?? this.pair().fingerprint);
      held = outputLines(outputs, fee, S);
    } catch { /* not readable: the size and the signature still say something */ }
    const sig = scanCheck(r, this.pair(), S);
    this.el.out.value = r.base64;
    this.el.out.hidden = false;
    return { ok: sig.ok, lines: [...held, ...sig.lines, S["wallet.no_broadcast"]] };
  }
}
