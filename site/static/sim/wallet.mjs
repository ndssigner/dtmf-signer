// The simulator's test wallet: what a smartphone with the ur-tones web tool, or
// a computer with a wallet and ur-tones, would send by cable. It uses the
// ur-tones JavaScript itself (UrTones, passed in), so two implementations
// talk: ur-tones' JavaScript sends, the firmware's C listens.

import { RATE } from "./core.mjs";

// ur-tones SPEC paces: tone, silence, and the pause between frames (ms).
// By cable 40/20/400; by air (a speaker next to a microphone) 80/80/600.
export const PACES = { cable: [40, 20, 400], air: [80, 80, 600] };

const fromHex = (hex) => Uint8Array.from(hex.match(/../g) ?? [], (b) => parseInt(b, 16));

function fromBase64(b64) {
  const bin = atob(b64);
  return Uint8Array.from(bin, (c) => c.charCodeAt(0));
}

function toInt16(f32) {
  const out = new Int16Array(f32.length);
  for (let i = 0; i < f32.length; i++) out[i] = Math.max(-32768, Math.min(32767, Math.round(f32[i] * 32767)));
  return out;
}

const render = (U, frames, rate, pace) => toInt16(U.render(frames, rate, ...PACES[pace]));

// A seed (entropy in hex) protected by a PIN, as one UR frame.
export function seedAudio(U, entropyHex, pin, rate = RATE, pace = "cable") {
  return render(U, [U.urToFrame(U.seedToUR(fromHex(entropyHex), pin))], rate, pace);
}

// A PSBT (base64) as the parts of a multi-part UR, 100-byte fragments, sent
// `rounds` times over (the fountain code needs only enough parts, in any order).
export function psbtAudio(U, psbtBase64, rate = RATE, rounds = 2, pace = "cable") {
  const psbt = fromBase64(psbtBase64);
  const { urs } = U.psbtToURs(psbt, 100, 0);
  const all = urs.length > 1 ? U.psbtToURs(psbt, 100, urs.length * (rounds - 1)).urs : urs;
  return render(U, all.map((ur) => U.urToFrame(ur)), rate, pace);
}

const toBase64 = (bytes) => btoa(Array.from(bytes, (b) => String.fromCharCode(b)).join(""));

// A wallet listening to a loudspeaker or an earpiece: ur-tones' own
// streaming Listener on the samples, the frames made into URs, the parts
// gathered by its fountain decoder. result: { type, base64, bytes } (the
// PSBT itself for a crypto-psbt, else the message) once a whole message is in.
export class EarDecoder {
  constructor(U, rate = RATE) {
    this.U = U;
    this.result = null;
    this.parts = 0;            // frames heard and understood
    this.fountain = null;
    this.listener = new U.Listener(rate, { onGroup: (g) => this.group(g) });
  }
  push(int16) {
    const f = new Float32Array(int16.length);
    for (let i = 0; i < int16.length; i++) f[i] = int16[i] / 32768;
    this.listener.push(f);
  }
  flush() { this.listener.flush(); }
  group(tones) {
    if (this.result) return;
    let frame;
    try { frame = this.U.frameToUR(tones); } catch { return; }   // a key, a voice, a broken frame
    this.parts++;
    let msg = null;
    if (frame.kind === 0) msg = frame.body;
    else {
      if (!this.fountain || this.fountain.type !== frame.type) this.fountain = { type: frame.type, dec: new this.U.FountainDecoder() };
      try { this.fountain.dec.receive(frame.body); } catch { return; }
      if (this.fountain.dec.error) { this.fountain = null; return; }
      msg = this.fountain.dec.result;
    }
    if (!msg) return;
    let bytes = msg;
    if (frame.type === "crypto-psbt" || frame.type === "psbt") {
      try { bytes = this.U.cborToPsbt(msg); } catch { return; }
    }
    this.result = { type: frame.type, base64: toBase64(bytes), bytes: bytes.length };
  }
}

// Gathers the signed PSBT from the firmware's public echo:
//   signed-psbt-begin <id> <bytes> / signed-psbt <base64>… / signed-psbt-end <id>
// "signed-psbt-abort <id>" drops it; so does a size that does not match.
export class SignedCollector {
  constructor() { this.cur = null; this.done = null; }
  push(line) {
    const [word, a, b] = line.split(" ");
    if (word === "signed-psbt-begin") { this.cur = { id: Number(a), bytes: Number(b), parts: [] }; this.done = null; }
    else if (word === "signed-psbt" && this.cur) this.cur.parts.push(a);
    else if (word === "signed-psbt-abort") this.cur = null;
    else if (word === "signed-psbt-end" && this.cur && Number(a) === this.cur.id) {
      const base64 = this.cur.parts.join("");
      if (fromBase64(base64).length === this.cur.bytes) this.done = { id: this.cur.id, base64, bytes: this.cur.bytes };
      this.cur = null;
    }
  }
  get result() { return this.done; }
}
