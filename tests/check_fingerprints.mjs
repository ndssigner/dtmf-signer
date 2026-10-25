// Prints the seed fingerprint of each hex entropy given on the command line,
// using ur-tones' own JavaScript (an implementation independent of the C
// core). It is the source of the fingerprints in vectors/keys-v0.json.
//     node tests/check_fingerprints.mjs 00000000000000000000000000000000
import { readFileSync } from "node:fs";
import vm from "node:vm";

const web = new URL("../third_party/ur-tones/web/", import.meta.url);
const ctx = vm.createContext({ TextEncoder, TextDecoder });
for (const f of ["src/bip39-english.js", "src/crypto.js"]) {
	vm.runInContext(readFileSync(new URL(f, web), "utf8"), ctx, { filename: f });
}
vm.runInContext("globalThis.C = UrTonesCrypto;", ctx);

for (const hex of process.argv.slice(2)) {
	if (!/^([0-9a-f]{2})+$/i.test(hex)) {
		console.error("not a hex string: " + hex);
		process.exit(2);
	}
	console.log(ctx.C.fingerprint(Uint8Array.from(Buffer.from(hex, "hex"))));
}
