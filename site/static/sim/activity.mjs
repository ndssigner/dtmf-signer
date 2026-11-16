// What is happening on the bench, from the simulator's state: the channels
// to light up (only while active, and only in their mode), and the one line
// of status that names the most telling of them.
import { MIC_ACTIVE_DBFS } from "./mic.mjs";

export function activeChannels({ mode, queued, listening, qrShown, scanning, micOn, micDbfs }) {
  const on = new Set(["power"]);
  if (mode === "sim-wallet" && queued > 0) on.add("line-in");
  if (mode === "your-wallet" && micOn) {
    on.add("mic");
    if (listening && micDbfs > MIC_ACTIVE_DBFS) on.add("line-in");
  }
  if (listening) on.add("listening");
  if (qrShown) on.add("qr");
  // Only the simulated wallet's camera: the page cannot see yours.
  if (qrShown && scanning && mode === "sim-wallet") on.add("camera");
  return on;
}

const ORDER = [["camera", "status.camera"], ["line-in", "status.line_in"], ["qr", "status.qr"],
  ["listening", "status.listening"], ["mic", "status.mic"]];

export function statusKey(active) {
  for (const [ch, key] of ORDER) if (active.has(ch)) return key;
  return "status.idle";
}

// The board's own LED, as ports/pico/main.c drives it: a flash each time the
// count of keys heard grows. The firmware's flash lasts 15 ms; shown for
// LED_SHOW_MS (about two frames) so a screen shows it, still going out
// between tones (one every 60 ms by cable).
export const LED_SHOW_MS = 30;
export class LedFlash {
  constructor() { this.keys = 0; this.offAt = -Infinity; }
  update(keys, now) {
    if (keys !== this.keys) {
      this.keys = keys;
      if (keys !== 0) this.offAt = now + LED_SHOW_MS;
    }
    return now < this.offAt;
  }
}
