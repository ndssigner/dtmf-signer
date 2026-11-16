// The simulator's board, without any page: a virtual clock that drives the
// firmware (ports/wasm) in 16 ms steps, the button held, the audio queued
// on its line-in, and the public echo it gives back. The page (ui.mjs) and
// the tests both drive it through this class.

export const RATE = 16000;        // line-in samples a second, as on the Pico
const STEP_MS = 16;
const MAX_ADVANCE_MS = 250;       // a paused or slow tab never makes the firmware jump

export class Simulator {
  constructor(module) {
    this.m = module;
    this.reset();
  }

  reset() {
    this.m._dsw_init();
    this.queue = [];               // Int16Array chunks waiting on the line
    this.queuePos = 0;             // samples of queue[0] already played
    this.held = 0;
    this.carry = 0;
  }

  press(cls) { this.held = cls; }
  release() { this.held = 0; }
  get cls() { return this.held; }

  queueAudio(samples) { if (samples.length) this.queue.push(samples); }
  clearAudio() { this.queue = []; this.queuePos = 0; }
  get queued() {
    return this.queue.reduce((n, c) => n + c.length, 0) - this.queuePos;
  }

  // Fills the module's buffer with the next n samples of the queue, or
  // silence once it runs out.
  feed(n) {
    const at = this.m._dsw_audio_buf() >> 1;
    const buf = this.m.HEAP16.subarray(at, at + n);
    let i = 0;
    while (i < n && this.queue.length) {
      const chunk = this.queue[0];
      const take = Math.min(n - i, chunk.length - this.queuePos);
      buf.set(chunk.subarray(this.queuePos, this.queuePos + take), i);
      i += take;
      this.queuePos += take;
      if (this.queuePos === chunk.length) { this.queue.shift(); this.queuePos = 0; }
    }
    buf.fill(0, i);
    this.m._dsw_audio(n);
  }

  advance(ms) {
    // Whole milliseconds only; the fraction waits for the next call.
    const want = Math.min(Math.max(0, ms), MAX_ADVANCE_MS) + this.carry;
    const total = Math.floor(want);
    this.carry = want - total;
    const echo = [];
    let dirty = false;
    for (let t = 0; t < total; t += STEP_MS) {
      const ms = Math.min(STEP_MS, total - t);   // the last step may be shorter
      this.feed(ms * (RATE / 1000));
      this.m._dsw_advance(ms, this.held);
      while (this.m._dsw_take_echo() > 0) echo.push(this.m.UTF8ToString(this.m._dsw_echo()));
      if (this.m._dsw_take_dirty()) dirty = true;
      if (this.m._dsw_wants_reboot()) {
        this.reset();
        echo.push("reboot");
        dirty = true;
        break;
      }
    }
    return { dirty, echo };
  }

  frame() {
    const at = this.m._dsw_draw();
    return this.m.HEAPU8.slice(at, at + 1024);
  }
  contrast() { return this.m._dsw_contrast(); }
  lines() {
    const out = [];
    for (let r = 0; r < this.m._dsw_rows(); r++) out.push(this.m.UTF8ToString(this.m._dsw_line(r)));
    return out;
  }
  keys() { return this.m._dsw_keys(); }
}
