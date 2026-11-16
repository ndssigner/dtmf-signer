import { test } from "node:test";
import assert from "node:assert/strict";
import { renderMarkdown, escapeHtml } from "../lib/markdown.mjs";

test("headings with slug ids", () => {
  assert.equal(renderMarkdown("## Qué es"), '<h2 id="que-es">Qué es</h2>');
});
test("paragraphs and inline marks", () => {
  assert.equal(renderMarkdown("a **b** *c* `d`\n\ne"),
    "<p>a <strong>b</strong> <em>c</em> <code>d</code></p>\n<p>e</p>");
});
test("lines of one paragraph are joined", () => {
  assert.equal(renderMarkdown("a\nb"), "<p>a\nb</p>");
});
test("lists", () => {
  assert.equal(renderMarkdown("- uno\n- dos"), "<ul><li>uno</li><li>dos</li></ul>");
});
test("links: relative, http(s), anchors", () => {
  assert.equal(renderMarkdown("[x](../es/)"), '<p><a href="../es/">x</a></p>');
  assert.equal(renderMarkdown("[x](#a)"), '<p><a href="#a">x</a></p>');
  assert.match(renderMarkdown("[x](https://github.com/a)"), /href="https:\/\/github.com\/a" rel="noopener noreferrer"/);
});
test("raw HTML is escaped", () => {
  assert.equal(renderMarkdown("<script>alert(1)</script>"), "<p>&lt;script&gt;alert(1)&lt;/script&gt;</p>");
});
test("javascript: links are not links", () => {
  assert.doesNotMatch(renderMarkdown("[x](javascript:alert(1))"), /<a /);
  assert.doesNotMatch(renderMarkdown("[x](JavaScript:alert(1))"), /<a /);
});
test("quotes inside a URL cannot break the attribute", () => {
  assert.doesNotMatch(renderMarkdown('[x](a"onmouseover="b)'), /onmouseover="/);
});
test("code is not formatted inside", () => {
  assert.equal(renderMarkdown("`**x**`"), "<p><code>**x**</code></p>");
});
test("escapeHtml", () => {
  assert.equal(escapeHtml(`<a href="x">'&`), "&lt;a href=&quot;x&quot;&gt;&#39;&amp;");
});
