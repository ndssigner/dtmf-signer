import { test } from "node:test";
import assert from "node:assert/strict";
import { buildInto, brokenLinks, text } from "./helpers.mjs";

const files = buildInto();

test("the Cable and QR page in each language", () => {
  for (const lang of ["en", "es"]) assert.ok(files.has(`${lang}/cable-qr/index.html`), lang);
});
test("it shows the parts list from bom.csv", () => {
  const html = text(files, "en/cable-qr/index.html");
  assert.match(html, /<table class="bom">/);
  assert.match(html, /Raspberry Pi Pico \(RP2040\)/);
  assert.match(html, /<td>22k<\/td>/);
  assert.match(text(files, "es/cable-qr/index.html"), /<th>Pieza<\/th>/);
});
test("it shows the layout and offers its files", () => {
  const html = text(files, "en/cable-qr/index.html");
  assert.match(html, /<img [^>]*src="[^"]*breadboard-layout\.png"/);
  assert.match(html, /href="[^"]*files\/dtmf-signer-breadboard\.diy"/);
  assert.match(html, /href="[^"]*files\/template-es\.pdf"/);
  assert.ok(files.has("files/dtmf-signer-breadboard.diy"));
});
test("it names its profile and holds a place for the simulator", () => {
  const html = text(files, "en/cable-qr/index.html");
  assert.match(html, /PROFILE-CABLE-QR/);
  assert.match(html, /class="simulator"/);
  assert.match(html, /abc1234/);
});
test("details link to the repo in the page's language", () => {
  assert.match(text(files, "en/cable-qr/index.html"), /blob\/main\/docs\/catalog\.md#profile-cable-qr--cable-and-qr/);
  assert.match(text(files, "es/cable-qr/index.html"), /blob\/main\/docs\/es\/catalogo\.md/);
  assert.match(text(files, "es/cable-qr/index.html"), /blob\/main\/hardware\/breadboard\/README\.es\.md/);
});
test("the home page lists the build", () => {
  assert.match(text(files, "en/index.html"), /href="cable-qr\/"/);
});
test("links still resolve", () => {
  assert.deepEqual(brokenLinks(files), []);
});
