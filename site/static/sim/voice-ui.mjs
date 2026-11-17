// The Centralita's simulator on its page: the telephone (keypad, handset,
// R key, a Type code button that dials the wallet's keypad code as a
// person's fingers would), its earpiece on the speakers of this computer, the simulated
// wallet held to the handset, the bench's links and what is active, the
// board's LED, and what the signer says as text. Everything it needs comes
// from #sim-data (written by the site's build) and the page's markup; the
// firmware does the rest (voice-core.mjs).
import { VoiceSimulator, RATE } from "./voice-core.mjs";
import { SimWallet } from "./sim-wallet.mjs";
import { seedCheck, psbtCheck, fill } from "./checks.mjs";
import { orientationFor, linkPath } from "./bench.mjs";
import { LedFlash } from "./activity.mjs";
import { anchor, mobileNote, tapTips } from "./ui.mjs";
import { startMic, MicSwitch, MIC_ACTIVE_DBFS } from "./mic.mjs";

const SVG = "http://www.w3.org/2000/svg";
const ECHO_LINES = 200;
const FLASH_MS = 500;          // the R key: a short hang-up, as a telephone's flash
const SAID_MS = 600;           // this long quiet: what was said is over
const KEYS = "0123456789*#";
// Your wallet: the microphone of this computer never runs the handset this
// far behind (what is older is dropped, as a real handset would not wait).
export const MIC_BACKLOG_MS = 2000;

// What the signer says, as text, from its public echo: "say <id>" in the
// language it speaks (phrases[lang][id]; each language of the list in its
// own), numbers and characters as they are, a PIN character as a dot
// (never the character), its tones as one mark. S: the page's texts.
export class Transcript {
  constructor(phrases, S, langs = Object.keys(phrases)) {
    Object.assign(this, { phrases, S, langs });
    this.lang = "en";
    this.words = [];
    this.listed = 0;           // in the language list: the next pack to name
  }
  // One echo line; returns the sentence ended by it, if any.
  push(line) {
    const sp = line.indexOf(" ");
    const head = sp < 0 ? line : line.slice(0, sp), rest = sp < 0 ? "" : line.slice(sp + 1);
    if (head === "state") {
      if (rest === "lang") this.listed = 0;
      return this.end();
    }
    if (head === "lang") { this.lang = rest; return null; }
    if (head === "num" || head === "chars") this.words.push(rest);
    else if (head === "tones") { if (this.words.at(-1) !== this.S["say.tones"]) this.words.push(this.S["say.tones"]); }
    else if (head === "say") {
      if (rest === "pin-char") this.words.push(this.S["say.pin_char"]);
      else if (/^c_.$/.test(rest)) this.words.push(rest.slice(2).toUpperCase());
      else if (rest === "lang_press") this.words.push(this.phrases[this.langs[this.listed++]]?.lang_press ?? "");
      else this.words.push(this.phrases[this.lang]?.[rest] ?? this.phrases.en?.[rest] ?? "");
    }
    return null;
  }
  // The sentence so far, and a new one begun.
  end() {
    const t = this.words.filter(Boolean).join(" ");
    this.words = [];
    return t || null;
  }
}

// What is active on the bench, and the one status line that says the most.
// Simulated wallet: the air lights as it plays or listens. Your wallet: the
// microphone of this computer is the handset's (lit with sound above the
// threshold), its speakers the earpiece (lit while the signer speaks).
export function voiceActivity({ mode = "sim-wallet", offHook, micQueued, keysQueued, speaking, walletListening, micOn = false, micDbfs = -100 }) {
  const on = new Set(["power"]);
  if (!offHook) return { on, status: "status.on_hook" };
  if (mode === "your-wallet") {
    const hearing = micOn && micDbfs > MIC_ACTIVE_DBFS;
    if (speaking || keysQueued > 0 || hearing) on.add("line");
    if (hearing) on.add("air-in");
    if (speaking) on.add("air-out");
    const status = micOn ? "status.mic" : keysQueued > 0 ? "status.keypad" : speaking ? "status.speaking" : "status.listening_line";
    return { on, status };
  }
  if (speaking || keysQueued > 0 || micQueued > 0) on.add("line");
  if (micQueued > 0) on.add("air-in");
  if (speaking && walletListening) on.add("air-out");
  const status = micQueued > 0 ? "status.air_in" : on.has("air-out") ? "status.air_out"
    : keysQueued > 0 ? "status.keypad" : speaking ? "status.speaking" : "status.listening_line";
  return { on, status };
}

