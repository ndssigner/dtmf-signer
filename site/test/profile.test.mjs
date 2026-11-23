import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { buildInto, brokenLinks, text, SITE } from "./helpers.mjs";

const files = buildInto();
const BUILDS = ["cable-qr", "pex"];
// "" is the simulator's tab (▶ Try it), at the build's own address.
const TABS = ["", "what/", "parts/", "build/", "gallery/", "status/", "downloads/"];
const URTONES = { spec: "https://github.com/ndssigner/ur-tones/blob/main/SPEC.md", web: "https://ndssigner.github.io/ur-tones/" };

test("every tab page exists for both builds and languages", () => {
  for (const lang of ["en", "es"]) for (const b of BUILDS) for (const t of TABS)
    assert.ok(files.has(`${lang}/${b}/${t}index.html`), `${lang}/${b}/${t}`);
});
test("each tab page marks exactly one tab as the current one, its own", () => {
  for (const lang of ["en", "es"]) for (const b of BUILDS) for (const t of TABS) {
    const html = text(files, `${lang}/${b}/${t}index.html`);
    const nav = /<nav class="tabs" aria-label="[^"]+">([\s\S]*?)<\/nav>/.exec(html);
    assert.ok(nav, `${lang}/${b}/${t}: tab bar`);
    assert.equal([...html.matchAll(/aria-current="page"/g)].length, 1, `${lang}/${b}/${t}`);
    const current = /<a href="([^"]*)" aria-current="page"/.exec(nav[1]);
    assert.ok(current && current[1].endsWith(`${lang}/${b}/${t}`), `${lang}/${b}/${t}: ${current?.[1]}`);
    assert.equal([...nav[1].matchAll(/<a /g)].length, TABS.length);
  }
});
test("language switch keeps the tab", () => {
  assert.match(text(files, "en/cable-qr/parts/index.html"), /href="\.\.\/\.\.\/\.\.\/es\/cable-qr\/parts\/" hreflang="es"/);
  assert.match(text(files, "es/pex/status/index.html"), /href="\.\.\/\.\.\/\.\.\/en\/pex\/status\/" hreflang="en"/);
});
test("the build's header: pictogram, title, one line", () => {
  const html = text(files, "es/cable-qr/what/index.html");
  assert.match(html, /<img src="\.\.\/\.\.\/\.\.\/img\/builds\/cable-qr\.svg" alt="">/);
  assert.match(html, /<h1>Cable y QR<\/h1>/);
  assert.match(html, /<p class="lede">Escucha por cable/);
  assert.match(text(files, "en/pex/index.html"), /img\/builds\/pex\.svg/);
});
test("the pictograms: small line drawings, no text", () => {
  for (const b of BUILDS) {
    const svg = readFileSync(`${SITE}static/img/builds/${b}.svg`, "utf8");
    assert.ok(Buffer.byteLength(svg) < 2048, `${b}: ${Buffer.byteLength(svg)} bytes`);
    assert.match(svg, /viewBox="0 0 120 80"/);
    assert.match(svg, /stroke="currentColor"/);
    assert.doesNotMatch(svg, /<text/);
    assert.ok(files.get(`img/builds/${b}.svg`)?.equals(Buffer.from(svg)), b);
  }
});
test("the parts tab shows the parts list from bom.csv", () => {
  const html = text(files, "en/cable-qr/parts/index.html");
  assert.match(html, /<table class="bom">/);
  assert.match(html, /Raspberry Pi Pico \(RP2040\)/);
  assert.match(html, /<td>22k<\/td>/);
  assert.match(text(files, "es/cable-qr/parts/index.html"), /<th>Pieza<\/th>/);
});
test("the build tab shows the layout and offers its files", () => {
  const html = text(files, "en/cable-qr/build/index.html");
  assert.match(html, /<img [^>]*src="[^"]*breadboard-layout\.png"/);
  assert.match(html, /<h2 id="layout">/);
  assert.match(html, /href="[^"]*files\/cable-qr\/dtmf-signer-breadboard\.diy"/);
  assert.match(html, /href="[^"]*files\/cable-qr\/template-es\.pdf"/);
  assert.ok(files.has("files/cable-qr/dtmf-signer-breadboard.diy"));
  assert.match(html, /<h2 id="schematic">[\s\S]*<img class="schematic" src="[^"]*img\/cable-qr\/cable-qr\.svg"[\s\S]*<h2 id="layout">/);
  assert.match(html, /href="[^"]*files\/cable-qr\/cable-qr\.kicad_sch"/);
  assert.ok(files.has("img/cable-qr/cable-qr.svg"));
});
test("the PEX: its parts list, and no drawn layout yet, which its build tab says, with its guide", () => {
  for (const lang of ["en", "es"]) {
    assert.match(text(files, `${lang}/pex/parts/index.html`), /<table class="bom">[\s\S]*RJ11 socket/, lang);
    const html = text(files, `${lang}/pex/build/index.html`);
    assert.match(html, /<p class="not-yet">[^<]*<a href="\.\.\/\.\.\/\.\.\/(en|es)\/pex\/status\/">/, lang);
    assert.match(html, new RegExp(`hardware/phone/README${lang === "es" ? "\\.es" : ""}\\.md`), lang);
  }
});
test("the PEX's build tab shows its KiCad schematic and offers its source", () => {
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/pex/build/index.html`);
    assert.match(html, /<h2 id="schematic">/, lang);
    assert.match(html, /<img class="schematic" src="[^"]*img\/pex\/pex\.svg" alt="[^"]+">/, lang);
    assert.match(html, /href="[^"]*files\/pex\/pex\.kicad_sch"/, lang);
  }
  assert.ok(files.has("img/pex/pex.svg"));
  assert.ok(files.has("files/pex/pex.kicad_sch"));
});
test("the simulator's tab names its profile and holds a place for the simulator", () => {
  const html = text(files, "en/cable-qr/index.html");
  assert.match(html, /PROFILE-CABLE-QR/);
  assert.match(html, /class="simulator"/);
  assert.match(html, /abc1234/);
});
test("only the simulator's tab may run WebAssembly", () => {
  for (const t of TABS.slice(1)) assert.doesNotMatch(text(files, `en/cable-qr/${t}index.html`), /wasm-unsafe-eval/, t);
});
test("the build tab links to the repo in the page's language", () => {
  assert.match(text(files, "en/cable-qr/build/index.html"), /blob\/main\/docs\/catalog\.md#profile-cable-qr--cable-and-qr/);
  assert.match(text(files, "es/cable-qr/build/index.html"), /blob\/main\/docs\/es\/catalog\.md/);
  assert.match(text(files, "es/cable-qr/build/index.html"), /blob\/main\/hardware\/breadboard\/README\.es\.md/);
});
test("«What it does» mentions ur-tones with a link", () => {
  for (const lang of ["en", "es"]) for (const b of BUILDS) {
    const main = text(files, `${lang}/${b}/what/index.html`).match(/<main>[\s\S]*<\/main>/)[0];
    assert.ok(main.includes(`href="${URTONES.spec}"`) || main.includes(`href="${URTONES.web}"`), `${lang}/${b}`);
    assert.match(main, /ur-tones/);
  }
});
test("the home page lists the build", () => {
  assert.match(text(files, "en/index.html"), /href="cable-qr\/"/);
});
test("links still resolve", () => {
  assert.deepEqual(brokenLinks(files), []);
});

// A build's old address (moved_from in config.json) still leads to it: each
// of its tabs, in each language, a page that sends the visitor on.
test("the PEX's old address, /centralita/, sends each tab on to /pex/", () => {
  for (const lang of ["en", "es"]) for (const t of TABS) {
    const path = `${lang}/centralita/${t}index.html`;
    assert.ok(files.has(path), path);
    const html = text(files, path);
    const to = `${"../".repeat(1 + t.split("/").filter(Boolean).length)}pex/${t}`;
    assert.match(html, new RegExp(`<meta http-equiv="refresh" content="0; url=${to.replace(/\./g, "\\.")}">`), path);
    assert.match(html, new RegExp(`<a href="${to.replace(/\./g, "\\.")}">`), path);
    assert.match(html, /<meta name="robots" content="noindex">/, path);
  }
});
