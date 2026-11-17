import { test } from "node:test";
import assert from "node:assert/strict";
import { buildInto, brokenLinks, text, FIX } from "./helpers.mjs";

const CSP = "default-src 'none'; img-src 'self'; style-src 'self'; script-src 'self'; base-uri 'none'; form-action 'none'";

test("deterministic", () => {
  const a = buildInto(), b = buildInto();
  assert.deepEqual([...a.keys()].sort(), [...b.keys()].sort());
  for (const [k, v] of a) assert.ok(v.equals(b.get(k)), k);
});
test("every internal href/src resolves", () => {
  assert.deepEqual(brokenLinks(buildInto()), []);
});
test("CSP and no external resources", () => {
  for (const [path, bytes] of buildInto()) {
    if (!path.endsWith(".html")) continue;
    const html = bytes.toString("utf8");
    // Only a page that runs the simulator may also run WebAssembly.
    const want = html.includes('id="sim-data"') ? CSP.replace("script-src 'self'", "script-src 'self' 'wasm-unsafe-eval'") : CSP;
    assert.ok(html.includes(`content="${want}"`), path);
    assert.doesNotMatch(html, /src="https?:/, path);
    assert.doesNotMatch(html, /<link[^>]*href="https?:/, path);
  }
});
test("root lists every language without JS", () => {
  const html = text(buildInto(), "index.html");
  assert.match(html, /<a [^>]*href="en\/"/);
  assert.match(html, /<a [^>]*href="es\/"/);
  assert.match(html, /data-langs="en es"/);
  assert.match(html, /<script type="module" src="lang.js"><\/script>/);
});
test("home pages in each language", () => {
  const files = buildInto();
  assert.match(text(files, "en/index.html"), /<html lang="en"/);
  assert.match(text(files, "es/index.html"), /<html lang="es"/);
  assert.match(text(files, "es/index.html"), /Montajes/);
  assert.doesNotMatch(text(files, "es/index.html"), /not translated/);
});
test("an incomplete language builds with English and the notice", () => {
  const files = buildInto(FIX);
  const xx = text(files, "xx/index.html");
  assert.match(xx, /The fixture home page/);
  assert.match(xx, /Part of this page is not translated yet/);
  assert.match(xx, /TRANSLATING\.md/);
});
test("footer shows the commit", () => {
  assert.match(text(buildInto(), "en/index.html"), /abc1234/);
});
test("no page repeats an id", () => {
  for (const [path, bytes] of buildInto()) {
    if (!path.endsWith(".html")) continue;
    const ids = [...bytes.toString("utf8").matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]);
    assert.deepEqual(ids.filter((id, i) => ids.indexOf(id) !== i), [], path);
  }
});

const header = (files, path) => text(files, path).match(/<header[\s\S]*?<\/header>/)[0];
const URTONES = { spec: "https://github.com/ndssigner/ur-tones/blob/main/SPEC.md", web: "https://ndssigner.github.io/ur-tones/" };
test("the header has builds, downloads, specification and the other language", () => {
  const files = buildInto();
  const en = header(files, "en/index.html"), es = header(files, "es/index.html");
  assert.match(en, /<nav class="site">[\s\S]*href="\.\.\/en\/#builds">Builds<[\s\S]*href="\.\.\/en\/downloads\/">Downloads<[\s\S]*SPEC\.md[^>]*>Specification</);
  assert.match(es, /href="\.\.\/es\/#builds">Montajes</);
  assert.match(es, /href="\.\.\/es\/downloads\/">Descargas</);
  assert.match(es, /blob\/main\/docs\/es\/especificacion\.md"[^>]*>Especificaci\u00f3n</);
  assert.match(es, /href="\.\.\/en\/" hreflang="en"/);
  assert.match(en, /href="\.\.\/es\/" hreflang="es"/);
  assert.equal([...en.matchAll(/<a /g)].length, 5, "brand, three links, one language");
});
test("the footer links to the ur-tones spec and web tool", () => {
  const files = buildInto();
  for (const [path, label] of [["en/index.html", "Speaks ur-tones"], ["es/downloads/index.html", "Habla ur-tones"]]) {
    const foot = text(files, path).match(/<footer[\s\S]*?<\/footer>/)[0];
    assert.ok(foot.includes(label), path);
    assert.ok(foot.includes(`href="${URTONES.spec}"`), path);
    assert.ok(foot.includes(`href="${URTONES.web}"`), path);
    assert.match(foot, /abc1234/);
  }
});
test("the language switch is a dropdown that keeps the page", () => {
  const files = buildInto();
  const html = text(files, "es/cable-qr/parts/index.html");
  const m = /<details class="langs">\s*<summary[^>]*>([\s\S]*?)<\/summary>\s*<ul>([\s\S]*?)<\/ul>\s*<\/details>/.exec(html);
  assert.ok(m, "details.langs with a summary and a list");
  assert.match(m[1], /Español/);
  assert.match(m[2], /<a href="\.\.\/\.\.\/\.\.\/en\/cable-qr\/parts\/" hreflang="en" data-lang="en"[^>]*>English<\/a>/);
  assert.match(m[2], /aria-current="true"[^>]*>Español/);
});
