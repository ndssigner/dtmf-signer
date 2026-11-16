// The simulated phone: a test wallet that sends a seed and a PSBT through
// its headphone jack, and "scans" the signer's QR with its camera. The scan
// takes the signed PSBT from the signer's public echo, which carries the
// same bytes as the QR (tests/c/test_app.c checks they match); the image
// itself is not decoded.
import { seedAudio, psbtAudio, SignedCollector } from "./wallet.mjs";

export class SimPhone {
  constructor({ sim, U, data, el }) {
    Object.assign(this, { sim, U, data, el });
    this.collector = new SignedCollector();
    this.scanning = false;
  }
  sendSeed() { return this.U ? seedAudio(this.U, this.data.seed, this.data.pin) : new Int16Array(0); }
  sendPsbt() { return this.U ? psbtAudio(this.U, this.data.psbt) : new Int16Array(0); }
  onEcho(line) {
    if (line === "reboot") this.collector = new SignedCollector();
    else this.collector.push(line);
  }
  // "no-qr", "other-qr" (not the signed screen), "waiting" (nothing signed
  // yet) or "ok" (shown in el.out).
  scan() {
    if (!this.sim.qrShown()) { this.scanning = false; return "no-qr"; }
    // Only the signed screen's QR is a signed PSBT (the account QR is not).
    if (!/^Signed/.test((this.sim.lines()[0] ?? "").trim())) { this.scanning = false; return "other-qr"; }
    this.scanning = true;
    const r = this.collector.result;
    if (!r) return "waiting";
    this.el.out.value = r.base64;
    this.el.out.hidden = false;
    return "ok";
  }
}
