// The Centralita's simulator on its page: the telephone (keypad, handset,
// R key, a Type code button that dials the wallet's keypad code as a
// person's fingers would), its earpiece on the speakers of this computer,
// you listening to the voice, the wallet held to the handset (simulated, or
// yours through this computer's microphone and speakers: chosen inside it),
// the bench's links and what is active, the board's LED, and a speech
// bubble with what the earpiece says, as text. Everything it needs comes
// from #sim-data (written by the site's build) and the page's markup; the
// firmware does the rest (voice-core.mjs).
import { VoiceSimulator, RATE } from "./voice-core.mjs";
import { SimWallet } from "./sim-wallet.mjs";
import { CardView } from "./wallet-card.mjs";
import { orientationFor, linkPath } from "./bench.mjs";
import { LedFlash } from "./activity.mjs";
import { anchor, mobileNote, tapTips, wireWho, firstHint } from "./ui.mjs";
import { startMic, MicSwitch, MIC_ACTIVE_DBFS } from "./mic.mjs";

const SVG = "http://www.w3.org/2000/svg";
const ECHO_LINES = 200;
const FLASH_MS = 500;          // the R key: a short hang-up, as a telephone's flash
const SAID_MS = 600;           // this long quiet: what was said is over
const KEYS = "0123456789*#";
// Your wallet: the microphone of this computer never runs the handset this
// far behind (what is older is dropped, as a real handset would not wait).
export const MIC_BACKLOG_MS = 2000;   // (dropped whole: it never runs the line behind)
const ECHO_MS = 1000;                  // the speakers' echo, after the signer stops

