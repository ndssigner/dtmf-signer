// The Centralita's simulator on its page: the telephone (keypad, handset,
// R key), its earpiece on the speakers of this computer, the simulated
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

const SVG = "http://www.w3.org/2000/svg";
const ECHO_LINES = 200;
const FLASH_MS = 500;          // the R key: a short hang-up, as a telephone's flash
const SAID_MS = 600;           // this long quiet: what was said is over
const KEYS = "0123456789*#";

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
export function voiceActivity({ offHook, micQueued, keysQueued, speaking, walletListening }) {
  const on = new Set(["power"]);
  if (!offHook) return { on, status: "status.on_hook" };
  if (speaking || keysQueued > 0 || micQueued > 0) on.add("line");
  if (micQueued > 0) on.add("air-in");
  if (speaking && walletListening) on.add("air-out");
  const status = micQueued > 0 ? "status.air_in" : on.has("air-out") ? "status.air_out"
    : keysQueued > 0 ? "status.keypad" : speaking ? "status.speaking" : "status.listening_line";
  return { on, status };
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
  const wdStatus = root.querySelector(".wd-status"), wdCheck = root.querySelector(".wd-check"), wdWarn = root.querySelector(".wd-warn");
  const listenBtn = root.querySelector(".wd-listen");
  const ledEl = root.querySelector(".sim-led"), led = new LedFlash();
  let ledOn = false, echo = [], paths = [], lastActive = "", lastStatus = "", lastLine = "", lastCheck = "", lastWarn = null;
  let walletNote = "", checkText = "", signerNet = null, flashUntil = null, said = false;
  let audio = null, nextStart = 0;
  tapTips(doc, root);

  // ---- the bench: links drawn port to port (top and bottom ports meet
  // their pair vertically, whatever the layout) ----
  function drawLinks() {
    bench.dataset.orient = orientationFor(bench.clientWidth);
    const box = bench.getBoundingClientRect();
    svg.replaceChildren();
    paths = [];
    for (const l of Object.values(data.links).flat()) {
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
  root.querySelector(".blk-phone").addEventListener("keydown", (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey || !KEYS.includes(e.key) || e.key.length !== 1) return;
    e.preventDefault();
    if (!e.repeat) press(e.key);
  });
  function play(samples) {
    if (!audio || !sound.checked || fast.checked) return;
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
    const dt = (now - last) * voiceSpeed({ micQueued: vs.micQueued, walletListening: wallet.listening }, fast.checked);
    last = now;
    const { echo: lines, earpiece, room } = vs.advance(dt);
    if (flashUntil !== null && vs.clock >= flashUntil) { flashUntil = null; vs.hook(true); showHook(); }
    // The speakers: the earpiece, and the wallet playing aloud next to it.
    const mix = new Int16Array(earpiece.length);
    for (let i = 0; i < mix.length; i++) mix[i] = Math.max(-32768, Math.min(32767, earpiece[i] + room[i]));
    play(mix);
    // The wallet hears the earpiece, never the echo.
    const heard = wallet.hear(earpiece);
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
    const { on, status: st } = voiceActivity({ offHook: vs.offHook, micQueued: vs.micQueued, keysQueued: vs.keysQueued,
      speaking, walletListening: wallet.listening });
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
