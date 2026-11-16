// The simulator on its page: draws the OLED, turns the on-screen buttons
// and the keyboard into the button held, runs the simulated wallet or the
// microphone of this computer, draws the bench's links and lights the ones
// that are active. Everything it needs comes from #sim-data (JSON written by
// the site's build) and the page's markup.
import { Simulator, RATE } from "./core.mjs";
import { SimWallet } from "./sim-wallet.mjs";
import { seedCheck, psbtCheck } from "./checks.mjs";
import { orientationFor, linkPath } from "./bench.mjs";
import { activeChannels, statusKey, LedFlash } from "./activity.mjs";
import { startMic, MicSwitch } from "./mic.mjs";

const SVG = "http://www.w3.org/2000/svg";
const MIC_BACKLOG = RATE * 2;   // the line never lags the microphone by more than this

const BTN = { left: 1, up: 2, ok: 3, down: 4, right: 5 };
const KEYS = { ArrowLeft: 1, ArrowUp: 2, Enter: 3, ArrowDown: 4, ArrowRight: 5 };
const ECHO_LINES = 200;

// The on-screen buttons to draw down for the button class the firmware is
// given (whoever pressed it: the mouse, the keyboard or a script).
export function buttonsDown(cls) {
  return Object.keys(BTN).filter((b) => BTN[b] === cls);
}

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
export function leaveMode({ sim, mic, speaker }) {
  sim.clearAudio();
  mic?.stop();
  speaker?.close();         // tones already scheduled on the speakers stop too
  return null;
}

