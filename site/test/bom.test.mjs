import { test } from "node:test";
import assert from "node:assert/strict";
import { parseCsv, bomTable } from "../lib/bom.mjs";

test("quoted fields", () => {
  assert.deepEqual(parseCsv('part,qty\n"a, b",2\n'), [{ part: "a, b", qty: "2" }]);
});
test("doubled quotes inside quotes", () => {
  assert.deepEqual(parseCsv('part\n"1.3"" OLED"\n'), [{ part: '1.3" OLED' }]);
});
test("table escapes and formats price", () => {
  const html = bomTable([{ part: "<b>", qty: "1", value: "", notes: "", price_usd: "3.5-15" }], (k) => k);
  assert.match(html, /&lt;b&gt;/);
  assert.match(html, /~3\.5–15 US\$/);
  assert.match(html, /<th>bom\.part<\/th>/);
});
