// The simulator's board, without any page: a virtual clock that drives the
// firmware (ports/wasm) in 16 ms steps, the button held, the audio queued
// on its line-in, and the public echo it gives back. The page (ui.mjs) and
// the tests both drive it through this class.

export const RATE = 16000;        // line-in samples a second, as on the Pico
const STEP_MS = 16;
const MAX_ADVANCE_MS = 250;       // a paused or slow tab never makes the firmware jump
// A mouse click can be shorter than the firmware's 30 ms debounce, which is
// right for a real button but would lose clicks: a release waits until the
// button has been held this long.
const MIN_PRESS_MS = 60;
// And a new press waits for this much of no button after the last one, or
// two quick taps of the same button would reach it as one long press.
const MIN_GAP_MS = 48;

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
    this.heldMs = 0;
    this.releasing = false;
    this.gapMs = MIN_GAP_MS;       // time with no button since the last release
    this.pending = 0;              // a press waiting for that gap
    this.pendingUp = false;        // …already released again (a quick tap)
    this.carry = 0;
  }

  press(cls) {
    if (this.held || this.gapMs < MIN_GAP_MS) { this.pending = cls; this.pendingUp = false; return; }
    this.held = cls; this.heldMs = 0; this.releasing = false;
  }
  // Lets go of button cls (or of the latest press when cls is left out). A
  // release of another button than the one held or waiting changes nothing:
  // with two pressed at once, each release lets go of its own.
  release(cls) {
    if (this.pending && (cls === undefined || cls === this.pending)) this.pendingUp = true;
    else if (this.held && (cls === undefined || cls === this.held)) {
      if (this.heldMs >= MIN_PRESS_MS) { this.held = 0; this.gapMs = 0; } else this.releasing = true;
    }
  }
  // Every button up, now, waiting presses dropped: when the page loses the focus.
  releaseAll() {
    if (this.held) this.gapMs = 0;
    this.held = 0; this.releasing = false;
    this.pending = 0; this.pendingUp = false;
  }
  get idle() { return !this.held && !this.pending; }
  // After each step: ends a release that waited, starts a press that waited.
  settle(ms) {
    if (this.held) {
      this.heldMs += ms;
      if (this.releasing && this.heldMs >= MIN_PRESS_MS) { this.held = 0; this.releasing = false; this.gapMs = 0; }
      return;
    }
    this.gapMs += ms;
    if (this.pending && this.gapMs >= MIN_GAP_MS) {
      const cls = this.pending, up = this.pendingUp;
      this.pending = 0; this.pendingUp = false;
      this.press(cls);
      if (up) this.release();
    }
  }
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
      this.settle(ms);
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
  listening() { return this.m._dsw_listening() === 1; }
  qrShown() { return this.m._dsw_qr_shown() === 1; }
}