// A phone: a narrow screen and a touch pointer, both (a touch laptop or a
// narrow desktop window is not one).
export function isPhone({ width, coarse }) {
  return width < 600 && coarse;
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
  // On a phone, before anything else: a computer or a tablet suits it better.
  const note = doc.querySelector?.(".sim-mobile");
  const view = doc.defaultView;
  if (note && view) {
    let closed = false;
    const update = () => {
      // The screen's width too: a page can be laid out wider than the phone.
      const width = Math.min(view.innerWidth, view.screen?.width ?? Infinity);
      note.hidden = closed || !isPhone({ width, coarse: view.matchMedia?.("(pointer: coarse)").matches ?? false });
    };
    note.querySelector(".sim-mobile-close")?.addEventListener("click", () => { closed = true; update(); });
    view.addEventListener?.("resize", update);
    update();
  }
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
  const wdStatus = root.querySelector(".wd-status");
  const sound = root.querySelector(".wd-sound");
  const fast = root.querySelector(".wd-fast");
  const wallet = new SimWallet({ sim, U: UrTones, sha256: UrTonesCrypto.sha256, S, data, el: { out: root.querySelector(".wd-out"), status: wdStatus } });
  const bench = root.querySelector(".bench");
  const svg = bench.querySelector(".links");
  const micBtn = root.querySelector(".mic-toggle");
  const micLevel = root.querySelector(".mic-level");
  let walletNote = "";        // a passing message on the wallet (no QR, reading…)
  const btnEls = [...root.querySelectorAll("[data-btn]")];
  let lastCls = -1;
  const ledEl = root.querySelector(".sim-led"), led = new LedFlash();
  let ledOn = false;
  let checkText = "", lastCheck = "", lastWarn = null;   // what to check on the signer
  const wdCheck = root.querySelector(".wd-check"), wdWarn = root.querySelector(".wd-warn");
  let echo = [];
  let audio = null, nextStart = 0;
  let mode = bench.dataset.mode;
  let micDbfs = -100;
  // The microphone of this computer: the audio context is made in the click
  // itself, so the browser lets it run.
  const micSwitch = new MicSwitch(() => startMic({
    getUserMedia: navigator.mediaDevices?.getUserMedia?.bind(navigator.mediaDevices),
    audioContext: globalThis.AudioContext ? new AudioContext() : null,
    AudioWorkletNode: globalThis.AudioWorkletNode,
    onSamples: (x) => { if (sim.queued > MIC_BACKLOG) sim.clearAudio(); sim.queueAudio(x); },
    onLevel: (db) => { micDbfs = db; micLevel.value = Math.max(-80, db); },
  }));
  function micShown() {
    if (!micBtn) return;
    micBtn.disabled = micSwitch.starting;
    micBtn.textContent = micSwitch.on ? S["mic.stop"] : S["mic.listen"];
    micBtn.setAttribute("aria-pressed", String(micSwitch.on));
  }
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
    leaveMode({ sim, mic: { stop: () => micSwitch.cancel() }, speaker: audio });
    audio = null; nextStart = 0;
    micDbfs = -100;
    micShown();
    mode = next;
    bench.dataset.mode = mode;
    for (const el of root.querySelectorAll("[data-modes]")) el.hidden = !el.dataset.modes.split(" ").includes(mode);
    for (const b of root.querySelectorAll(".sim-modes [data-mode]")) b.setAttribute("aria-selected", String(b.dataset.mode === mode));
    drawLinks();
  }
  for (const b of root.querySelectorAll(".sim-modes [data-mode]")) b.addEventListener("click", () => switchMode(b.dataset.mode));
  new ResizeObserver(() => drawLinks()).observe(bench);

  // ---- the microphone of this computer (mode "your-wallet") ----
  micBtn?.addEventListener("click", () => {
    if (!micSwitch.on) micDbfs = -100;
    const p = micSwitch.toggle();
    micShown();
    p.then(micShown, (err) => {
      micShown();
      micBtn.textContent = "⚠ " + S[err.message === "mic:denied" ? "mic.denied" : "mic.unsupported"];
    });
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
  // Stop: nothing left on the line, the speakers quiet.
  function stopSending() {
    leaveMode({ sim, speaker: audio });
    audio = null; nextStart = 0;
  }
  root.querySelector(".wd-seed").addEventListener("click", () => {
    send(wallet.sendSeed());
    walletNote = ""; checkText = seedCheck(wallet.pair(), data.pin, S).join("\n");
  });
  root.querySelector(".wd-psbt").addEventListener("click", () => {
    send(wallet.sendPsbt());
    walletNote = ""; checkText = psbtCheck(wallet.pair(), S).join("\n");
  });
  root.querySelector(".wd-stop").addEventListener("click", () => { stopSending(); walletNote = ""; });
  root.querySelector(".wd-net").addEventListener("change", (e) => {
    stopSending();
    wallet.setNetwork(e.target.value);
    walletNote = ""; checkText = "";
  });
  root.querySelector(".wd-scan").addEventListener("click", () => {
    const r = wallet.scan();
    walletNote = { "no-qr": S["wallet.no_qr"], waiting: S["wallet.waiting"], ok: S["wallet.got"] }[r] ?? "";
    checkText = wallet.last?.lines.join("\n") ?? "";
  });

  function frame(now) {
    const dt = (now - last) * speedFor(sim, fast.checked && mode === "sim-wallet");
    last = now;
    const { dirty, echo: lines } = sim.advance(dt);
    for (const l of lines) {
      if (l === "reboot") sim.clearAudio();
      wallet.onEcho(l);
      echo.push(l === "reboot" ? "[reboot]" : l);
    }
    if (lines.length) {
      echo = echo.slice(-ECHO_LINES);
      echoPre.textContent = echo.join("\n");
    }
    // While scanning, the wallet takes the signed PSBT as soon as it is there.
    if (wallet.scanning && !sim.qrShown()) wallet.scanning = false;
    // While scanning, the wallet reads what the QR holds as soon as it is there.
    if (wallet.scanning && walletNote === S["wallet.waiting"]) {
      const r = wallet.scan();
      if (r !== "waiting") { walletNote = r === "ok" ? S["wallet.got"] : ""; checkText = wallet.last?.lines.join("\n") ?? ""; }
    }
    // The buttons: down while the firmware is given them.
    if (sim.cls !== lastCls) {
      lastCls = sim.cls;
      const down = buttonsDown(lastCls);
      for (const b of btnEls) b.classList.toggle("down", down.includes(b.dataset.btn));
    }
    // The board's LED: a flash per tone heard, as on the Pico.
    const lit = led.update(sim.keys(), now);
    if (lit !== ledOn && ledEl) { ledEl.classList.toggle("on", lit); ledOn = lit; }
    // The signer's network against the wallet's: a mark beside "Send PSBT".
    wallet.watch();
    const warn = wallet.mismatch();
    if (warn !== lastWarn) { wdWarn.hidden = !warn; lastWarn = warn; }
    if (checkText !== lastCheck) { wdCheck.textContent = checkText; lastCheck = checkText; }
    // Light what is active, only when it changes.
    const active = activeChannels({ mode, queued: sim.queued, listening: sim.listening(), qrShown: sim.qrShown(),
      scanning: wallet.scanning, micOn: micSwitch.on, micDbfs });
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
      root.querySelector(".yw-mic")?.classList.toggle("capturing", active.has("mic"));
      const st = S[statusKey(active)];
      if (st !== lastStatus) { status.textContent = st; lastStatus = st; }
    }
    if (dirty) {
      draw(ctx, img, sim.frame(), sim.contrast());
      screenText.textContent = sim.lines().join("\n").replace(/\s+$/gm, "");
    }
    const left = Math.ceil(sim.queued / RATE);
    const text = left > 0 ? S["wallet.sending"].replace("{seconds}", String(left)) : walletNote || S["wallet.idle"];
    if (text !== lastLine) { wdStatus.textContent = text; lastLine = text; }
    requestAnimationFrame(frame);
  }
  draw(ctx, img, sim.frame(), sim.contrast());
  drawLinks();
  requestAnimationFrame(frame);
}

if (typeof document !== "undefined" && document.getElementById("sim-data"))
  start(document, () => import("./dtmf-signer.mjs").then((f) => f.default()));
