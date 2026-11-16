import { test } from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { SKIP, load, lines, tap } from "./wasm-helpers.mjs";

test("boots to the welcome screen", { skip: SKIP }, async () => {
  const m = await load();
  m._dsw_init();
  m._dsw_advance(1000, 0);
  assert.match(lines(m).join("\n"), /DTMF-Signer/);
  assert.equal(m._dsw_take_dirty(), 1);
});
test("a short OK press is a PRESS: past the welcome to the menu", { skip: SKIP }, async () => {
  const m = await load();
  m._dsw_init();
  m._dsw_advance(500, 0);
  tap(m, 3);
  assert.match(lines(m).join("\n"), /Load seed/);
});
test("the version is the git one", { skip: SKIP }, async () => {
  // The wasm sits base64-encoded inside the .mjs, so ask the module itself.
  const m = await load();
  const v = execFileSync("git", ["describe", "--tags", "--always", "--dirty"], { encoding: "utf8" }).trim();
  assert.equal(m.UTF8ToString(m._dsw_version()), v);
});
