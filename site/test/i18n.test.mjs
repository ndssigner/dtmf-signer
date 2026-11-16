import { test } from "node:test";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { loadSite, t, checkI18n } from "../lib/i18n.mjs";

const FIX = fileURLToPath(new URL("fixtures/i18n/", import.meta.url));
const SITE = fileURLToPath(new URL("../", import.meta.url));

test("t falls back to English", () => {
  const site = loadSite(FIX);
  assert.deepEqual(t(site, "xx", "nav.home"), { text: "Home", fallback: true });
  assert.deepEqual(t(site, "es", "nav.home"), { text: "Inicio", fallback: false });
});
test("t fills placeholders", () => {
  assert.equal(t(loadSite(FIX), "es", "footer.built", { commit: "abc1234" }).text, "Compilada desde abc1234");
});
test("t throws on a key English lacks", () => {
  assert.throws(() => t(loadSite(FIX), "es", "no.such"), /missing key no.such/);
});
test("checker: incomplete language warns, complete one errors", () => {
  const r = checkI18n(FIX);
  assert.ok(r.errors.some((e) => e.includes("es") && e.includes("nav.downloads")), r.errors.join("\n"));
  assert.ok(r.warnings.some((w) => w.includes("xx")), r.warnings.join("\n"));
  assert.ok(!r.errors.some((e) => e.includes("xx") && e.includes("nav.home")));
});
test("checker: a complete language lacking a content file is an error", () => {
  assert.ok(checkI18n(FIX).errors.some((e) => e.includes("es") && e.includes("home.md")));
});
test("checker: placeholder mismatch and extra keys are errors", () => {
  const r = checkI18n(FIX);
  assert.ok(r.errors.some((e) => e.includes("xx") && e.includes("footer.built")));
  assert.ok(r.errors.some((e) => e.includes("xx") && e.includes("extra.key")));
});
test("the real site passes", () => {
  assert.deepEqual(checkI18n(SITE).errors, []);
});
test("checker: every language needs its own name, even an incomplete one", () => {
  // fixture xx.json has lang.name; a language with none would show up as "English" in the switch
  const r = checkI18n(FIX);
  assert.ok(!r.errors.some((e) => e.includes("xx") && e.includes("lang.name")));
  assert.ok(r.errors.some((e) => e.includes("yy") && e.includes("lang.name")), r.errors.join("\n"));
});
test("a language code that is not a plain code is refused", async () => {
  const { mkdtempSync, cpSync, writeFileSync, readFileSync } = await import("node:fs");
  const { join } = await import("node:path");
  const { tmpdir } = await import("node:os");
  const dir = mkdtempSync(join(tmpdir(), "i18n-"));
  cpSync(FIX, dir, { recursive: true });
  const cfg = JSON.parse(readFileSync(join(dir, "config.json"), "utf8"));
  cfg.languages.push({ code: 'x" onload="y', complete: false });
  writeFileSync(join(dir, "config.json"), JSON.stringify(cfg));
  assert.throws(() => loadSite(dir), /language code/);
});
