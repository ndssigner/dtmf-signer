// The Centralita's simulator, without any page: the telephone around the
// voice firmware (ports/wasm/voice.c). It does only what the hardware
// around the signer does. The telephone: its keypad (a dialler's DTMF,
// 100 ms of the key's pair at -10 dBFS each), its hook, its handset
// microphone (what a wallet plays next to it) and its earpiece (the line
// plus the keypad's sidetone at -20 dB). The line: one node, so what the
// signer hears is its own last block out plus the keypad plus the
// microphone, clipped, as on the board. Everything else is the firmware.

export const RATE = 16000;          // the line, both ways
const STEP_MS = 16;
const MAX_ADVANCE_MS = 250;         // a paused or slow tab never makes the firmware jump
const KEY_MS = 100, KEY_GAP_MS = 200;   // a dialler's tone, and the silence before the next
const KEY_LEVEL = 10 ** (-10 / 20);     // each tone of the pair
const SIDETONE = 10 ** (-20 / 20);      // the keypad heard in the earpiece
const ROWS = ["123A", "456B", "789C", "*0#D"], LOW = [697, 770, 852, 941], HIGH = [1209, 1336, 1477, 1633];

const clip = (x) => (x > 32767 ? 32767 : x < -32768 ? -32768 : x);

// A key's DTMF as a telephone dials it: KEY_MS of its pair, 2 ms ramps.
export function keyTone(key, rate = RATE) {
  const r = ROWS.findIndex((row) => row.includes(key));
  if (r < 0 || key.length !== 1) throw new Error(`no telephone key ${key}`);
  const lo = LOW[r], hi = HIGH[ROWS[r].indexOf(key)];
  const n = Math.round(rate * KEY_MS / 1000), ramp = Math.round(rate * 0.002);
  const out = new Int16Array(n);
  for (let i = 0; i < n; i++) {
    const env = Math.min(1, i / ramp, (n - 1 - i) / ramp);
    const s = env * KEY_LEVEL * (Math.sin(2 * Math.PI * lo * i / rate) + Math.sin(2 * Math.PI * hi * i / rate));
    out[i] = Math.round(s * 32767);
  }
  return out;
}

// Int16Array chunks played one after the other.
class Queue {
  constructor() { this.clear(); }
  clear() { this.chunks = []; this.pos = 0; }
  push(x) { if (x.length) this.chunks.push(x); }
  get length() { return this.chunks.reduce((n, c) => n + c.length, 0) - this.pos; }
  // The next n samples into out (silence once it runs out).
  take(out) {
    let i = 0;
    while (i < out.length && this.chunks.length) {
      const c = this.chunks[0], k = Math.min(out.length - i, c.length - this.pos);
      out.set(c.subarray(this.pos, this.pos + k), i);
      i += k; this.pos += k;
      if (this.pos === c.length) { this.chunks.shift(); this.pos = 0; }
    }
    out.fill(0, i);
  }
}

export class VoiceSimulator {
  // pack: the bytes of the page language's pack (none for English, built in).
  constructor(module, lang = "en", pack = null) {
    Object.assign(this, { m: module, lang, pack });
    this.offHook = false;   // the handset, as it lies: a power cut does not move it
    this.reset();
  }

  // A power cut: the packs are copied into the region again and the
  // firmware starts from zero; the telephone stays as it is.
  reset() {
    const region = this.m.HEAPU8.subarray(this.m._dvw_region_buf(), this.m._dvw_region_buf() + this.m._dvw_region_len());
    region.fill(0);
    if (this.pack) region.set(this.pack, 0);
    this.m._dvw_init();
    this.keypad = new Queue();
    this.mic = new Queue();
    this.carry = 0;
    this.clock = 0;
    this.quietMs = 0;       // since the signer last put sound on the line
    this.lastOut = new Int16Array(0);
    this.lastIn = new Int16Array(0);
    if (this.offHook) this.m._dvw_hook(1);
  }

  hook(off) {
    this.offHook = Boolean(off);
    this.m._dvw_hook(this.offHook ? 1 : 0);
    if (!this.offHook) this.keypad.clear();   // the handset down cuts a key short
  }
  // A key on the keypad: its tone, then the dialler's gap. On-hook it
  // reaches nothing.
  pressKey(k) {
    if (!this.offHook) return;
    this.keypad.push(keyTone(k));
    this.keypad.push(new Int16Array(RATE * KEY_GAP_MS / 1000));
  }
  // What is played next to the handset microphone.
  queueMic(samples) { this.mic.push(samples); }
  clearMic() { this.mic.clear(); }
  get micQueued() { return this.mic.length; }
  get keysQueued() { return this.keypad.length; }

  // ms of line in 16 ms steps: { echo: [lines], earpiece: Int16Array,
  // room: Int16Array } (room: what the wallet played aloud meanwhile).
  advance(ms) {
    const want = Math.min(Math.max(0, ms), MAX_ADVANCE_MS) + this.carry;
    const total = Math.floor(want);
    this.carry = want - total;
    const m = this.m, echo = [];
    const earpiece = new Int16Array(total * RATE / 1000), room = new Int16Array(earpiece.length);
    let at = 0;
    for (let t = 0; t < total; t += STEP_MS) {
      const step = Math.min(STEP_MS, total - t), n = step * RATE / 1000;
      const got = m._dvw_out(n), op = m._dvw_out_buf() >> 1;
      const out = new Int16Array(n);
      out.set(m.HEAP16.subarray(op, op + got));
      const key = new Int16Array(n), mic = new Int16Array(n);
      this.keypad.take(key);
      this.mic.take(mic);   // the wallet plays on, whatever the hook
      room.set(mic, at);
      const ip = m._dvw_in_buf() >> 1, line = m.HEAP16.subarray(ip, ip + n);
      for (let i = 0; i < n; i++) {
        line[i] = this.offHook ? clip(out[i] + key[i] + mic[i]) : out[i];
        earpiece[at + i] = this.offHook ? clip(out[i] + Math.round(key[i] * SIDETONE)) : 0;
      }
      this.lastOut = out;
      this.lastIn = line.slice();
      m._dvw_in(n);
      m._dvw_advance(step);
      this.clock += step;
      this.quietMs = out.some((x) => x !== 0) ? 0 : this.quietMs + step;
      while (m._dvw_take_echo() > 0) echo.push(m.UTF8ToString(m._dvw_echo()));
      at += n;
    }
    return { echo, earpiece, room };
  }

  keys() { return this.m._dvw_keys(); }
  version() { return this.m.UTF8ToString(this.m._dvw_version()); }
}
