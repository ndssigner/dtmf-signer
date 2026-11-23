import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { buildInto, brokenLinks, text } from "./helpers.mjs";

const files = buildInto();
// Pinned: GitHub's /releases/latest skips pre-releases, and every release so far is one.
const REL = "https://github.com/ndssigner/dtmf-signer/releases/download/v0.3.0-alpha/";

test("downloads page in each language with the latest release's files", () => {
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/downloads/index.html`);
    assert.ok(html.includes(`href="${REL}dtmf-signer-pico.uf2"`), lang);
    assert.ok(html.includes(`href="${REL}SHA256SUMS"`), lang);
    assert.ok(html.includes(`href="${REL}dtmf-signer-pex.uf2"`), lang);
    assert.match(html, /v0\.3\.0-alpha/, lang);
    assert.ok(html.includes('href="https://github.com/ndssigner/dtmf-signer/releases"'), lang);
  }
});
test("the downloads page lists each build: its firmware, packs with hash, files", () => {
  const bytes = readFileSync(new URL("../../app/voice/packs/es.bin", import.meta.url));
  const sha = createHash("sha256").update(bytes).digest("hex");
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/downloads/index.html`);
    const sec = (slug) => new RegExp(`<section class="dl-build" id="${slug}">([\\s\\S]*?)</section>`).exec(html)?.[1];
    const a = sec("cable-qr"), c = sec("pex");
    assert.ok(a && c, lang);
    assert.ok(a.includes(`${REL}dtmf-signer-pico.uf2`), lang);
    assert.match(a, /files\/cable-qr\/dtmf-signer-breadboard\.diy/, lang);
    assert.ok(!c.includes("dtmf-signer-pico.uf2"), lang);
    assert.ok(c.includes(`${REL}dtmf-signer-pex.uf2`), lang);
    assert.ok(!a.includes("dtmf-signer-pex.uf2"), lang);
    const row = /<li class="pack">([\s\S]*?)<\/li>/.exec(c);
    assert.equal(/<code class="sha">([0-9a-f]{64})<\/code>/.exec(row?.[1])?.[1], sha, lang);
    assert.ok(row[1].includes(`${Math.round(bytes.length / 1024)} KiB`), lang);
    assert.match(c, /<p class="install"><a href="[^"]*ports\/pico\/README[^"]*#(voice-packs|paquetes-de-voz)"/, lang);
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
test("each build's downloads tab: its own firmware from the release, and its guide", () => {
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/cable-qr/downloads/index.html`);
    assert.ok(html.includes(`href="${REL}dtmf-signer-pico.uf2"`), lang);
    assert.ok(html.includes(`href="${REL}SHA256SUMS"`), lang);
    assert.match(html, /files\/cable-qr\/dtmf-signer-breadboard\.diy/, lang);
    assert.match(html, /<li><a href="[^"]*ports\/pico\/README[^"]*#(get-the-firmware|conseguir-el-firmware)"/, lang);
    const c = text(files, `${lang}/pex/downloads/index.html`);
    assert.ok(!c.includes(`${REL}dtmf-signer-pico.uf2`), lang);
    assert.ok(c.includes(`href="${REL}dtmf-signer-pex.uf2"`), lang);
    assert.ok(c.includes(`href="${REL}SHA256SUMS"`), lang);
    assert.doesNotMatch(c, /class="not-yet"/, lang);
    assert.match(c, /<li><a href="[^"]*ports\/pico\/README[^"]*#(the-phone-exchange-pex|la-centralita-pex)"/, lang);
  }
});
test("the PEX's downloads offer its installer with the three steps; Cable and QR has none", () => {
  for (const lang of ["en", "es"]) {
    const c = text(files, `${lang}/pex/downloads/index.html`);
    assert.match(c, /<a href="[^"]*files\/pex\/install_pex\.py" download>install_pex\.py<\/a>/, lang);
    const steps = /<ol class="install-steps">([\s\S]*?)<\/ol>/.exec(c)?.[1] ?? "";
    assert.equal((steps.match(/<li>/g) ?? []).length, 3, lang);
    assert.match(steps, /dtmf-signer-pex\.uf2/, lang);
    assert.match(steps, /BOOTSEL/, lang);
    assert.match(steps, /<code>python3 install_pex\.py<\/code>/, lang);
    assert.doesNotMatch(text(files, `${lang}/cable-qr/downloads/index.html`), /install_pex|install-steps/, lang);
  }
  assert.ok(files.get("files/pex/install_pex.py")?.equals(readFileSync(new URL("../../tools/install_pex.py", import.meta.url))));
});
test("the release names each build's UF2, and a build marked published has one", () => {
  const config = JSON.parse(readFileSync(new URL("../config.json", import.meta.url), "utf8"));
  const slugs = config.profiles.map((p) => p.slug);
  for (const slug of Object.keys(config.release.uf2)) assert.ok(slugs.includes(slug), slug);
  for (const p of config.profiles) {
    const status = JSON.parse(readFileSync(new URL(`../../${p.hardware}/status.json`, import.meta.url), "utf8"));
    const published = status.firmware.find((s) => s.stage === "release")?.state === "done";
    assert.equal(published, p.slug in config.release.uf2, `${p.slug}: its status and the release must agree`);
  }
});
test("the PEX's downloads tab: its voice pack, with size and full SHA-256", () => {
  const bytes = readFileSync(new URL("../../app/voice/packs/es.bin", import.meta.url));
  const sha = createHash("sha256").update(bytes).digest("hex");
  for (const [lang, check] of [["en", "Check it before installing it"], ["es", "Compruébalo antes de instalarlo"]]) {
    const html = text(files, `${lang}/pex/downloads/index.html`);
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
