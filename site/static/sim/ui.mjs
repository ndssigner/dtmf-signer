// The simulator on its page: draws the OLED, turns the on-screen buttons
// and the keyboard into the button held, runs the simulated phone or the
// microphone of this computer, draws the bench's links and lights the ones
// that are active. Everything it needs comes from #sim-data (JSON written by
// the site's build) and the page's markup.
import { Simulator, RATE } from "./core.mjs";
import { SimPhone } from "./phone.mjs";
import { orientationFor, linkPath } from "./bench.mjs";
import { activeChannels, statusKey } from "./activity.mjs";
import { startMic } from "./mic.mjs";

const SVG = "http://www.w3.org/2000/svg";
const MIC_BACKLOG = RATE * 2;   // the line never lags the microphone by more than this

const BTN = { left: 1, up: 2, ok: 3, down: 4, right: 5 };
const KEYS = { ArrowLeft: 1, ArrowUp: 2, Enter: 3, ArrowDown: 4, ArrowRight: 5 };
const ECHO_LINES = 200;

// A button must never stay held when the page loses the focus...
export function releaseOnBlur(sim, target) {
  target.addEventListener("blur", () => sim.releaseAll());
}

// ...or when the focus leaves the device, from any element inside it (blur
// does not bubble: a focused inner button would hide it).
export function releaseOnLeave(sim, device) {
  device.addEventListener("focusout", (e) => {
    if (!e.relatedTarget || !device.contains(e.relatedTarget)) sim.releaseAll();
  });
}

// Four times faster only while tones are playing and no button is down: the
// firmware counts a long press (a signature) and the wipe in its own time.
export function speedFor(sim, fast) {
  return fast && sim.queued > 0 && sim.idle ? 4 : 1;
}

// Leaving a mode: nothing left on the line, the microphone stopped; the
// signer keeps running (it is the same device, connected differently).
export function leaveMode({ sim, mic }) {
  sim.clearAudio();
  mic?.stop();
  return null;
}

