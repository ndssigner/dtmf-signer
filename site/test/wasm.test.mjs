import { test } from "node:test";
import assert from "node:assert/strict";
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
test("the version comes from git", { skip: SKIP }, async () => {
  // The wasm sits base64-encoded inside the .mjs, so ask the module itself.
  // Not compared with HEAD: any commit after the build would fail it.
  const m = await load();
  assert.match(m.UTF8ToString(m._dsw_version()), /^(v\d+\.\d+\.\d+\S*|[0-9a-f]{7,}(-dirty)?)$/);
});
test("it says when it listens and when it shows a QR", { skip: SKIP }, async () => {
  const m = await load();
  m._dsw_init();
  m._dsw_advance(500, 0);
  assert.equal(m._dsw_listening(), 0);
  assert.equal(m._dsw_qr_shown(), 0);
  assert.equal(m._dsw_qr_kind(), 0);
  tap(m, 3);                    // past the welcome
  tap(m, 3);                    // Load seed
  assert.equal(m._dsw_listening(), 1);
});
