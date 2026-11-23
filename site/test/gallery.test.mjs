import { test } from "node:test";
import assert from "node:assert/strict";
import { buildInto, text } from "./helpers.mjs";
import { galleryList, photoMetadata } from "../lib/gallery.mjs";

const files = buildInto();

test("Cable y QR's gallery links its two videos of real builds, opened elsewhere, not embedded", () => {
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/cable-qr/gallery/index.html`);
    for (const url of ["https://odysee.com/@ndssigner:9/demo-dtmf-signer:c", "https://odysee.com/@ndssigner:9/dtmf-signer-perfboard-v0-2-0:7"])
      assert.match(html, new RegExp(`<a href="${url.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}" rel="noopener noreferrer"`), `${lang}: ${url}`);
    assert.match(html, /<ul class="gallery">/);
    assert.doesNotMatch(html, /<iframe|<video/);
  }
});
test("a build without pictures yet gets no gallery list (its tab says so)", () => {
  assert.equal(galleryList([], (k) => k, "../", "x"), null);
  assert.equal(galleryList(undefined, (k) => k, "../", "x"), null);
});
test("the PEX's gallery: the breadboard prototype as tested, captioned in each language", () => {
  assert.ok(files.has("img/pex/gallery/pex-breadboard.jpg"));
  for (const [lang, words] of [["en", /earbuds/], ["es", /auriculares/]]) {
    const html = text(files, `${lang}/pex/gallery/index.html`);
    assert.match(html, /<li class="photo"><figure><img src="\.\.\/\.\.\/\.\.\/img\/pex\/gallery\/pex-breadboard\.jpg"/, lang);
    assert.match(html, /<figcaption>[\s\S]*<\/figcaption>/, lang);
    assert.match(html, words, lang);
    assert.doesNotMatch(html, /class="not-yet"/, lang);
  }
});
test("a photo with metadata (EXIF, GPS, text chunks) is refused; a clean one passes", () => {
  const jpegClean = Uint8Array.from([0xff, 0xd8, 0xff, 0xdb, 0x00, 0x04, 0, 0, 0xff, 0xd9]);
  const jpegExif = Uint8Array.from([0xff, 0xd8, 0xff, 0xe1, 0x00, 0x08, 0x45, 0x78, 0x69, 0x66, 0, 0, 0xff, 0xd9]);
  assert.equal(photoMetadata(jpegClean), null);
  assert.match(photoMetadata(jpegExif), /EXIF/i);
  const png = (type) => {
    const sig = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
    const chunk = (t, n = 0) => [0, 0, 0, n, ...[...t].map((c) => c.charCodeAt(0)), ...Array(n).fill(0), 0, 0, 0, 0];
    return Uint8Array.from([...sig, ...chunk("IHDR", 13), ...(type ? chunk(type, 2) : []), ...chunk("IEND")]);
  };
  assert.equal(photoMetadata(png()), null);
  for (const t of ["eXIf", "tEXt", "iTXt", "zTXt"]) assert.match(photoMetadata(png(t)), new RegExp(t));
});