// Where a link meets a port: the middle of the label's side that faces the
// other port, relative to the bench.
export function anchor(r, other, orient, box) {
  const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
  const ox = other.left + other.width / 2, oy = other.top + other.height / 2;
  const p = orient === "h" ? { x: ox > cx ? r.right : r.left, y: cy } : { x: cx, y: oy > cy ? r.bottom : r.top };
  return { x: Math.round(p.x - box.left), y: Math.round(p.y - box.top) };
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
    status.textContent = S["sim.unavailable"];
    return;
  }
  status.textContent = S["status.idle"];

  const sim = new Simulator(module);
  const root = doc.getElementById("sim");
  const canvas = root.querySelector(".sim-oled");
  const ctx = canvas.getContext("2d");
  const img = ctx.createImageData(128, 64);
  const screenText = root.querySelector(".screen-text");
  const echoPre = root.querySelector(".echo");
  const phStatus = root.querySelector(".ph-status");
  const sound = root.querySelector(".ph-sound");
  const fast = root.querySelector(".ph-fast");
  const phone = new SimPhone({ sim, U: UrTones, data, el: { out: root.querySelector(".ph-out"), status: phStatus } });
  const bench = root.querySelector(".bench");
  const svg = bench.querySelector(".links");
  const micBtn = root.querySelector(".mic-toggle");
  const micLevel = root.querySelector(".mic-level");
  let phoneNote = "";        // a passing message on the phone (no QR, reading…)
  let echo = [];
  let audio = null, nextStart = 0;
  let mode = bench.dataset.mode;
  let mic = null, micDbfs = -100;
  let paths = [];            // [{ el, channel, ports: [from, to] }]
  let last = performance.now(), lastLine = "", lastStatus = "", lastActive = "";

  // ---- the bench: orientation, links, modes ----
  function drawLinks() {
    bench.dataset.orient = orientationFor(bench.clientWidth);
    const box = bench.getBoundingClientRect();
    svg.replaceChildren();
    paths = [];
    for (const l of data.links[mode] ?? []) {
      const a = root.querySelector(`[data-port="${l.from}"]`), b = root.querySelector(`[data-port="${l.to}"]`);
      if (!a || !b) continue;
      const el = doc.createElementNS(SVG, "path");
      const ra = a.getBoundingClientRect(), rb = b.getBoundingClientRect(), o = bench.dataset.orient;
      el.setAttribute("d", linkPath(anchor(ra, rb, o, box), anchor(rb, ra, o, box), o));
      el.setAttribute("class", `link type-${a.dataset.type} ch-${l.channel}`);
      svg.append(el);
      paths.push({ el, channel: l.channel, ports: [a, b] });
    }
    lastActive = "";         // new paths: apply the activity again on the next frame
  }
  function switchMode(next) {
    if (next === mode) return;
    mic = leaveMode({ sim, mic });
    micDbfs = -100;
    if (micBtn) { micBtn.textContent = S["mic.listen"]; micBtn.setAttribute("aria-pressed", "false"); }
    mode = next;
    bench.dataset.mode = mode;
    for (const el of root.querySelectorAll("[data-modes]")) el.hidden = !el.dataset.modes.split(" ").includes(mode);
    for (const b of root.querySelectorAll(".sim-modes [data-mode]")) b.setAttribute("aria-selected", String(b.dataset.mode === mode));
    drawLinks();
  }
  for (const b of root.querySelectorAll(".sim-modes [data-mode]")) b.addEventListener("click", () => switchMode(b.dataset.mode));
  new ResizeObserver(() => drawLinks()).observe(bench);

  // ---- the microphone of this computer (mode "your-phone") ----
  micBtn?.addEventListener("click", async () => {
    if (mic) {
      mic = leaveMode({ sim, mic });
      micDbfs = -100;
      micBtn.textContent = S["mic.listen"];
      micBtn.setAttribute("aria-pressed", "false");
      return;
    }
    try {
      mic = await startMic({
        getUserMedia: navigator.mediaDevices?.getUserMedia?.bind(navigator.mediaDevices),
        AudioContext: globalThis.AudioContext, AudioWorkletNode: globalThis.AudioWorkletNode,
        onSamples: (x) => { if (sim.queued > MIC_BACKLOG) sim.clearAudio(); sim.queueAudio(x); },
        onLevel: (db) => { micDbfs = db; micLevel.value = Math.max(-80, db); },
      });
      micBtn.textContent = S["mic.stop"];
      micBtn.setAttribute("aria-pressed", "true");
    } catch (err) {
      micBtn.textContent = err.message === "mic:denied" ? "⚠ " + S["mic.denied"] : "⚠ " + S["mic.unsupported"];
    }
  });

  // ---- tooltips: a tap opens one, a tap elsewhere closes it ----
  doc.addEventListener("click", (e) => {
    const t = e.target.closest?.("[data-tip]");
    for (const o of root.querySelectorAll(".tip-open")) if (o !== t) o.classList.remove("tip-open");
    if (t && e.pointerType === "touch") t.classList.toggle("tip-open");
  });

  // Buttons: held while the pointer is down on them.
  for (const b of root.querySelectorAll("[data-btn]")) {
    const cls = BTN[b.dataset.btn];
    b.addEventListener("pointerdown", (e) => { b.setPointerCapture?.(e.pointerId); sim.press(cls); });
    for (const ev of ["pointerup", "pointercancel", "lostpointercapture"]) b.addEventListener(ev, () => sim.release(cls));
    b.addEventListener("contextmenu", (e) => e.preventDefault());
  }
  // Keyboard: arrows and Enter on the device, held while the key is down.
  const device = root.querySelector(".sim-device");
  device.addEventListener("keydown", (e) => {
    if (!(e.key in KEYS)) return;
    e.preventDefault();
    if (!e.repeat) sim.press(KEYS[e.key]);
  });
  // The key may come up anywhere once the focus has moved: listen on the page.
  doc.addEventListener("keyup", (e) => { if (e.key in KEYS) sim.release(KEYS[e.key]); });
  releaseOnLeave(sim, device);
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
  root.querySelector(".ph-seed").addEventListener("click", () => send(phone.sendSeed()));
  root.querySelector(".ph-psbt").addEventListener("click", () => send(phone.sendPsbt()));
  root.querySelector(".ph-scan").addEventListener("click", () => {
    const r = phone.scan();
    phoneNote = r === "no-qr" ? S["phone.no_qr"] : r === "waiting" ? S["phone.waiting"] : S["phone.got"];
  });

  function frame(now) {
    const dt = (now - last) * speedFor(sim, fast.checked && mode === "sim-phone");
    last = now;
    const { dirty, echo: lines } = sim.advance(dt);
    for (const l of lines) {
      if (l === "reboot") sim.clearAudio();
      phone.onEcho(l);
      echo.push(l === "reboot" ? "[reboot]" : l);
    }
    if (lines.length) {
      echo = echo.slice(-ECHO_LINES);
      echoPre.textContent = echo.join("\n");
    }
    // While scanning, the phone takes the signed PSBT as soon as it is there.
    if (phone.scanning && !sim.qrShown()) phone.scanning = false;
    if (phone.scanning && phoneNote === S["phone.waiting"] && phone.scan() === "ok") phoneNote = S["phone.got"];
    // Light what is active, only when it changes.
    const active = activeChannels({ mode, queued: sim.queued, listening: sim.listening(), qrShown: sim.qrShown(),
      scanning: phone.scanning, micOn: Boolean(mic), micDbfs });
    const key = [...active].sort().join(" ");
    if (key !== lastActive) {
      lastActive = key;
      for (const p of paths) {
        const on = active.has(p.channel);
        p.el.classList.toggle("active", on);
        for (const port of p.ports) port.classList.toggle("active", on);
      }
      for (const port of root.querySelectorAll('[data-port$=".qr"]')) port.classList.toggle("emit", active.has("qr"));
      for (const port of root.querySelectorAll('[data-port="signer.in"]')) port.classList.toggle("listening", active.has("listening"));
      root.querySelector(".yp-mic")?.classList.toggle("capturing", active.has("mic"));
      const st = S[statusKey(active)];
      if (st !== lastStatus) { status.textContent = st; lastStatus = st; }
    }
    if (dirty) {
      draw(ctx, img, sim.frame(), sim.contrast());
      screenText.textContent = sim.lines().join("\n").replace(/\s+$/gm, "");
    }
    const left = Math.ceil(sim.queued / RATE);
    const text = left > 0 ? S["phone.sending"].replace("{seconds}", String(left)) : phoneNote || S["phone.idle"];
    if (text !== lastLine) { phStatus.textContent = text; lastLine = text; }
    requestAnimationFrame(frame);
  }
  draw(ctx, img, sim.frame(), sim.contrast());
  drawLinks();
  requestAnimationFrame(frame);
}

if (typeof document !== "undefined" && document.getElementById("sim-data"))
  start(document, () => import("./dtmf-signer.mjs").then((f) => f.default()));
