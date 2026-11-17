import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";

const MODULE = fileURLToPath(new URL("../../build/wasm/dtmf-signer-voice.mjs", import.meta.url));
const SKIP = existsSync(MODULE) || process.env.SIM_REQUIRED === "1"
  ? false : "build/wasm/dtmf-signer-voice.mjs missing: run tools/build-wasm.sh";

async function load() {
  return (await import(pathToFileURL(MODULE).href)).default();
}

// Every echo line so far.
function echoes(m) {
  const out = [];
  while (m._dvw_take_echo() > 0) out.push(m.UTF8ToString(m._dvw_echo()));
  return out;
}

// Advances the clock in 10 ms steps, pulling the line-out samples as a sound card would.
function run(m, ms, collect) {
  for (let t = 0; t < ms; t += 10) {
    m._dvw_advance(10);
    const n = m._dvw_out(160);
    collect?.push(...m.HEAP16.subarray(m._dvw_out_buf() >> 1, (m._dvw_out_buf() >> 1) + n));
  }
}

test("voice wasm greets on off-hook", { skip: SKIP }, async () => {
  const m = await load();
  m._dvw_init();
  run(m, 100);
  m._dvw_hook(1);
  const got = [];
  run(m, 3000, null);
  got.push(...echoes(m));
  assert.ok(got.includes("say greeting"), got.join("|"));
});

test("out samples are the greeting", { skip: SKIP }, async () => {
  const m = await load();
  m._dvw_init();
  run(m, 100);
  m._dvw_hook(1);
  const s = [];
  run(m, 3000, s);
  let loud = 0;
  for (const x of s) if (Math.abs(x) > 100) loud++;
  assert.ok(loud >= 8000, `${loud} non-silent samples`);   // 0.5 s at 16 kHz
});

test("the version comes from git", { skip: SKIP }, async () => {
  const m = await load();
  assert.match(m.UTF8ToString(m._dvw_version()), /^(v\d+\.\d+\.\d+\S*|[0-9a-f]{7,}(-dirty)?)$/);
});
