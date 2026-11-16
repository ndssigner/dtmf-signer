import { test } from "node:test";
import assert from "node:assert/strict";
import { buildInto, brokenLinks, text } from "./helpers.mjs";

const files = buildInto();
const REL = "https://github.com/ndssigner/dtmf-signer/releases/latest/download/";

test("downloads page in each language with the latest release's files", () => {
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/downloads/index.html`);
    assert.ok(html.includes(`href="${REL}dtmf-signer-pico.uf2"`), lang);
    assert.ok(html.includes(`href="${REL}SHA256SUMS"`), lang);
    assert.match(html, /href="\.\.\/cable-qr\/#layout"/, lang);
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
test("links still resolve", () => {
  assert.deepEqual(brokenLinks(files), []);
});
