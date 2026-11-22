import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { buildInto, brokenLinks, text } from "./helpers.mjs";

const files = buildInto();
// Pinned: GitHub's /releases/latest skips pre-releases, and every release so far is one.
const REL = "https://github.com/ndssigner/dtmf-signer/releases/download/v0.2.0-alpha/";

test("downloads page in each language with the latest release's files", () => {
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/downloads/index.html`);
    assert.ok(html.includes(`href="${REL}dtmf-signer-pico.uf2"`), lang);
    assert.ok(html.includes(`href="${REL}SHA256SUMS"`), lang);
    assert.match(html, /v0\.2\.0-alpha/, lang);
    assert.ok(html.includes('href="https://github.com/ndssigner/dtmf-signer/releases"'), lang);
  }
});
test("the downloads page lists each build: firmware or not yet, packs with hash, files", () => {
  const bytes = readFileSync(new URL("../../app/voice/packs/es.bin", import.meta.url));
  const sha = createHash("sha256").update(bytes).digest("hex");
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/downloads/index.html`);
    const sec = (slug) => new RegExp(`<section class="dl-build" id="${slug}">([\\s\\S]*?)</section>`).exec(html)?.[1];
    const a = sec("cable-qr"), c = sec("centralita");
    assert.ok(a && c, lang);
    assert.ok(a.includes(`${REL}dtmf-signer-pico.uf2`), lang);
    assert.match(a, /files\/cable-qr\/dtmf-signer-breadboard\.diy/, lang);
    assert.ok(!c.includes("dtmf-signer-pico.uf2"), lang);
    assert.match(c, /<p class="not-yet">[^<]*<a href="\.\.\/centralita\/status\/">/, lang);
    const row = /<li class="pack">([\s\S]*?)<\/li>/.exec(c);
    assert.equal(/<code class="sha">([0-9a-f]{64})<\/code>/.exec(row?.[1])?.[1], sha, lang);
    assert.ok(row[1].includes(`${Math.round(bytes.length / 1024)} KiB`), lang);
    assert.match(c, /make_uf2\.py --offset/, lang);
    assert.match(c, lang === "es" ? /Compruébalo antes de instalarlo/ : /Check it before installing it/, lang);
    assert.ok(!a.includes('class="pack"'), lang);
  }
});
test("the navigation links to downloads", () => {
  assert.match(text(files, "en/index.html"), /href="\.\.\/en\/downloads\/"/);
});
test("donations on the home and downloads pages", () => {
  for (const page of ["en/index.html", "es/downloads/index.html"]) {
    const html = text(files, page);
    assert.match(html, /bc1qx5snc0wlc8cg9gwxhyx27y6pkru8rnngyq7uja/, page);
    assert.match(html, /ndssigner@coinos\.io/, page);
    assert.match(html, /img\/donate-bitcoin\.png/, page);
  }
});
test("a build's downloads tab: the release firmware when it has one", () => {
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/cable-qr/downloads/index.html`);
    assert.ok(html.includes(`href="${REL}dtmf-signer-pico.uf2"`), lang);
    assert.ok(html.includes(`href="${REL}SHA256SUMS"`), lang);
    assert.match(html, /files\/cable-qr\/dtmf-signer-breadboard\.diy/, lang);
    const c = text(files, `${lang}/centralita/downloads/index.html`);
    assert.ok(!c.includes(`${REL}dtmf-signer-pico.uf2`), lang);
    assert.match(c, /<p class="not-yet">[^<]*<a href="\.\.\/\.\.\/\.\.\/(en|es)\/centralita\/status\/">/, lang);
  }
});
test("the PEX's downloads tab: its voice pack, with size and full SHA-256", () => {
  const bytes = readFileSync(new URL("../../app/voice/packs/es.bin", import.meta.url));
  const sha = createHash("sha256").update(bytes).digest("hex");
  for (const [lang, check] of [["en", "Check it before installing it"], ["es", "Compruébalo antes de instalarlo"]]) {
    const html = text(files, `${lang}/centralita/downloads/index.html`);
    const row = /<li class="pack">([\s\S]*?)<\/li>/.exec(html);
    assert.ok(row, lang);
    assert.match(row[1], /href="[^"]*es\.bin"[^>]*>es\.bin</);
    assert.equal(/<code class="sha">([0-9a-f]{64})<\/code>/.exec(row[1])?.[1], sha, lang);
    assert.ok(row[1].includes(`${Math.round(bytes.length / 1024)} KiB`), lang);
    assert.ok(html.includes(check), lang);
  }
  assert.ok(files.get("sim/packs/es.bin")?.equals(bytes));
});
test("links still resolve", () => {
  assert.deepEqual(brokenLinks(files), []);
});