// What the speakers of this computer play: the earpiece and, with the
// simulated wallet, what it plays aloud next to the handset. With your
// wallet the "room" is the microphone itself: played back, it would howl.
export function speakerMix(mode, earpiece, room) {
  const mix = new Int16Array(earpiece.length);
  for (let i = 0; i < mix.length; i++) mix[i] = Math.max(-32768, Math.min(32767, earpiece[i] + (mode === "sim-wallet" ? room[i] : 0)));
  return mix;
}
// ⏩ belongs to the simulated wallet: it never mutes your wallet's speakers.
export const speakersOn = ({ mode, sound, fast }) => sound && !(mode === "sim-wallet" && fast);

// Leaving a mode: the microphone stopped, nothing left at the handset's
// microphone, the dialling cut; the signer keeps running (the same device).
export function leaveVoiceMode({ vs, mic }) {
  vs.clearMic();
  vs.stopDialling();
  mic?.stop();
}

// Four times faster only while the wallet plays or listens: the firmware's
// clock goes with it, so nothing it times changes.
export const voiceSpeed = ({ micQueued, walletListening }, fast) => (fast && (micQueued > 0 || walletListening) ? 4 : 1);

const fromBase64 = (b64) => Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));

export async function start(doc, loadModule, loadPack) {
  const data = JSON.parse(doc.getElementById("sim-data").textContent);
  mobileNote(doc);
  const S = data.strings;
  const status = doc.querySelector(".sim-status");
  let module, pack = null;
  try {
    if (typeof WebAssembly === "undefined" || typeof UrTones === "undefined") throw new Error("unsupported");
    module = await loadModule();
    if (data.pack) pack = fromBase64(await loadPack(data.pack));
  } catch {
    status.textContent = S["sim.unavailable"];
    return;
  }
  status.textContent = S["status.on_hook"];

  const root = doc.getElementById("sim");
  const vs = new VoiceSimulator(module, doc.documentElement.lang, pack);
  const wallet = new SimWallet({ sim: vs, U: UrTones, sha256: UrTonesCrypto.sha256, S, data, el: { out: root.querySelector(".wd-out") } });
  const transcript = new Transcript(data.phrases, S, data.pack ? ["en", data.pack] : ["en"]);
  const bench = root.querySelector(".bench"), svg = bench.querySelector(".links");
  const caption = root.querySelector(".ph-caption"), echoPre = root.querySelector(".echo");
  const hookBtn = root.querySelector(".ph-hook"), hookLabel = root.querySelector(".ph-hook-label");
  const sound = root.querySelector(".ph-sound"), fast = root.querySelector(".wd-fast");
  const typeBtn = root.querySelector(".ph-type");
  let typing = false;
  const wdStatus = root.querySelector(".wd-status"), wdCheck = root.querySelector(".wd-check"), wdWarn = root.querySelector(".wd-warn");
  const listenBtn = root.querySelector(".wd-listen");
  const ledEl = root.querySelector(".sim-led"), led = new LedFlash();
  let ledOn = false, echo = [], paths = [], lastActive = "", lastStatus = "", lastLine = "", lastCheck = "", lastWarn = null;
  let walletNote = "", checkText = "", signerNet = null, flashUntil = null, said = false;
  let audio = null, nextStart = 0;
  let mode = bench.dataset.mode;
  tapTips(doc, root);

  // ---- your wallet: the microphone of this computer is the handset's ----
  const micBtn = root.querySelector(".mic-toggle"), micLevel = root.querySelector(".mic-level");
  let micDbfs = -100;
  const micSwitch = new MicSwitch(() => startMic({
    getUserMedia: navigator.mediaDevices?.getUserMedia?.bind(navigator.mediaDevices),
    audioContext: globalThis.AudioContext ? new AudioContext() : null,
    AudioWorkletNode: globalThis.AudioWorkletNode,
    onSamples: (x) => { if (vs.micQueued > RATE * MIC_BACKLOG_MS / 1000) vs.clearMic(); vs.queueMic(x); },
    onLevel: (db) => { micDbfs = db; if (micLevel) micLevel.value = Math.max(-80, db); },
  }));
  function micShown() {
    if (!micBtn) return;
    micBtn.disabled = micSwitch.starting;
    micBtn.textContent = micSwitch.on ? S["mic.stop"] : S["mic.listen"];
    micBtn.setAttribute("aria-pressed", String(micSwitch.on));
  }
  micBtn?.addEventListener("click", () => {
    ensureAudio();
    if (!micSwitch.on) micDbfs = -100;
    const p = micSwitch.toggle();
    micShown();
    p.then(micShown, (err) => {
      micShown();
      micBtn.textContent = "⚠ " + S[err.message === "mic:denied" ? "mic.denied" : "mic.unsupported"];
    });
  });

  // ---- the modes: the simulated wallet, or yours ----
  function switchMode(next) {
    if (next === mode) return;
    leaveVoiceMode({ vs, mic: { stop: () => micSwitch.cancel() } });
    showTyping(false);
    if (wallet.listening) wallet.stopListening();
    walletNote = ""; micDbfs = -100;
    micShown();
    mode = next;
    bench.dataset.mode = mode;
    for (const el of root.querySelectorAll("[data-modes]")) el.hidden = !el.dataset.modes.split(" ").includes(mode);
    for (const b of root.querySelectorAll(".sim-modes [data-mode]")) b.setAttribute("aria-selected", String(b.dataset.mode === mode));
    drawLinks();
  }
  for (const b of root.querySelectorAll(".sim-modes [data-mode]")) b.addEventListener("click", () => switchMode(b.dataset.mode));

  // ---- the bench: links drawn port to port (top and bottom ports meet
  // their pair vertically, whatever the layout) ----
  function drawLinks() {
    bench.dataset.orient = orientationFor(bench.clientWidth);
    const box = bench.getBoundingClientRect();
    svg.replaceChildren();
    paths = [];
    for (const l of data.links[mode] ?? []) {
      const a = root.querySelector(`[data-port="${l.from}"]`), b = root.querySelector(`[data-port="${l.to}"]`);
      if (!a || !b) continue;
      const stacked = [a, b].some((p) => p.classList.contains("edge-top") || p.classList.contains("edge-bottom"));
      const o = stacked ? "v" : bench.dataset.orient;
      const ra = a.getBoundingClientRect(), rb = b.getBoundingClientRect();
      const el = doc.createElementNS(SVG, "path");
      el.setAttribute("d", linkPath(anchor(ra, rb, o, box), anchor(rb, ra, o, box), o));
      el.setAttribute("class", `link type-${a.dataset.type} ch-${l.channel}${l.external ? " external" : ""}`);
      svg.append(el);
      paths.push({ el, channel: l.channel, ports: [a, b] });
    }
    lastActive = "";
  }
  new ResizeObserver(() => drawLinks()).observe(bench);

  // ---- the telephone ----
  function showHook() {
    hookBtn.setAttribute("aria-pressed", String(vs.offHook));
    hookLabel.textContent = S[vs.offHook ? "phone.hang" : "phone.lift"];
    root.querySelector(".blk-phone")?.classList.toggle("off-hook", vs.offHook);
    if (typeBtn) typeBtn.disabled = !vs.offHook;
    if (!vs.offHook) showTyping(false);   // the handset down cuts it short
  }
  function showTyping(on) {
    typing = on;
    typeBtn?.setAttribute("aria-pressed", String(on));
  }
  // The speakers of this computer: made in a click, so the browser lets it play.
  const ensureAudio = () => { if (!audio && globalThis.AudioContext) audio = new AudioContext(); };
  hookBtn.addEventListener("click", () => {
    ensureAudio();
    flashUntil = null;
    vs.hook(!vs.offHook);
    showHook();
  });
  root.querySelector(".ph-flash").addEventListener("click", () => {
    if (!vs.offHook) return;
    vs.hook(false);
    flashUntil = vs.clock + FLASH_MS;
    showHook();
  });
  const keyEls = new Map([...root.querySelectorAll("[data-key]")].map((b) => [b.dataset.key, b]));
  function press(k) {
    ensureAudio();
    vs.pressKey(k);
    const b = keyEls.get(k);
    b?.classList.add("down");
    setTimeout(() => b?.classList.remove("down"), 120);
  }
  for (const [k, b] of keyEls) b.addEventListener("click", () => press(k));
  // Type code: the wallet's keypad code (the public test seed) typed on this
  // keypad, key by key at a person's pace; a second click stops it.
  typeBtn?.addEventListener("click", () => {
    if (!vs.offHook) return;
    ensureAudio();
    if (typing) { vs.stopDialling(); showTyping(false); return; }
    vs.dial(wallet.keypadCode());
    showTyping(true);
  });
  root.querySelector(".blk-phone").addEventListener("keydown", (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey || !KEYS.includes(e.key) || e.key.length !== 1) return;
    e.preventDefault();
    if (!e.repeat) press(e.key);
  });
  function play(samples) {
    if (!audio || !speakersOn({ mode, sound: sound.checked, fast: fast.checked })) return;
    const buf = audio.createBuffer(1, samples.length, RATE);
    const ch = buf.getChannelData(0);
    for (let i = 0; i < samples.length; i++) ch[i] = samples[i] / 32768;
    const src = audio.createBufferSource();
    src.buffer = buf;
    src.connect(audio.destination);
    if (nextStart < audio.currentTime + 0.02) nextStart = audio.currentTime + 0.08;
    src.start(nextStart);
    nextStart += buf.duration;
  }

  // ---- the simulated wallet, next to the handset ----
  wallet.setNetwork(root.querySelector(".wd-net").value);
  const stop = () => { vs.clearMic(); walletNote = ""; };
  root.querySelector(".wd-seed").addEventListener("click", () => {
    ensureAudio();
    vs.queueMic(wallet.sendSeedAir());
    walletNote = ""; checkText = seedCheck(wallet.pair(), data.pin, S).join("\n");
  });
  root.querySelector(".wd-keypad").addEventListener("click", () => {
    walletNote = ""; checkText = fill(S["wallet.keypad_code"], { code: wallet.keypadCode() });
  });
  root.querySelector(".wd-psbt").addEventListener("click", () => {
    ensureAudio();
    vs.queueMic(wallet.sendPsbtAir());
    walletNote = ""; checkText = psbtCheck(wallet.pair(), S).join("\n");
  });
  root.querySelector(".wd-stop").addEventListener("click", stop);
  root.querySelector(".wd-net").addEventListener("change", (e) => { stop(); wallet.setNetwork(e.target.value); checkText = ""; });
  function showListen() { listenBtn.setAttribute("aria-pressed", String(wallet.listening)); }
  listenBtn.addEventListener("click", () => {
    if (wallet.listening) { wallet.stopListening(); walletNote = ""; } else { wallet.listen(); walletNote = S["wallet.listening"]; checkText = ""; }
    showListen();
  });

  let last = performance.now();
  function frame(now) {
    const speaking = vs.quietMs < 200;
    const sim = mode === "sim-wallet";
    const dt = (now - last) * (sim ? voiceSpeed({ micQueued: vs.micQueued, walletListening: wallet.listening }, fast.checked) : 1);
    last = now;
    const { echo: lines, earpiece, room } = vs.advance(dt);
    if (flashUntil !== null && vs.clock >= flashUntil) { flashUntil = null; vs.hook(true); showHook(); }
    if (typing && vs.keysQueued === 0) showTyping(false);
    // The speakers: the earpiece, and the wallet playing aloud next to it.
    play(speakerMix(mode, earpiece, room));
    // The wallet hears the earpiece, never the echo (yours hears the speakers).
    const heard = sim ? wallet.hear(earpiece) : null;
    if (heard === "ok" || heard === "other") {
      walletNote = S["wallet.got"]; checkText = wallet.last?.lines.join("\n") ?? ""; showListen();
    } else if (heard === "listening" && wallet.ear.parts) walletNote = fill(S["wallet.heard"], { parts: wallet.ear.parts });
    for (const l of lines) {
      if (l === "say network_now_main") signerNet = "mainnet";
      if (l === "say network_now_test") signerNet = "testnet";
      const t = transcript.push(l);
      if (t) caption.textContent = t;
      echo.push(l);
    }
    if (lines.length) { said = true; echo = echo.slice(-ECHO_LINES); echoPre.textContent = echo.join("\n"); }
    // Once the signer has been quiet a moment, what it said is complete.
    if (said && vs.quietMs >= SAID_MS) {
      said = false;
      const t = transcript.end();
      if (t) caption.textContent = t;
    }
    const lit = led.update(vs.keys(), now);
    if (lit !== ledOn && ledEl) { ledEl.classList.toggle("on", lit); ledOn = lit; }
    const warn = signerNet !== null && signerNet !== wallet.net;
    if (warn !== lastWarn) { wdWarn.hidden = !warn; lastWarn = warn; }
    if (checkText !== lastCheck) { wdCheck.textContent = checkText; lastCheck = checkText; }
    const { on, status: st } = voiceActivity({ mode, offHook: vs.offHook, micQueued: vs.micQueued, keysQueued: vs.keysQueued,
      speaking, walletListening: wallet.listening, micOn: micSwitch.on, micDbfs });
    const key = [...on].sort().join(" ");
    if (key !== lastActive) {
      lastActive = key;
      for (const p of paths) {
        const a = on.has(p.channel);
        p.el.classList.toggle("active", a);
        for (const port of p.ports) port.classList.toggle("active", a);
      }
    }
    if (S[st] !== lastStatus) { status.textContent = S[st]; lastStatus = S[st]; }
    const left = Math.ceil(vs.micQueued / RATE);
    const text = left > 0 ? S["wallet.sending"].replace("{seconds}", String(left)) : walletNote || S["wallet.idle"];
    if (text !== lastLine) { wdStatus.textContent = text; lastLine = text; }
    requestAnimationFrame(frame);
  }
  showHook();
  drawLinks();
  requestAnimationFrame(frame);
}

if (typeof document !== "undefined" && document.getElementById("sim")?.dataset.firmware === "voice")
  start(document, () => import("./dtmf-signer-voice.mjs").then((f) => f.default()),
    (lang) => import(`./packs/${lang}.mjs`).then((m) => m.default));
