// The simulator on its page: draws the OLED, turns the on-screen buttons
// and the keyboard into the button held, runs the test wallet, plays the
// tones, and shows the echo and the signed PSBT. Everything it needs comes
// from #sim-data (JSON written by the site's build) and the page's markup.
import { Simulator, RATE } from "./core.mjs";
import { seedAudio, psbtAudio, SignedCollector } from "./wallet.mjs";

const BTN = { left: 1, up: 2, ok: 3, down: 4, right: 5 };
const KEYS = { ArrowLeft: 1, ArrowUp: 2, Enter: 3, ArrowDown: 4, ArrowRight: 5 };
const ECHO_LINES = 200;

// A button must never stay held when the page loses the focus.
export function releaseOnBlur(sim, target) {
  target.addEventListener("blur", () => sim.release());
}

// The 128 × 64 framebuffer (8 pages of 128 bytes, bit 0 on top), lit pixels
// pale blue on black, brighter with the contrast the firmware asks for.
function draw(ctx, img, fb, contrast) {
  const k = 0.45 + 0.55 * (contrast / 255);
  const on = [Math.round(140 * k), Math.round(210 * k), Math.round(255 * k)];
  for (let y = 0; y < 64; y++) {
    for (let x = 0; x < 128; x++) {
      const lit = (fb[(y >> 3) * 128 + x] >> (y & 7)) & 1;
      const i = (y * 128 + x) * 4;
      img.data[i] = lit ? on[0] : 0;
      img.data[i + 1] = lit ? on[1] : 0;
      img.data[i + 2] = lit ? on[2] : 0;
      img.data[i + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
}

export async function start(doc, loadModule) {
  const data = JSON.parse(doc.getElementById("sim-data").textContent);
  const S = data.strings;
  const status = doc.querySelector(".sim-status");
  let module;
  try {
    if (typeof WebAssembly === "undefined" || typeof UrTones === "undefined") throw new Error("unsupported");
    module = await loadModule();
  } catch {
    status.textContent = S.unavailable;
    return;
  }
  status.textContent = "";
  status.hidden = true;

  const sim = new Simulator(module);
  const root = doc.getElementById("sim");
  const canvas = root.querySelector(".sim-oled");
  const ctx = canvas.getContext("2d");
  const img = ctx.createImageData(128, 64);
  const screenText = root.querySelector(".screen-text");
  const echoPre = root.querySelector(".echo");
  const line = root.querySelector(".sim-line");
  const result = root.querySelector(".sim-result");
  const signedBox = root.querySelector(".sim-signed");
  const sound = root.querySelector(".sim-sound");
  const fast = root.querySelector(".sim-fast");
  let collector = new SignedCollector();
  let echo = [];
  let audio = null, nextStart = 0;

  // Buttons: held while the pointer is down on them.
  for (const b of root.querySelectorAll("[data-btn]")) {
    const cls = BTN[b.dataset.btn];
    b.addEventListener("pointerdown", (e) => { b.setPointerCapture?.(e.pointerId); sim.press(cls); });
    for (const ev of ["pointerup", "pointercancel", "lostpointercapture"]) b.addEventListener(ev, () => sim.release());
    b.addEventListener("contextmenu", (e) => e.preventDefault());
  }
  // Keyboard: arrows and Enter on the device, held while the key is down.
  const device = root.querySelector(".sim-device");
  device.addEventListener("keydown", (e) => {
    if (!(e.key in KEYS)) return;
    e.preventDefault();
    if (!e.repeat) sim.press(KEYS[e.key]);
  });
  device.addEventListener("keyup", (e) => { if (e.key in KEYS) sim.release(); });
  releaseOnBlur(sim, device);
  releaseOnBlur(sim, doc.defaultView);

  // The tones also go to the speakers, at the pace the firmware hears them.
  function play(samples) {
    if (!sound.checked || fast.checked) return;
    audio ??= new AudioContext();
    const buf = audio.createBuffer(1, samples.length, RATE);
    const ch = buf.getChannelData(0);
    for (let i = 0; i < samples.length; i++) ch[i] = samples[i] / 32768;
    const src = audio.createBufferSource();
    src.buffer = buf;
    src.connect(audio.destination);
    nextStart = Math.max(audio.currentTime + 0.05, nextStart);
    src.start(nextStart);
    nextStart += buf.duration;
  }
  function send(samples) {
    if (sim.queued === 0 && audio) nextStart = 0;
    sim.queueAudio(samples);
    play(samples);
  }
  fast.addEventListener("change", () => { if (fast.checked && audio) { audio.close(); audio = null; nextStart = 0; } });
  root.querySelector(".sim-send-seed").addEventListener("click", () => send(seedAudio(UrTones, data.seed, data.pin)));
  root.querySelector(".sim-send-psbt").addEventListener("click", () => send(psbtAudio(UrTones, data.psbt)));

  let last = performance.now(), lastLine = "";
  function frame(now) {
    const dt = (now - last) * (fast.checked ? 4 : 1);
    last = now;
    const { dirty, echo: lines } = sim.advance(dt);
    for (const l of lines) {
      if (l === "reboot") { collector = new SignedCollector(); sim.clearAudio(); }
      collector.push(l);
      echo.push(l);
    }
    if (lines.length) {
      echo = echo.slice(-ECHO_LINES);
      echoPre.textContent = echo.join("\n");
      const r = collector.result;
      if (r && signedBox.value !== r.base64) { signedBox.value = r.base64; result.hidden = false; }
    }
    if (dirty) {
      draw(ctx, img, sim.frame(), sim.contrast());
      screenText.textContent = sim.lines().join("\n").replace(/\s+$/gm, "");
    }
    const left = Math.ceil(sim.queued / RATE);
    const text = left > 0 ? S.line_busy.replace("{seconds}", String(left)) : S.line_idle;
    if (text !== lastLine) { line.textContent = text; lastLine = text; }
    requestAnimationFrame(frame);
  }
  draw(ctx, img, sim.frame(), sim.contrast());
  requestAnimationFrame(frame);
}

if (typeof document !== "undefined" && document.getElementById("sim-data"))
  start(document, () => import("./dtmf-signer.mjs").then((f) => f.default()));
