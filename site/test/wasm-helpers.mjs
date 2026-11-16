// Loads the firmware built as WebAssembly (tools/build-wasm.sh), if it is there.
import { existsSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";

export const MODULE = fileURLToPath(new URL("../../build/wasm/dtmf-signer.mjs", import.meta.url));
export const HAVE_MODULE = existsSync(MODULE);
// CI sets SIM_REQUIRED=1: there a missing module fails the tests, never skips them.
export const REQUIRED = process.env.SIM_REQUIRED === "1";
export const SKIP = HAVE_MODULE || REQUIRED ? false : "build/wasm/dtmf-signer.mjs missing: run tools/build-wasm.sh";

export async function load() {
  const factory = (await import(pathToFileURL(MODULE).href)).default;
  return factory();
}

export function lines(m) {
  const out = [];
  for (let r = 0; r < m._dsw_rows(); r++) out.push(m.UTF8ToString(m._dsw_line(r)));
  return out;
}

// Holds a button class for ms, then releases it for ms, in 16 ms steps.
export function tap(m, cls, holdMs = 100, restMs = 100) {
  for (let t = 0; t < holdMs; t += 16) m._dsw_advance(16, cls);
  for (let t = 0; t < restMs; t += 16) m._dsw_advance(16, 0);
}