// What the signer says, as text, from its public echo: "say <id>" in the
// language it speaks (phrases[lang][id]; each language of the list in its
// own), numbers and characters as they are, a PIN character as a dot
// (never the character), its tones as one mark. S: the page's texts.
export class Transcript {
  constructor(phrases, S, langs = Object.keys(phrases)) {
    Object.assign(this, { phrases, S, langs });
    this.lang = "en";
    this.words = [];
    this.listed = 0;           // in the language list: the next pack to name (it repeats)
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
      else if (rest === "lang_press") this.words.push(this.phrases[this.langs[this.listed++ % this.langs.length]]?.lang_press ?? "");
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

// Whether the earpiece is playing data tones (not words): what the signer
// says next decides it, from its public echo ("tones K", or words: "say",
// "num", "chars", "ear"); anything else leaves it as it was.
export function tonesAfter(prev, line) {
  const head = line.split(" ", 1)[0];
  if (head === "tones") return true;
  if (head === "say" || head === "num" || head === "chars" || head === "ear") return false;
  return prev;
}

// What is active on the bench, and the one status line that says the most.
// who runs the wallet: "sim" (it plays and listens by the handset) or
// "yours" (this computer's microphone is the handset's, lit with sound above
// the threshold; its speakers the earpiece). From the earpiece: words to
// your ear ("voice"), data tones to the wallet ("tones", also while the
// simulated wallet listens).
export function voiceActivity({ who = "sim", offHook, micQueued = 0, keysQueued = 0, speaking, tonesOut = false, walletListening = false,
  micOn = false, micDbfs = -100, quietMs = Infinity }) {
  const on = new Set(["power"]);
  if (!offHook) return { on, status: "status.on_hook" };
  if (speaking) on.add(tonesOut ? "tones" : "voice");
  const said = speaking ? (tonesOut ? "status.tones" : "status.speaking") : "status.listening_line";
  if (who === "yours") {
    // The microphone hears the speakers too: while the signer speaks, and a
    // moment after, what it hears is mostly that echo, not your device.
    const hearing = micOn && micDbfs > MIC_ACTIVE_DBFS && !speaking && quietMs >= ECHO_MS;
    if (speaking || keysQueued > 0 || hearing) on.add("line");
    if (hearing) on.add("air-in");
    return { on, status: micOn ? "status.mic" : keysQueued > 0 ? "status.keypad" : said };
  }
  if (speaking || keysQueued > 0 || micQueued > 0) on.add("line");
  if (micQueued > 0) on.add("air-in");
  if (walletListening) on.add("tones");
  const status = micQueued > 0 ? "status.air_in" : walletListening ? "status.air_out" : keysQueued > 0 ? "status.keypad" : said;
  return { on, status };
}

// What the speakers of this computer play: the earpiece and, with the
// simulated wallet, what it plays aloud next to the handset. With your
// wallet the "room" is the microphone itself: played back, it would howl.
export function speakerMix(who, earpiece, room) {
  const mix = new Int16Array(earpiece.length);
  for (let i = 0; i < mix.length; i++) mix[i] = Math.max(-32768, Math.min(32767, earpiece[i] + (who === "sim" ? room[i] : 0)));
  return mix;
}
// ⏩ belongs to the simulated wallet: it never mutes your wallet's speakers.
export const speakersOn = ({ who, sound, fast }) => sound && !(who === "sim" && fast);

// Leaving who runs the wallet: the microphone stopped, nothing left at the
// handset's microphone, the dialling cut, the simulated wallet no longer
// listening; the signer keeps running (the same device).
export function leaveVoiceMode({ vs, mic, wallet }) {
  vs.clearMic();
  vs.stopDialling();
  mic?.stop();
  wallet?.stopListening();
}

// Four times faster only while the wallet plays or listens: the firmware's
// clock goes with it, so nothing it times changes.
export const voiceSpeed = ({ micQueued, walletListening }, fast) => (fast && (micQueued > 0 || walletListening) ? 4 : 1);

// The speech bubble: above the telephone, its tail on the earpiece's
// connector, inside the bench. Pure geometry from rects (or elements):
// { left, top, tail } relative to the bench, tail from the bubble's left.
const TAIL_IN = 28;      // the tail this far from the bubble's right end, if it fits
const TAIL_MIN = 12;
const BUBBLE_GAP = 14;   // the tail's height, between the bubble and the telephone
const BUBBLE_MIN = 180;  // px: the narrowest it is made
// avoid: ports on the telephone's top edge (the RJ11 when stacked) whose
// cable comes down through the space above it: the bubble moves to their
// right when it fits, so the cable stays in sight.
export function placeBubble(bubble, ear, phone, bench, pad = 8, avoid = []) {
  const r = (x) => (typeof x.getBoundingClientRect === "function" ? x.getBoundingClientRect() : x);
  const b = r(bubble), e = r(ear), p = r(phone), B = r(bench);
  const x = e.left + e.width / 2 - B.left;
  let lo = pad;
  const hi = B.width - b.width - pad;
  for (const a of avoid.map(r))
    if (a.top <= p.top + a.height && a.left + a.width + pad - B.left <= hi)
      lo = Math.max(lo, Math.round(a.left + a.width + pad - B.left));
  const left = hi < lo ? Math.max(0, Math.round((B.width - b.width) / 2)) : Math.round(Math.min(hi, Math.max(lo, x - (b.width - TAIL_IN))));
  const tail = Math.round(Math.min(b.width - TAIL_MIN, Math.max(TAIL_MIN, x - left)));
  const top = Math.max(0, Math.round(p.top - B.top - b.height - BUBBLE_GAP));
  return { left, top, tail };
}

// The test seed's keypad code ("*", four digits a word, "#") in groups, so
// it reads and wraps; and shown in the first-time guide from the wallet's
// own keypadCode(), what ⌨ Type code dials (el: the guide's <code>, if any).
export const keypadGroups = (code) => (String(code).match(/[*#]|[0-9]{1,4}/g) ?? []).join(" ");
export function showKeypadCode(el, wallet) {
  if (el) el.textContent = keypadGroups(wallet.keypadCode());
}

const fromBase64 = (b64) => Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));

export async function start(doc, loadModule, loadPack) {
  const data = JSON.parse(doc.getElementById("sim-data").textContent);
  mobileNote(doc);
  const seen = firstHint(doc);
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
  const bench = root.querySelector(".bench"), svg = bench.querySelector("svg.cables");
  const echoPre = root.querySelector(".echo");
  const hookBtn = root.querySelector(".ph-hook"), hookLabel = root.querySelector(".ph-hook-label");
  const sound = root.querySelector(".ph-sound"), fast = root.querySelector(".wd-fast");
  const typeBtn = root.querySelector(".ph-type");
  let typing = false;
  const wdWarn = root.querySelector(".wd-warn");
  const listenBtn = root.querySelector(".wd-listen");
  const ledEl = root.querySelector(".sim-led"), led = new LedFlash();
  let ledOn = false, echo = [], paths = [], lastActive = "", lastStatus = "", lastWarn = null;
  let signerNet = null, flashUntil = null, said = false, tonesOut = false;
  let audio = null, nextStart = 0;
  const links = data.links[bench.dataset.mode] ?? [];
  tapTips(doc, root);
  // The first action on the bench: the hint has been seen.
  bench.addEventListener("pointerdown", seen);
  bench.addEventListener("keydown", seen);

  // ---- the earpiece's transcript, in a bubble above the telephone ----
  const bubble = bench.querySelector(".bubble"), saidEl = bubble?.querySelector(".said");
  const phoneEl = root.querySelector(".mod-phone"), earEl = root.querySelector('[data-port="phone.ear"]');
  const lineEl = root.querySelector('[data-port="phone.line"]');
  function placeShown() {
    if (!bubble || !earEl || !phoneEl) return;
    // No wider than from the telephone's left edge (or, stacked, from just
    // right of its RJ11 cable) to just past the earpiece: it never covers
    // the modules beside the telephone, nor that cable.
    const p = phoneEl.getBoundingClientRect(), e = earEl.getBoundingClientRect();
    const l = lineEl?.getBoundingClientRect();
    const from = l && l.top <= p.top + l.height ? Math.max(p.left, l.right + 8) : p.left;
    bubble.style.setProperty("--cap", `${Math.max(BUBBLE_MIN, Math.round(e.left + e.width / 2 - from + TAIL_IN))}px`);
    const { left, top, tail } = placeBubble(bubble, earEl, phoneEl, bench, 8, lineEl ? [lineEl] : []);
    bubble.style.left = `${left}px`;
    bubble.style.top = `${top}px`;
    bubble.style.setProperty("--tail", `${tail}px`);
  }
  function say(t) {
    if (!saidEl || saidEl.textContent === t) return;
    saidEl.textContent = t;
    placeShown();
  }

  // ---- the wallet's screen: a card with what you need for what you did ----
  const cards = { sim: new CardView(root.querySelector('[data-panel="sim"] .wl-card'), S),
    yours: new CardView(root.querySelector('[data-panel="yours"] .wl-card'), S) };
  let card = null;            // { kind, info } on the simulated wallet, or null
  let sending = null;         // { what, total, then: the card once it is played }

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
    if (!micSwitch.on && micLevel) micLevel.value = -80;
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

  // ---- who runs the wallet: the simulated one or yours ----
  const whoSwitch = wireWho({
    buttons: [...root.querySelectorAll(".seg [data-who]")],
    panels: [...root.querySelectorAll("[data-panel]")],
    leave: () => {
      leaveVoiceMode({ vs, mic: { stop: () => micSwitch.cancel() }, wallet });
      showTyping(false);
      sending = null; card = null; micDbfs = -100;
    },
    onChange: () => { micShown(); showListen(); lastActive = ""; },
  });

  // ---- the bench: links drawn port to port (top and bottom ports meet
  // their pair vertically, whatever the layout) ----
  function drawLinks() {
    bench.dataset.orient = orientationFor(bench.clientWidth, Number(bench.dataset.turn) || undefined);
    const box = bench.getBoundingClientRect();
    svg.replaceChildren();
    paths = [];
    for (const l of links) {
      const a = root.querySelector(`[data-port="${l.from}"]`), b = root.querySelector(`[data-port="${l.to}"]`);
      if (!a || !b) continue;
      const stacked = [a, b].some((p) => p.classList.contains("edge-top") || p.classList.contains("edge-bottom"));
      const o = stacked ? "v" : bench.dataset.orient;
      const ra = a.getBoundingClientRect(), rb = b.getBoundingClientRect();
      const el = doc.createElementNS(SVG, "path");
      el.setAttribute("d", linkPath(anchor(ra, rb, o, box), anchor(rb, ra, o, box), o));
      el.setAttribute("class", `c-${a.dataset.type} ch-${l.channel}`);
      svg.append(el);
      paths.push({ el, channel: l.channel, ports: [a, b] });
    }
    lastActive = "";
    placeShown();
  }
  new ResizeObserver(() => drawLinks()).observe(bench);

  // ---- the telephone ----
  function showHook() {
    hookBtn.setAttribute("aria-pressed", String(vs.offHook));
    hookLabel.textContent = S[vs.offHook ? "phone.hang" : "phone.lift"];
    phoneEl?.classList.toggle("off-hook", vs.offHook);
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
  phoneEl.addEventListener("keydown", (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey || !KEYS.includes(e.key) || e.key.length !== 1) return;
    e.preventDefault();
    if (!e.repeat) press(e.key);
  });
  function play(samples) {
    if (!audio || !speakersOn({ who: whoSwitch.who, sound: sound.checked, fast: fast.checked })) return;
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
  const netSel = root.querySelector(".wd-net"), fpEl = root.querySelector(".wl-fp"), netEl = root.querySelector(".wl-net");
  const codeEl = doc.querySelector("details.first .keypad-code");
  function setNetwork(net) {
    wallet.setNetwork(net);
    showKeypadCode(codeEl, wallet);
    if (fpEl) fpEl.textContent = wallet.pair().fingerprint;
    if (netEl) netEl.textContent = net;
  }
  setNetwork(netSel.value);
  // Playing by the handset: a card counting down, then what the signer will say.
  function playNear(samples, what, then) {
    ensureAudio();
    vs.queueMic(samples);
    sending = { what, total: vs.micQueued, then };
    card = null;
  }
  root.querySelector(".wd-seed").addEventListener("click", () =>
    playNear(wallet.sendSeedAir(), S["card.what_seed"], { kind: "seed", info: { pin: data.pin, fingerprint: wallet.pair().fingerprint } }));
  root.querySelector(".wd-psbt").addEventListener("click", () =>
    playNear(wallet.sendPsbtAir(), S["card.what_psbt"], { kind: "psbt", info: { outputs: wallet.pair().outputs, fee: wallet.pair().fee } }));
  const stop = () => { vs.clearMic(); sending = null; };
  // Stopped once the signer has it (the review begun): what to check stays on the card.
  root.querySelector(".wd-stop").addEventListener("click", () => { card = sending?.then ?? null; stop(); });
  netSel.addEventListener("change", (e) => { stop(); setNetwork(e.target.value); card = null; });
  function showListen() { listenBtn.setAttribute("aria-pressed", String(wallet.listening)); }
  listenBtn.addEventListener("click", () => {
    if (wallet.listening) { wallet.stopListening(); card = null; } else { wallet.listen(); card = { kind: "hearing", info: { parts: 0 } }; }
    showListen();
  });

  let last = performance.now();
  function frame(now) {
    const who = whoSwitch.who, sim = who === "sim";
    const speaking = vs.quietMs < 200;
    const dt = (now - last) * (sim ? voiceSpeed({ micQueued: vs.micQueued, walletListening: wallet.listening }, fast.checked) : 1);
    last = now;
    const { echo: lines, earpiece, room } = vs.advance(dt);
    if (flashUntil !== null && vs.clock >= flashUntil) { flashUntil = null; vs.hook(true); showHook(); }
    if (typing && vs.keysQueued === 0) showTyping(false);
    // The speakers: the earpiece, and the wallet playing aloud next to it.
    play(speakerMix(who, earpiece, room));
    // The simulated wallet hears the earpiece, never the echo.
    const heard = sim ? wallet.hear(earpiece) : null;
    if (heard === "ok") {
      const l = wallet.last;
      card = { kind: "scan", info: { ok: l.ok, bytes: l.bytes, lines: l.rows } };
      showListen();
    } else if (heard === "other") {
      card = { kind: "scan", info: { lines: wallet.last?.lines ?? [] } };
      showListen();
    } else if (heard === "listening" && card?.kind === "hearing" && card.info.parts !== wallet.ear.parts) {
      card = { kind: "hearing", info: { parts: wallet.ear.parts } };
    }
    for (const l of lines) {
      if (l === "say network_now_main") signerNet = "mainnet";
      if (l === "say network_now_test") signerNet = "testnet";
      tonesOut = tonesAfter(tonesOut, l);
      const t = transcript.push(l);
      if (t) say(t);
      echo.push(l);
    }
    if (!vs.offHook) tonesOut = false;
    if (lines.length) { said = true; echo = echo.slice(-ECHO_LINES); echoPre.textContent = echo.join("\n"); }
    // Once the signer has been quiet a moment, what it said is complete.
    if (said && vs.quietMs >= SAID_MS) {
      said = false;
      const t = transcript.end();
      if (t) say(t);
    }
    const lit = led.update(vs.keys(), now);
    if (lit !== ledOn && ledEl) { ledEl.classList.toggle("on", lit); ledOn = lit; }
    const warn = signerNet !== null && signerNet !== wallet.net;
    if (warn !== lastWarn) { wdWarn.hidden = !warn; lastWarn = warn; }
    const { on, status: st } = voiceActivity({ who, offHook: vs.offHook, micQueued: vs.micQueued, keysQueued: vs.keysQueued,
      speaking, tonesOut, walletListening: sim && wallet.listening, micOn: micSwitch.on, micDbfs, quietMs: vs.quietMs });
    const key = [...on].sort().join(" ");
    if (key !== lastActive) {
      lastActive = key;
      for (const p of paths) {
        const a = on.has(p.channel);
        p.el.classList.toggle("busy", a);
        for (const port of p.ports) port.classList.toggle("busy", paths.some((q) => q.ports.includes(port) && on.has(q.channel)));
      }
    }
    if (S[st] !== lastStatus) { status.textContent = S[st]; lastStatus = S[st]; }
    // The wallet's card: what is being played, from the first tone; on
    // yours, listening.
    if (sending && vs.micQueued === 0) { card = sending.then; sending = null; }
    if (sending) cards.sim.show({ kind: "sending", info: { what: sending.what, seconds: Math.ceil(vs.micQueued / RATE),
      progress: 1 - vs.micQueued / sending.total } });
    else cards.sim.show(card);
    cards.yours.show(micSwitch.on ? { kind: "listening" } : null);
    requestAnimationFrame(frame);
  }
  showHook();
  drawLinks();
  requestAnimationFrame(frame);
}

if (typeof document !== "undefined" && document.getElementById("sim")?.dataset.firmware === "voice")
  start(document, () => import("./dtmf-signer-voice.mjs").then((f) => f.default()),
    (lang) => import(`./packs/${lang}.mjs`).then((m) => m.default));
