import { test } from "node:test";
import assert from "node:assert/strict";
import { buildInto, brokenLinks, text, FIX } from "./helpers.mjs";
import { HAVE_MODULE, REQUIRED, SKIP } from "./wasm-helpers.mjs";
import { CSP } from "../lib/page.mjs";
import { releaseOnBlur, start } from "../static/sim/ui.mjs";

const files = buildInto();
const SIM_CSP = CSP.replace("script-src 'self'", "script-src 'self' 'wasm-unsafe-eval'");

test("the page carries the simulator, its data and its scripts", { skip: SKIP }, () => {
  const html = text(files, "en/cable-qr/index.html");
  const json = /<script type="application\/json" id="sim-data">([\s\S]*?)<\/script>/.exec(html);
  assert.ok(json, "sim-data");
  const data = JSON.parse(json[1]);
  assert.equal(data.pin, "1234");
  assert.match(data.psbt, /^cHNidP8BAHECAAAAAX4gBlDy3j1VOEPCv1EQt1Y5OeRbmPWZVrdbpYPwIXfE/);
  assert.equal(data.seed, "00000000000000000000000000000000");
  assert.match(html, /<canvas [^>]*width="128" height="64"/);
  for (const b of ["left", "up", "ok", "down", "right"]) assert.match(html, new RegExp(`data-btn="${b}"`));
  assert.match(html, /class="sim-warning"/);
  assert.match(html, /<script type="module" src="\.\.\/\.\.\/sim\/ui\.mjs"><\/script>/);
  for (const f of ["sim/ui.mjs", "sim/core.mjs", "sim/wallet.mjs", "sim/dtmf-signer.mjs", "sim/ur-tones.js", "sim/ur-tones-crypto.js"])
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
test("a blur releases the button", () => {
  const sim = { held: 3, release() { this.held = 0; } };
  const target = new EventTarget();
  releaseOnBlur(sim, target);
  target.dispatchEvent(new Event("blur"));
  assert.equal(sim.held, 0);
});
test("if the module cannot load, the page says so", async () => {
  const status = { textContent: "" };
  const doc = {
    getElementById: (id) => (id === "sim-data" ? { textContent: JSON.stringify({ strings: { unavailable: "Not available" } }) } : null),
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
