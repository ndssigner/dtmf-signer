import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { buildInto, brokenLinks, text, SITE } from "./helpers.mjs";

const files = buildInto();
const TABS = ["", "what/", "parts/", "build/", "gallery/", "status/", "downloads/"];

test("the Nintendo DS build (no parts list, files, simulator or bench) has every tab in both languages", () => {
  for (const lang of ["en", "es"]) for (const t of TABS)
    assert.ok(files.has(`${lang}/nds/${t}index.html`), `${lang}/nds/${t}`);
  assert.deepEqual(brokenLinks(files), []);
});
test("a build whose hardware stages are all 'na' says the console is the hardware, not 'not yet'", () => {
  const en = text(files, "en/nds/parts/index.html");
  assert.match(en, /No parts to build: the console is the hardware/);
  assert.doesNotMatch(en, /Not ready yet/);
  assert.match(text(files, "es/nds/parts/index.html"), /Nada que montar: la consola es el hardware/);
  assert.doesNotMatch(text(files, "es/nds/parts/index.html"), /Aún no está listo/);
});
test("its build tab has no empty layout section, and its simulator tab is not 'coming soon'", () => {
  for (const lang of ["en", "es"]) {
    const build = text(files, `${lang}/nds/build/index.html`);
    assert.doesNotMatch(build, /id="layout"/, lang);
    assert.doesNotMatch(build, /not-yet/, lang);
    assert.doesNotMatch(text(files, `${lang}/nds/index.html`), /Coming soon|Próximamente/, lang);
  }
  assert.match(text(files, "en/nds/index.html"), /runs on a console/);
});
test("a build with parts still says 'not yet' where it has none (Cable and QR keeps its parts list)", () => {
  assert.match(text(files, "en/cable-qr/parts/index.html"), /<table class="bom">/);
});
test("the DS is on the home page with its air chip and a pictogram", () => {
  const html = text(files, "en/index.html");
  assert.match(html, /img\/builds\/nds\.svg/);
  assert.match(html, /in through the air/);
  const svg = readFileSync(`${SITE}static/img/builds/nds.svg`, "utf8");
  assert.ok(Buffer.byteLength(svg) < 2048);
  assert.match(svg, /viewBox="0 0 120 80"/);
  assert.doesNotMatch(svg, /<text/);
});
test("the DS's downloads: its .nds from the release, titled for the Nintendo DS", () => {
  const REL = "https://github.com/ndssigner/dtmf-signer/releases/download/v0.4.0-alpha/";
  for (const [lang, title] of [["en", /Firmware for the Nintendo DS, version v0\.4\.0-alpha/], ["es", /Firmware para la Nintendo DS, versión v0\.4\.0-alpha/]]) {
    const html = text(files, `${lang}/nds/downloads/index.html`);
    assert.ok(html.includes(`href="${REL}dtmf-signer-ds.nds"`), lang);
    assert.ok(html.includes(`href="${REL}SHA256SUMS"`), lang);
    assert.match(html, title, lang);
    assert.doesNotMatch(/<main>([\s\S]*)<\/main>/.exec(html)[1], /Raspberry Pi Pico/, lang);
    assert.doesNotMatch(html, /class="not-yet"/, lang);
    const all = text(files, `${lang}/downloads/index.html`);
    assert.ok(all.includes(`${REL}dtmf-signer-ds.nds`), lang);
  }
});
