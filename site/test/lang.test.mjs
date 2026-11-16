import { test } from "node:test";
import assert from "node:assert/strict";
import { pickLanguage } from "../static/lang.js";

test("pickLanguage", () => {
  assert.equal(pickLanguage(["es-MX"], ["en", "es"]), "es");
  assert.equal(pickLanguage(["pt-BR"], ["en", "es"]), "en");
  assert.equal(pickLanguage([], ["en", "es"]), "en");
  assert.equal(pickLanguage(["fr", "es"], ["en", "es"]), "es");
});
test("importing lang.js without a DOM does nothing", async () => {
  await import("../static/lang.js");
});
