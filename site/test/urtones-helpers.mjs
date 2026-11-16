// The ur-tones JavaScript (the web tool's, from the submodule) in Node, the
// way its own tests load it: classic scripts in a vm context.
import { readFileSync } from "node:fs";
import vm from "node:vm";

const SRC = new URL("../../third_party/ur-tones/web/src/", import.meta.url);

export function loadUrTones() {
  const ctx = vm.createContext({ TextEncoder, TextDecoder });
  for (const f of ["crypto.js", "ur-tones.js"])
    vm.runInContext(readFileSync(new URL(f, SRC), "utf8"), ctx, { filename: f });
  return vm.runInContext("UrTones", ctx);
}
