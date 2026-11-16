import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { buildInto, brokenLinks, text, FIX } from "./helpers.mjs";
import { HAVE_MODULE, REQUIRED, SKIP } from "./wasm-helpers.mjs";
import { CSP } from "../lib/page.mjs";
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
  const yw = /<section class="blk blk-your-wallet[\s\S]*?<\/section>/.exec(html)[0];
  assert.match(yw, /<a class="yw-urtones" href="https:\/\/ndssigner\.github\.io\/ur-tones\/"/);
  assert.match(yw, /<a class="yw-scan" href="https:\/\/nunchuk\.io\/"/);
  assert.ok(yw.includes("Nunchuk"));
});

test("the Pico's own LED is on the signer, and only where the bench declares it", { skip: SKIP }, async () => {
  const html = text(files, "es/cable-qr/index.html");
  const signer = /<section class="blk blk-signer[\s\S]*?<\/section>/.exec(html)[0];
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
  assert.match(css, /\.bench\[data-orient="h"\] \.col-signer \.blk \{[^}]*padding-left: var\(--margin\)[^}]*padding-right: var\(--margin\)/);
});
