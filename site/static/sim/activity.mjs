// What is happening on the bench, from the simulator's state: the channels
// to light up (only while active, and only in their mode), and the one line
// of status that names the most telling of them.
import { MIC_ACTIVE_DBFS } from "./mic.mjs";

export function activeChannels({ mode, queued, listening, qrShown, scanning, micOn, micDbfs }) {
  const on = new Set(["power"]);
  if (mode === "sim-phone" && queued > 0) on.add("line-in");
  if (mode === "your-phone" && micOn) {
    on.add("mic");
    if (listening && micDbfs > MIC_ACTIVE_DBFS) on.add("line-in");
  }
  if (listening) on.add("listening");
  if (qrShown) on.add("qr");
  // Only the simulated phone's camera: the page cannot see yours.
  if (qrShown && scanning && mode === "sim-phone") on.add("camera");
  return on;
}

const ORDER = [["camera", "status.camera"], ["line-in", "status.line_in"], ["qr", "status.qr"],
  ["listening", "status.listening"], ["mic", "status.mic"]];

export function statusKey(active) {
  for (const [ch, key] of ORDER) if (active.has(ch)) return key;
  return "status.idle";
}
