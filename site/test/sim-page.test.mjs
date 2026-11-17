import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { buildInto, brokenLinks, text, FIX } from "./helpers.mjs";
import { HAVE_MODULE, REQUIRED, SKIP } from "./wasm-helpers.mjs";
import { CSP } from "../lib/page.mjs";
import { validateBench } from "../lib/bench.mjs";
import { releaseOnBlur, releaseOnLeave, speedFor, start, isPhone, buttonsDown } from "../static/sim/ui.mjs";

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
  const sim = html.slice(html.indexOf('id="sim"'), html.indexOf('id="sim-data"'));
  assert.doesNotMatch(sim, /<ol>/);
  assert.doesNotMatch(sim, /sim\.step/);
  assert.match(sim, /data-tip="/);
});
test("both modes and every port of the bench are on the page", { skip: SKIP }, () => {
  const html = text(files, "en/cable-qr/index.html");
  for (const m of ["sim-wallet", "your-wallet"]) assert.match(html, new RegExp(`data-mode="${m}"`));
  for (const p of ["signer.5v", "signer.qr", "signer.in", "sim-wallet.camera", "sim-wallet.jack", "your-wallet.camera", "your-wallet.jack", "charger.usb"])
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
test("mode 2 says it is only for trying, in both languages", { skip: SKIP }, () => {
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

test("each mode's text alternative describes its own connections", { skip: SKIP }, () => {
  const html = text(files, "en/cable-qr/index.html");
  const yp = /<ul class="connections[^"]*" data-modes="your-wallet"[^>]*>([\s\S]*?)<\/ul>/.exec(html);
  assert.ok(yp, "your-wallet list");
  assert.match(yp[1], /microphone/i);
  assert.doesNotMatch(yp[1], /Tones by cable/);
});

test("the wallet is never called a phone (the telephone is the exchange's)", { skip: SKIP }, () => {
  const es = text(files, "es/cable-qr/index.html"), en = text(files, "en/cable-qr/index.html");
  for (const t of ["Wallet simulada", "Tu wallet", "Tu dispositivo con wallet"]) assert.ok(es.includes(t), t);
  for (const t of ["Simulated wallet", "Your wallet", "Your wallet device"]) assert.ok(en.includes(t), t);
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
  assert.match(html, /<p class="wd-check" aria-live="polite">/);
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
  const yw = /<section class="mod mod-your-wallet[\s\S]*?<\/section>/.exec(html)[0];
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

// ---- the Centralita (the voice build) ----
const VOICE_SKIP = existsSync(new URL("../../build/wasm/dtmf-signer-voice.mjs", import.meta.url)) || REQUIRED
  ? false : "build/wasm/dtmf-signer-voice.mjs missing: run tools/build-wasm.sh";
const voiceData = (lang) => JSON.parse(/<script type="application\/json" id="sim-data">([\s\S]*?)<\/script>/.exec(text(files, `${lang}/centralita/index.html`))[1]);

test("/es/centralita/ has the phone keypad and handset, no screen, its profile", { skip: VOICE_SKIP }, () => {
  const html = text(files, "es/centralita/index.html");
  const keys = [...html.matchAll(/data-key="([0-9*#])"/g)].map((m) => m[1]);
  assert.deepEqual(keys.sort(), [..."#*0123456789"].sort());
  assert.match(html, /<button type="button" class="ph-hook"[^>]*aria-pressed="false"/);
  assert.doesNotMatch(html, /<canvas/);
  assert.doesNotMatch(html, /data-btn=/);
  assert.match(html, /PROFILE-PHONE/);
  assert.ok(html.includes(`content="${SIM_CSP}"`));
  assert.match(html, /<script type="module" src="\.\.\/\.\.\/sim\/voice-ui\.mjs"><\/script>/);
});
test("the Centralita's bench passes the catalog", async () => {
  const { checkBenchAgainstCatalog, validateBench } = await import("../lib/bench.mjs");
  const p = JSON.parse(readFileSync(new URL("../config.json", import.meta.url), "utf8")).profiles.find((x) => x.slug === "centralita");
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
  assert.equal(es.phrases.es.menu_empty, "Cargar semilla, 1.");
  assert.equal(es.phrases.en.menu_empty, "Load seed, 1.");
  assert.equal(en.phrases.es, undefined);
  assert.equal(en.phrases.en.c_a, undefined, "letters come from the echo's own text");
  assert.match(text(files, "es/centralita/index.html"), /<p class="ph-caption"[^>]*aria-live="polite"/);
});
test("the simulated wallet by the handset: a PIN of 12 or more, a listen button, no camera", { skip: VOICE_SKIP }, () => {
  const html = text(files, "en/centralita/index.html");
  assert.ok(voiceData("en").pin.length >= 12);
  const w = /<section class="mod mod-sim-wallet[\s\S]*?<\/section>/.exec(html)[0];
  for (const c of ["wd-seed", "wd-keypad", "wd-psbt", "wd-listen", "wd-net", "wd-stop"]) assert.match(w, new RegExp(`class="${c}"`), c);
  assert.doesNotMatch(w, /wd-scan/);
  assert.match(html, /data-port="sim-wallet\.speaker-near"/);
  assert.match(html, /data-port="sim-wallet\.mic-near"/);
});
test("the telephone has a Type code button: off until lifted, a short tip, in both languages", { skip: VOICE_SKIP }, () => {
  for (const [lang, label] of [["en", "Type code"], ["es", "Teclear código"]]) {
    const html = text(files, `${lang}/centralita/index.html`);
    const phone = /<div class="ph-phone">[\s\S]*?<p class="ph-caption"/.exec(html)[0];
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
test("the Centralita credits its voices, and shows the board's LED", { skip: VOICE_SKIP }, () => {
  const es = text(files, "es/centralita/index.html"), en = text(files, "en/centralita/index.html");
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
test("the Centralita's status: designed, simulated; the breadboard and the port still to come", () => {
  const s = JSON.parse(readFileSync(new URL("../../hardware/phone/status.json", import.meta.url), "utf8"));
  const st = (part, stage) => s[part].find((x) => x.stage === stage).state;
  assert.equal(st("hardware", "design"), "done");
  assert.equal(st("hardware", "simulation"), "done");
  assert.equal(st("hardware", "breadboard"), "planned");
  assert.equal(st("firmware", "core"), "done");
  assert.equal(st("firmware", "port"), "planned");
  assert.equal(st("firmware", "simulator"), "done");
});

test("the Centralita offers the simulated wallet and your wallet, this computer as the handset", { skip: VOICE_SKIP }, () => {
  const html = text(files, "es/centralita/index.html");
  assert.match(html, /<div class="sim-modes" role="tablist"[^>]*>(?![^<]*hidden)/);
  for (const m of ["sim-wallet", "your-wallet"]) assert.match(html, new RegExp(`data-mode="${m}"`));
  const yw = /<section class="mod mod-your-wallet[\s\S]*?<\/section>/.exec(html)[0];
  assert.match(yw, /class="mic-toggle"/);
  assert.match(yw, /class="mic-level"/);
  assert.ok(yw.includes("Micrófono de este ordenador"));
  assert.ok(yw.includes("Altavoces de este ordenador"));
  assert.match(yw, /<a class="yw-urtones" href="https:\/\/ndssigner\.github\.io\/ur-tones\/"/);
  for (const p of ["your-wallet.speaker-near", "your-wallet.mic-near"]) assert.match(html, new RegExp(`data-port="${p.replace(".", "\\.")}"`));
  const p = JSON.parse(readFileSync(new URL("../config.json", import.meta.url), "utf8")).profiles.find((x) => x.slug === "centralita");
  assert.deepEqual(validateBench(p.bench), []);
});

// ---- the bench's visual language: neutral modules, coloured connectors and cables ----
const BENCHES = [["cable-qr", SKIP, ["power", "audio", "light"]], ["centralita", VOICE_SKIP, ["power", "phone", "air"]]];
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
    assert.ok(mods.length >= 4);
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
