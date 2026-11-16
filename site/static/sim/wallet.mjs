// The simulator's test wallet: what a phone with the ur-tones web tool, or
// a computer with a wallet and ur-tones, would send by cable. It uses the
// ur-tones JavaScript itself (UrTones, passed in), so two implementations
// talk: ur-tones' JavaScript sends, the firmware's C listens.

import { RATE } from "./core.mjs";

// Cable pace (ur-tones SPEC): 40 ms tone, 20 ms silence, 400 ms between frames.
const TONE_MS = 40, GAP_MS = 20, PAUSE_MS = 400;

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

const render = (U, frames, rate) => toInt16(U.render(frames, rate, TONE_MS, GAP_MS, PAUSE_MS));

// A seed (entropy in hex) protected by a PIN, as one UR frame.
export function seedAudio(U, entropyHex, pin, rate = RATE) {
  return render(U, [U.urToFrame(U.seedToUR(fromHex(entropyHex), pin))], rate);
}

// A PSBT (base64) as the parts of a multi-part UR, 100-byte fragments, sent
// `rounds` times over (the fountain code needs only enough parts, in any order).
export function psbtAudio(U, psbtBase64, rate = RATE, rounds = 2) {
  const psbt = fromBase64(psbtBase64);
  const { urs } = U.psbtToURs(psbt, 100, 0);
  const all = urs.length > 1 ? U.psbtToURs(psbt, 100, urs.length * (rounds - 1)).urs : urs;
  return render(U, all.map((ur) => U.urToFrame(ur)), rate);
}

// Gathers the signed PSBT from the firmware's public echo:
//   signed-psbt-begin <id> <bytes> / signed-psbt <base64>… / signed-psbt-end <id>
// "signed-psbt-abort <id>" drops it; so does a size that does not match.
export class SignedCollector {
  constructor() { this.cur = null; this.done = null; }
  push(line) {
    const [word, a, b] = line.split(" ");
    if (word === "signed-psbt-begin") this.cur = { id: Number(a), bytes: Number(b), parts: [] };
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
