import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { buildInto, brokenLinks, text, FIX } from "./helpers.mjs";
import { HAVE_MODULE, REQUIRED, SKIP } from "./wasm-helpers.mjs";
import { CSP } from "../lib/page.mjs";
import { validateBench } from "../lib/bench.mjs";
import { releaseOnBlur, releaseOnLeave, speedFor, start, isPhone, buttonsDown, showHelp, leaveHelp } from "../static/sim/ui.mjs";

const files = buildInto();
const SIM_CSP = CSP.replace("script-src 'self'", "script-src 'self' 'wasm-unsafe-eval'");

test("the page carries the simulator, its data and its scripts", { skip: SKIP }, () => {
  const html = text(files, "en/cable-qr/index.html");
  const json = /<script type="application\/json" id="sim-data">([\s\S]*?)<\/script>/.exec(html);
  assert.ok(json, "sim-data");
  const data = JSON.parse(json[1]);
  assert.equal(data.pin, "1234");
  assert.match(data.networks.mainnet.psbt, /^cHNidP8BAHECAAAAAX4gBlDy3j1VOEPCv1EQt1Y5OeRbmPWZVrdbpYPwIXfE/);
  assert.equal(data.networks.mainnet.seed, "00000000000000000000000000000000");
  assert.equal(data.networks.mainnet.fingerprint, "73c5da0a");
  assert.equal(data.networks.testnet.fingerprint, "8b218e81");
  assert.match(data.networks.testnet.psbt, /^cHNidP8BAHECAAAAAa5DsWjV/);
  assert.equal(data.networks.mainnet.outputs.length, 2);
  assert.equal(data.tools.urtones, "https://ndssigner.github.io/ur-tones/");
  assert.match(html, /<canvas [^>]*width="128" height="64"/);
  for (const b of ["left", "up", "ok", "down", "right"]) assert.match(html, new RegExp(`data-btn="${b}"`));
  assert.match(html, /class="sim-warning"/);
  assert.match(html, /<script type="module" src="\.\.\/\.\.\/sim\/ui\.mjs"><\/script>/);
  for (const f of ["sim/ui.mjs", "sim/checks.mjs", "sim/core.mjs", "sim/wallet.mjs", "sim/sim-wallet.mjs", "sim/bench.mjs", "sim/mic.mjs", "sim/mic-worklet.js", "sim/dtmf-signer.mjs", "sim/ur-tones.js", "sim/ur-tones-crypto.js"])
    assert.ok(files.has(f), f);
});
test("only the simulator's page allows WebAssembly", { skip: SKIP }, () => {
  assert.ok(text(files, "en/cable-qr/index.html").includes(`content="${SIM_CSP}"`));
  assert.ok(text(files, "en/index.html").includes(`content="${CSP}"`));
  assert.ok(!text(files, "en/index.html").includes("wasm-unsafe-eval"));
});
test("without the module the page keeps its placeholder", { skip: HAVE_MODULE || REQUIRED ? "module present or required" : false }, () => {
  assert.match(text(files, "en/cable-qr/index.html"), /Coming soon/);
});
test("links still resolve", () => {
  assert.deepEqual(brokenLinks(files), []);
});
test("a blur releases every button", () => {
  const sim = { held: 3, releaseAll() { this.held = 0; } };
  const target = new EventTarget();
  releaseOnBlur(sim, target);
  target.dispatchEvent(new Event("blur"));
  assert.equal(sim.held, 0);
});
test("focus leaving the device, even from an inner button, releases every button", () => {
  const sim = { held: 3, releaseAll() { this.held = 0; } };
  const device = new EventTarget();
  const inside = {};
  device.contains = (n) => n === inside;
  releaseOnLeave(sim, device);
  const ev = (to) => Object.assign(new Event("focusout"), { relatedTarget: to });
  device.dispatchEvent(ev(inside));
  assert.equal(sim.held, 3, "moving inside keeps it");
  device.dispatchEvent(ev({}));
  assert.equal(sim.held, 0);
});
test("four times faster only while tones play and no button is down", () => {
  assert.equal(speedFor({ queued: 1000, idle: true }, true), 4);
  assert.equal(speedFor({ queued: 1000, idle: false }, true), 1);
  assert.equal(speedFor({ queued: 0, idle: true }, true), 1);
  assert.equal(speedFor({ queued: 1000, idle: true }, false), 1);
});
test("no numbered steps or paragraphs of help: icons and tooltips instead", { skip: SKIP }, () => {
  const html = text(files, "en/cable-qr/index.html");
  // The first-time hint is the one list, folded away (its own test below).
  const sim = html.slice(html.indexOf('id="sim"'), html.indexOf('id="sim-data"')).replace(/<details class="first">[\s\S]*?<\/details>/, "");
  assert.doesNotMatch(sim, /<ol>/);
  assert.doesNotMatch(sim, /sim\.step/);
  assert.match(sim, /data-tip="/);
});
test("every port of the bench is on the page", { skip: SKIP }, () => {
  const html = text(files, "en/cable-qr/index.html");
  for (const p of ["signer.5v", "signer.qr", "signer.in", "wallet.camera", "wallet.jack", "charger.usb"])
    assert.match(html, new RegExp(`class="port[^"]*"[^>]*data-port="${p.replace(".", "\\.")}"`), p);
  for (const m of html.matchAll(/<span class="port[^>]*>/g)) assert.match(m[0], /aria-label="[^"]+"/);
});
test("the real screen and buttons sit inside the signer's block", { skip: SKIP }, () => {
  const html = text(files, "en/cable-qr/index.html");
  const block = html.slice(html.indexOf('data-module="signer"'));
  const end = block.indexOf("</section>");
  assert.ok(block.indexOf("sim-oled") > 0 && block.indexOf("sim-oled") < end);
  assert.ok(block.indexOf('data-btn="ok"') > 0 && block.indexOf('data-btn="ok"') < end);
});
test("your own wallet says it is only for trying, in both languages", { skip: SKIP }, () => {
  assert.ok(text(files, "en/cable-qr/index.html").includes("Only for trying it out: this build is meant to be used with an audio cable."));
  assert.ok(text(files, "es/cable-qr/index.html").includes("Solo para probarlo: esta implementación está pensada para usarse con cable de audio."));
});
test("the simulated wallet has its buttons", { skip: SKIP }, () => {
  const html = text(files, "es/cable-qr/index.html");
  for (const c of ["wd-seed", "wd-psbt", "wd-scan"]) assert.match(html, new RegExp(`class="${c}"`));
});
test("if the module cannot load, the page says so", async () => {
  const status = { textContent: "" };
  const doc = {
    getElementById: (id) => (id === "sim-data" ? { textContent: JSON.stringify({ strings: { "sim.unavailable": "Not available" } }) } : null),
    querySelector: (sel) => (sel === ".sim-status" ? status : null),
  };
  await start(doc, () => Promise.reject(new Error("no wasm")));
  assert.equal(status.textContent, "Not available");
});

// Phase 1 minors in these files
test("files and images go under their build's folder", () => {
  assert.ok(files.has("files/cable-qr/dtmf-signer-breadboard.diy"));
  assert.ok(files.has("img/cable-qr/breadboard-layout.png"));
});
test("the home page titles its list of builds", () => {
  assert.match(text(files, "en/index.html"), /<h2[^>]*>Builds<\/h2>/);
});
test("text shown in English on another language's page is marked lang=en", () => {
  assert.match(text(buildInto(FIX), "xx/index.html"), /<div lang="en">/);
});

test("the text alternative describes the connections, the cable and your own device's", { skip: SKIP }, () => {
  const html = text(files, "en/cable-qr/index.html");
  const lists = [...html.matchAll(/<ul class="connections[^"]*"[^>]*>([\s\S]*?)<\/ul>/g)];
  assert.equal(lists.length, 1);
  assert.match(lists[0][1], /headphone/i);
  assert.match(lists[0][1], /microphone/i);
});

test("the wallet is never called a phone (the telephone is the exchange's)", { skip: SKIP }, () => {
  const es = text(files, "es/cable-qr/index.html"), en = text(files, "en/cable-qr/index.html");
  for (const t of ["🧪 Simulada", "📱 La tuya"]) assert.ok(es.includes(t), t);
  for (const t of ["🧪 Simulated", "📱 Yours"]) assert.ok(en.includes(t), t);
  assert.doesNotMatch(es, /class="(ph|yp)-/);
  const sim = (h) => /<div class="simulator"[\s\S]*?<script type="application\/json"/.exec(h)[0];
  assert.doesNotMatch(sim(es), /móvil simulado|smartphone/i);
  assert.doesNotMatch(sim(en), /simulated phone|smartphone/i);
});

test("the simulated wallet has a network selector, a stop button, a mismatch mark and a check area", { skip: SKIP }, () => {
  const html = text(files, "es/cable-qr/index.html");
  assert.match(html, /<select class="wd-net"[^>]*autocomplete="off"/);
  assert.match(html, /<select class="wd-net"[^>]*>\s*<option value="mainnet"[^>]*>mainnet<\/option>\s*<option value="testnet"[^>]*>testnet<\/option>/);
  assert.match(html, /<button type="button" class="wd-stop"/);
  assert.match(html, /<span class="wd-warn"[^>]*hidden/);
  assert.match(html, /<div class="wl-card" aria-live="polite">/);
});

test("every file of the simulator is published", { skip: SKIP }, async () => {
  const { readdirSync } = await import("node:fs");
  for (const f of readdirSync(new URL("../static/sim/", import.meta.url))) assert.ok(files.has(`sim/${f}`), f);
});

test("on a phone a computer or tablet is recommended: narrow and touch, both", () => {
  assert.equal(isPhone({ width: 390, coarse: true }), true);
  assert.equal(isPhone({ width: 390, coarse: false }), false);
  assert.equal(isPhone({ width: 1024, coarse: true }), false);
});
test("the phone notice is on the page, hidden until a phone is seen", { skip: SKIP }, () => {
  const html = text(files, "es/cable-qr/index.html");
  assert.match(html, /<p class="sim-mobile"[^>]*hidden/);
  assert.match(html, /class="sim-mobile-close"/);
  assert.ok(html.includes("Mejor desde un ordenador o una tablet"));
});
test("your wallet names its tools: ur-tones for the tones, a compatible wallet to scan", { skip: SKIP }, () => {
  const html = text(files, "es/cable-qr/index.html");
  const yw = /<div class="wl-yours" data-panel="yours"[\s\S]*?<\/section>/.exec(html)[0];
  assert.match(yw, /<a class="yw-urtones" href="https:\/\/ndssigner\.github\.io\/ur-tones\/"/);
  assert.match(yw, /<a class="yw-scan" href="https:\/\/nunchuk\.io\/"/);
  assert.ok(yw.includes("Nunchuk"));
});

test("the Pico's own LED is on the signer, and only where the bench declares it", { skip: SKIP }, async () => {
  const html = text(files, "es/cable-qr/index.html");
  const signer = /<section class="mod mod-signer[\s\S]*?<\/section>/.exec(html)[0];
  assert.match(signer, /<span class="sim-led"[^>]*data-tip="[^"]*Raspberry Pi Pico \(GP25\)/);
  const { blockBody } = await import("../build.mjs");
  assert.doesNotMatch(blockBody("signer", (k) => k, {}, { id: "signer" }), /sim-led/);
});

test("the button held shows as down by name", () => {
  assert.deepEqual(buttonsDown(3), ["ok"]);
  assert.deepEqual(buttonsDown(1), ["left"]);
  assert.deepEqual(buttonsDown(0), []);
});

test("side by side, the port labels stand upright in the blocks' side margins", () => {
  const css = readFileSync(new URL("../static/style.css", import.meta.url), "utf8");
  assert.match(css, /\.bench\[data-orient="h"\] \.port \{[^}]*writing-mode: vertical-rl/);
  assert.match(css, /\.bench\[data-orient="h"\] \.col-signer \.mod \{[^}]*padding-left: var\(--margin\)[^}]*padding-right: var\(--margin\)/);
});

// ---- the PEX (the voice build) ----
const VOICE_SKIP = existsSync(new URL("../../build/wasm/dtmf-signer-voice.mjs", import.meta.url)) || REQUIRED
  ? false : "build/wasm/dtmf-signer-voice.mjs missing: run tools/build-wasm.sh";
const voiceData = (lang) => JSON.parse(/<script type="application\/json" id="sim-data">([\s\S]*?)<\/script>/.exec(text(files, `${lang}/pex/index.html`))[1]);

test("/es/pex/ has the phone keypad and handset, no screen, its profile", { skip: VOICE_SKIP }, () => {
  const html = text(files, "es/pex/index.html");
  const keys = [...html.matchAll(/data-key="([0-9*#])"/g)].map((m) => m[1]);
  assert.deepEqual(keys.sort(), [..."#*0123456789"].sort());
  assert.match(html, /<button type="button" class="ph-hook"[^>]*aria-pressed="false"/);
  assert.doesNotMatch(html, /<canvas/);
  assert.doesNotMatch(html, /data-btn=/);
  assert.match(html, /PROFILE-PHONE/);
  assert.ok(html.includes(`content="${SIM_CSP}"`));
  assert.match(html, /<script type="module" src="\.\.\/\.\.\/sim\/voice-ui\.mjs"><\/script>/);
});
test("the PEX's bench passes the catalog", async () => {
  const { checkBenchAgainstCatalog, validateBench } = await import("../lib/bench.mjs");
  const p = JSON.parse(readFileSync(new URL("../config.json", import.meta.url), "utf8")).profiles.find((x) => x.slug === "pex");
  const catalog = readFileSync(new URL("../../docs/catalog.md", import.meta.url), "utf8");
  assert.equal(p.id, "PROFILE-PHONE");
  assert.deepEqual(validateBench(p.bench), []);
  assert.deepEqual(checkBenchAgainstCatalog(p.bench, catalog, p.id), []);
});
test("the voice build is published with the Spanish pack; only the Spanish page installs it", { skip: VOICE_SKIP }, () => {
  for (const f of ["sim/dtmf-signer-voice.mjs", "sim/voice-core.mjs", "sim/voice-ui.mjs", "sim/packs/es.bin", "sim/packs/es.mjs"])
    assert.ok(files.has(f), f);
  assert.ok(files.get("sim/packs/es.bin").equals(readFileSync(new URL("../../app/voice/packs/es.bin", import.meta.url))));
  assert.equal(voiceData("es").pack, "es");
  assert.equal(voiceData("en").pack, null);
});
test("the text alternative: what the signer says, in the languages it can speak, live", { skip: VOICE_SKIP }, () => {
  const es = voiceData("es"), en = voiceData("en");
  assert.equal(es.phrases.es.choose_option, "Elige una opción.");
  assert.equal(es.phrases.en.choose_option, "Choose an option.");
  assert.match(es.phrases.es.choose_option_long, /pulsa su tecla/);
  assert.match(es.phrases.en.choose_option_long, /press its key/);
  assert.equal(en.phrases.es, undefined);
  assert.equal(en.phrases.en.c_a, undefined, "letters come from the echo's own text");
});
test("the PEX's wallet: the same one module, the selector inside, Seed / Pay / Listen, no camera", { skip: VOICE_SKIP }, () => {
  assert.ok(voiceData("en").pin.length >= 12);
  for (const [lang, acts] of [["es", ["Semilla", "Pagar", "Escuchar"]], ["en", ["Seed", "Pay", "Listen"]]]) {
    const html = text(files, `${lang}/pex/index.html`);
    assert.doesNotMatch(html, /class="sim-modes"|role="tab"|<button[^>]*data-mode=|data-module="(sim|your)-wallet"/, lang);
    const w = /<section class="mod mod-wallet" data-module="wallet"[\s\S]*?<\/section>/.exec(html)[0];
    assert.match(w, /<div class="seg" role="group"[^>]*>[\s\S]*?data-who="sim" aria-pressed="true"[\s\S]*?data-who="yours" aria-pressed="false"/);
    const a = /<div class="wl-acts">([\s\S]*?)<\/div>/.exec(w)[1];
    assert.deepEqual([...a.matchAll(/class="(wd-[a-z]+)"/g)].map((m) => m[1]), ["wd-seed", "wd-psbt", "wd-listen"]);
    for (const t of ["🌱", "₿", "👂", ...acts]) assert.ok(a.includes(t), `${lang}: ${t}`);
    assert.doesNotMatch(w, /wd-scan|yw-cam/);
    for (const c of ["wd-net", "wd-stop", "wd-fast", "wl-card", "mic-toggle", "mic-level", "yw-urtones"]) assert.match(w, new RegExp(`class="${c}"`), c);
    assert.match(w, /<div class="wl-yours" data-panel="yours" hidden>/);
    assert.match(w, /data-port="wallet\.speaker-near"/);
    assert.match(w, /data-port="wallet\.mic-near"/);
  }
  const yw = /<div class="wl-yours" data-panel="yours"[\s\S]*?<\/section>/.exec(text(files, "es/pex/index.html"))[0];
  assert.ok(yw.includes("Micrófono de este ordenador") && yw.includes("Altavoces de este ordenador"));
});
test("you hear the voice: a module with your ear, fed by the earpiece; the wallet gets the tones", { skip: VOICE_SKIP }, () => {
  for (const [lang, ear, title] of [["es", "👂 oído", "Tú"], ["en", "👂 ear", "You"]]) {
    const html = text(files, `${lang}/pex/index.html`);
    const you = /<section class="mod mod-you" data-module="you"[\s\S]*?<\/section>/.exec(html)?.[0];
    assert.ok(you, lang);
    assert.ok(you.includes("🧑") && you.includes(title), lang);
    assert.match(you, new RegExp(`<span class="port p-air edge-peer-side" data-port="you\\.ear-in"[^>]*>${ear}</span>`), lang);
    // The telephone's connectors on its right edge: the earpiece above, the microphone below.
    const phone = /<section class="mod mod-phone"[\s\S]*?<\/section>/.exec(html)[0];
    const ports = [...phone.matchAll(/<span class="port p-air edge-far-side" data-port="phone\.(ear|mic)"/g)].map((m) => m[1]);
    assert.deepEqual(ports, ["ear", "mic"], lang);
  }
  const links = voiceData("en").links.wallet.map((l) => `${l.from}>${l.to}:${l.channel}`);
  for (const l of ["phone.ear>you.ear-in:voice", "phone.ear>wallet.mic-near:tones", "wallet.speaker-near>phone.mic:air-in", "signer.line>phone.line:line", "charger.usb>signer.5v:power"])
    assert.ok(links.includes(l), l);
  const css = readFileSync(new URL("../static/style.css", import.meta.url), "utf8");
  assert.match(css, /\.bench\[data-orient="h"\] \.col-peer \.edge-far-side \{[^}]*right: 0/);
  assert.match(css, /\[data-port="phone\.ear"\] \{[^}]*--at: \d+%/);
});
test("the earpiece's transcript: a speech bubble over the bench, live, saying what it is", { skip: VOICE_SKIP }, () => {
  for (const [lang, said] of [["es", "transcripción"], ["en", "transcript"]]) {
    const html = text(files, `${lang}/pex/index.html`);
    const bench = /<div class="bench"[\s\S]*?<p class="sim-status"/.exec(html)[0];
    const b = /<p class="bubble"[^>]*>([\s\S]*?)<\/p>/.exec(bench);
    assert.ok(b, lang);
    assert.match(b[0], /aria-live="polite"/);
    assert.ok(b[1].includes("🔈") && b[1].toLowerCase().includes(said), b[1]);
    assert.match(b[1], /<span class="said"><\/span>/);
    assert.doesNotMatch(html, /ph-caption/);
  }
  assert.doesNotMatch(text(files, "es/cable-qr/index.html"), /class="bubble"/);
});
test("the PEX's legend calls the RJ11 a telephone cable", { skip: VOICE_SKIP }, () => {
  for (const [lang, t] of [["es", "cable telefónico"], ["en", "telephone cable"]]) {
    const legend = /<ul class="legend">([\s\S]*?)<\/ul>/.exec(text(files, `${lang}/pex/index.html`))[1];
    assert.match(legend, new RegExp(`<li class="l-phone"><i aria-hidden="true"></i>${t}</li>`), lang);
  }
});
test("the PEX's first-time hint: the telephone's way, folded", { skip: VOICE_SKIP }, () => {
  for (const lang of ["es", "en"]) {
    const html = text(files, `${lang}/pex/index.html`);
    const d = /<details class="first">\s*<summary>[^<]+<\/summary>\s*<ol>([\s\S]*?)<\/ol>\s*<\/details>/.exec(html);
    assert.ok(d, lang);
    assert.equal((d[1].match(/<li>/g) ?? []).length, 4, lang);
    for (const t of ["⌨", "🌱", "₿", "👂", "<b>R</b>", "<b>#</b>"]) assert.ok(d[1].includes(t), `${lang}: ${t}`);
    assert.doesNotMatch(d[1], /📷|OK/, lang);
  }
});
test("the telephone has a Type code button: off until lifted, a short tip, in both languages", { skip: VOICE_SKIP }, () => {
  for (const [lang, label] of [["en", "Type code"], ["es", "Teclear código"]]) {
    const html = text(files, `${lang}/pex/index.html`);
    const phone = /<div class="ph-phone">[\s\S]*?<\/section>/.exec(html)[0];
    const b = /<button type="button" class="ph-type"[^>]*>[\s\S]*?<\/button>/.exec(phone)?.[0];
    assert.ok(b, lang);
    assert.match(b, /aria-pressed="false"/);
    assert.match(b, / disabled/);
    assert.match(b, /data-tip="[^"]{10,90}"/);
    assert.ok(b.includes("⌨") && b.includes(label), b);
  }
  const ui = readFileSync(new URL("../static/sim/voice-ui.mjs", import.meta.url), "utf8");
  assert.match(ui, /vs\.dial\(wallet\.keypadCode\(\)\)/, "the wallet's keypad code, through the keypad");
});
test("the PEX credits its voices, and shows the board's LED", { skip: VOICE_SKIP }, () => {
  const es = text(files, "es/pex/index.html"), en = text(files, "en/pex/index.html");
  const plain = (h) => h.replace(/<[^>]+>/g, "");
  assert.ok(plain(es).includes("Voz española: Piper es_ES-sharvard-medium, corpus Sharvard (Universidad de Edimburgo), CC BY 3.0"));
  assert.ok(plain(en).includes("Spanish voice: Piper es_ES-sharvard-medium, Sharvard corpus (University of Edinburgh), CC BY 3.0"));
  for (const h of [es, en]) {
    assert.ok(h.includes('href="https://datashare.ed.ac.uk/handle/10283/574"'));
    assert.ok(h.includes('href="https://creativecommons.org/licenses/by/3.0/"'));
    assert.ok(plain(h).includes("Piper en_US-ljspeech-medium, LJ Speech"));
    assert.match(/<section class="mod mod-voice-signer[\s\S]*?<\/section>/.exec(h)[0], /class="sim-led"/);
  }
});
test("the PEX's status: designed, simulated, on a breadboard and ported; the perfboard and the PCB still to come", () => {
  const s = JSON.parse(readFileSync(new URL("../../hardware/phone/status.json", import.meta.url), "utf8"));
  const st = (part, stage) => s[part].find((x) => x.stage === stage).state;
  assert.equal(st("hardware", "design"), "done");
  assert.equal(st("hardware", "simulation"), "done");
  assert.equal(st("hardware", "breadboard"), "done");
  assert.equal(st("hardware", "perfboard"), "planned");
  assert.equal(st("hardware", "pcb"), "planned");
  assert.equal(st("firmware", "core"), "done");
  assert.equal(st("firmware", "port"), "done");
  assert.equal(st("firmware", "simulator"), "done");
});

// ---- the bench's visual language: neutral modules, coloured connectors and cables ----
const BENCHES = [["cable-qr", SKIP, ["power", "audio", "light"]], ["pex", VOICE_SKIP, ["power", "phone", "air"]]];
for (const [slug, skip, types] of BENCHES) {
  test(`${slug}: the legend is always shown and lists exactly the cables of its bench`, { skip }, () => {
    for (const lang of ["en", "es"]) {
      const html = text(files, `${lang}/${slug}/index.html`);
      const legend = /<ul class="legend">([\s\S]*?)<\/ul>/.exec(html);
      assert.ok(legend, `${lang}: legend`);
      const before = html.slice(0, legend.index);
      assert.equal((before.match(/<details/g) ?? []).length, (before.match(/<\/details>/g) ?? []).length, `${lang}: not inside a <details>`);
      const shown = [...legend[1].matchAll(/<li class="l-([a-z]+)">/g)].map((m) => m[1]);
      assert.deepEqual(shown.sort(), [...types].sort(), lang);
      assert.match(html, /<details class="sim-echo">/, "the echo stays folded");
    }
  });
  test(`${slug}: modules are neutral sections, ports carry their cable's class and their edge`, { skip }, () => {
    const html = text(files, `en/${slug}/index.html`);
    assert.doesNotMatch(html, /class="blk|class="lg-|type-(power|audio|light|phone|air)/);
    const mods = [...html.matchAll(/<section class="([^"]*)" data-module=/g)].map((m) => m[1]);
    assert.ok(mods.length >= 3);
    for (const c of mods) assert.match(c, /^mod mod-[a-z-]+$/, c);
    const ports = [...html.matchAll(/<span class="(port [^"]*)" data-port="([^"]+)"/g)];
    assert.ok(ports.length >= 4);
    for (const [, c, id] of ports) assert.match(c, /^port p-(power|audio|air|light|phone) edge-[a-z-]+$/, id);
  });
}
test("both simulators draw links as c-<type> paths, busy while they carry something", () => {
  for (const f of ["ui.mjs", "voice-ui.mjs"]) {
    const ui = readFileSync(new URL(`../static/sim/${f}`, import.meta.url), "utf8");
    assert.match(ui, /`c-\$\{a\.dataset\.type\}/, f);
    assert.match(ui, /classList\.toggle\("busy"/, f);
    assert.doesNotMatch(ui, /"link |\.links"|"active"|blk-/, f);
  }
});

// ---- Cable y QR: the wallet as one phone, simulated or yours ----
test("one wallet module, the selector inside it, its two ports labelled, no mode tabs", { skip: SKIP }, () => {
  for (const [lang, cam, jack] of [["es", "cámara", "auriculares"], ["en", "camera", "headphones"]]) {
    const html = text(files, `${lang}/cable-qr/index.html`);
    assert.doesNotMatch(html, /class="sim-modes"|role="tab"|<button[^>]*data-mode=/, lang);
    assert.doesNotMatch(html, /data-module="(sim|your)-wallet"/, lang);
    const mods = [...html.matchAll(/<section class="mod mod-wallet" data-module="wallet"/g)];
    assert.equal(mods.length, 1, lang);
    const w = /<section class="mod mod-wallet" data-module="wallet"[\s\S]*?<\/section>/.exec(html)[0];
    const seg = /<div class="seg" role="group" aria-label="[^"]+">([\s\S]*?)<\/div>/.exec(w);
    assert.ok(seg, `${lang}: the selector is inside the wallet`);
    assert.match(seg[1], /<button type="button" data-who="sim" aria-pressed="true"[^>]*>🧪 /);
    assert.match(seg[1], /<button type="button" data-who="yours" aria-pressed="false"[^>]*>📱 /);
    assert.match(w, /<div class="wl-sim" data-panel="sim">/);
    assert.match(w, /<div class="wl-yours" data-panel="yours" hidden>/);
    assert.match(w, /<span class="port p-light edge-signer-side" data-port="wallet\.camera" data-type="light" data-dir="in"[^>]*aria-label="[^"]+"[^>]*>[^<]*/);
    assert.match(w, /<span class="port p-audio edge-signer-side" data-port="wallet\.jack" data-type="audio" data-dir="out"[^>]*aria-label="[^"]+"[^>]*>[^<]*/);
    assert.ok(/data-port="wallet\.camera"[^>]*>([^<]+)</.exec(w)[1].includes(cam), lang);
    assert.ok(/data-port="wallet\.jack"[^>]*>([^<]+)</.exec(w)[1].includes(jack), lang);
  }
});
test("the simulated wallet shows its account and three actions; the footer its network, sound and speed", { skip: SKIP }, () => {
  const html = text(files, "es/cable-qr/index.html");
  const sim = /<div class="wl-sim" data-panel="sim">[\s\S]*?<div class="wl-yours"/.exec(html)[0];
  const acct = /<dl class="wl-acct">([\s\S]*?)<\/dl>/.exec(sim)[1];
  for (const t of ["1234", "73c5da0a", "mainnet", "PIN"]) assert.ok(acct.includes(t), t);
  assert.match(acct, /<dd class="wl-fp">73c5da0a<\/dd>/);
  const acts = /<div class="wl-acts">([\s\S]*?)<\/div>/.exec(sim)[1];
  assert.deepEqual([...acts.matchAll(/class="(wd-[a-z]+)"/g)].map((m) => m[1]), ["wd-seed", "wd-psbt", "wd-scan"]);
  for (const t of ["🌱", "₿", "📷", "Semilla", "Pagar", "Escanear"]) assert.ok(acts.includes(t), t);
  const foot = /<div class="wl-foot">([\s\S]*?)<\/div>/.exec(sim)[1];
  for (const c of ["wd-net", "wd-stop", "wd-sound", "wd-fast"]) assert.match(foot, new RegExp(`class="${c}"`), c);
});
test("your wallet: this computer's microphone, a level, and the tools", { skip: SKIP }, () => {
  const html = text(files, "es/cable-qr/index.html");
  const yw = /<div class="wl-yours" data-panel="yours"[\s\S]*?<\/section>/.exec(html)[0];
  for (const c of ["yw-mic", "yw-cam", "mic-toggle", "mic-level", "mic-note", "wl-card"]) assert.match(yw, new RegExp(`class="${c}"`), c);
  assert.ok(yw.includes("este ordenador"));
});
test("the first-time hint: a folded list, closed in the page, open on a first visit by script only", { skip: SKIP }, () => {
  for (const lang of ["es", "en"]) {
    const html = text(files, `${lang}/cable-qr/index.html`);
    const d = /<details class="first">\s*<summary>([^<]+)<\/summary>\s*<ol>([\s\S]*?)<\/ol>\s*<\/details>/.exec(html);
    assert.ok(d, lang);
    assert.equal((d[2].match(/<li>/g) ?? []).length, 4, lang);
    for (const t of ["🌱", "₿", "📷", "Load seed", "Sign PSBT"]) assert.ok(d[2].includes(t), `${lang}: ${t}`);
    assert.ok(html.indexOf('class="first"') < html.indexOf('class="bench"'), "above the bench");
  }
  assert.ok(text(files, "es/cable-qr/index.html").includes("¿Primera vez? Prueba esto"));
});

// ---- Task 7, fix round 1 ----
for (const [slug, skip] of [["cable-qr", SKIP], ["pex", VOICE_SKIP]]) {
  test(`${slug}: no placeholder left unfilled on the page (tooltips included)`, { skip }, () => {
    for (const lang of ["en", "es"]) {
      const html = text(files, `${lang}/${slug}/index.html`).replace(/<script type="application\/json"[\s\S]*?<\/script>/g, "");
      assert.deepEqual(html.match(/\{[a-z_]+\}/g) ?? [], [], `${lang}/${slug}`);
    }
  });
}
test("the PEX's guide: or type the keypad code yourself, folded, filled by the page", { skip: VOICE_SKIP }, () => {
  for (const [lang, say, show] of [["es", "o tecléalo tú:", "ver código"], ["en", "or type it yourself:", "show code"]]) {
    const html = text(files, `${lang}/pex/index.html`);
    const li = /<details class="first">[\s\S]*?<ol>\s*<li>[\s\S]*?<\/li>\s*<li>([\s\S]*?)<\/li>/.exec(html)[1];
    assert.ok(li.toLowerCase().includes(say), li);
    assert.match(li, new RegExp(`<details class="kcode"><summary>${show}</summary><code class="keypad-code"></code></details>`), lang);
  }
});
test("the keypad code in fours, as the guide shows it", async () => {
  const { keypadGroups } = await import("../static/sim/voice-ui.mjs");
  assert.equal(keypadGroups("*000000010002#"), "* 0000 0001 0002 #");
  assert.equal(keypadGroups("*12345#"), "* 1234 5 #");
  assert.equal(keypadGroups(""), "");
});
test("the guide's code is what ⌨ Type code dials, on each network", { skip: VOICE_SKIP }, async () => {
  const { showKeypadCode, keypadGroups } = await import("../static/sim/voice-ui.mjs");
  const { SimWallet } = await import("../static/sim/sim-wallet.mjs");
  const { loadUrTones } = await import("./urtones-helpers.mjs");
  const wallet = new SimWallet({ sim: {}, U: loadUrTones(), data: voiceData("es"), el: {} });
  const codes = new Set();
  for (const net of ["mainnet", "testnet"]) {
    wallet.setNetwork(net);
    const el = { textContent: "" };
    showKeypadCode(el, wallet);
    assert.equal(el.textContent, keypadGroups(wallet.keypadCode()), net);
    assert.equal(el.textContent.replace(/ /g, ""), wallet.keypadCode(), net);
    codes.add(el.textContent);
  }
  assert.equal(codes.size, 2, "each network its own seed");
  showKeypadCode(null, wallet);   // no guide on the page: nothing breaks
  const ui = readFileSync(new URL("../static/sim/voice-ui.mjs", import.meta.url), "utf8");
  assert.match(ui, /setNetwork\(net\) \{[\s\S]*?showKeypadCode\(/, "refreshed when the network changes");
});
test("your wallet's mic button says it turns this computer's mic on, in both builds and languages", { skip: SKIP }, () => {
  for (const [lang, on] of [["es", "Activar micro"], ["en", "Turn on mic"]])
    for (const slug of ["cable-qr", "pex"]) {
      const html = text(files, `${lang}/${slug}/index.html`);
      assert.match(html, new RegExp(`class="mic-toggle"[^>]*>(<span[^>]*>🎙</span> ?)?${on}<`), `${lang}/${slug}`);
    }
});
test("every module keeps a strip for its help, and a module's tips show there, not floating", { skip: SKIP }, () => {
  for (const slug of ["cable-qr", "pex"]) {
    const html = text(files, `es/${slug}/index.html`);
    const mods = [...html.matchAll(/<section class="mod [\s\S]*?<\/section>/g)].map((m) => m[0]);
    assert.ok(mods.length >= 3, slug);
    for (const m of mods) assert.match(m, /<p class="mod-help"[^>]*><\/p>\s*<\/section>$/, `${slug}: ${m.slice(0, 60)}`);
  }
  const css = readFileSync(new URL("../static/style.css", import.meta.url), "utf8");
  assert.match(css, /\.mod \[data-tip\]:hover::after[^{]*\{[^}]*content: none/);
  // A fixed height, whole lines: the module never grows or shrinks with the text.
  assert.match(css, /\.mod-help \{[^}]*[^-]height: calc\(4 \* 1\.35 \* \.75rem\);[^}]*overflow-y: auto/);
});
test("pointing at something in a module tells its story in that module's strip; leaving the module clears it", () => {
  const strip = { textContent: "" };
  const mod = { querySelector: (s) => (s === ".mod-help" ? strip : null), contains: (n) => n === btn || n === mod };
  const btn = { dataset: { tip: "Colgar un momento: R" }, closest: (s) => (s === "[data-tip]" ? btn : s === ".mod" ? mod : null) };
  showHelp(btn);
  assert.equal(strip.textContent, "Colgar un momento: R");
  leaveHelp(mod, btn);
  assert.equal(strip.textContent, "Colgar un momento: R", "still inside the module");
  leaveHelp(mod, null);
  assert.equal(strip.textContent, "");
  const outside = { dataset: { tip: "x" }, closest: (s) => (s === "[data-tip]" ? outside : null) };
  assert.equal(showHelp(outside), false, "a tip outside the modules floats as before");
});
