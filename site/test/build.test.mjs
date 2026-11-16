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
  assert.match(text(files, "es/index.html"), /Inicio/);
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
