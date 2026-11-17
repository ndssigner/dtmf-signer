import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { validateBench, checkBenchAgainstCatalog, profileCapabilities } from "../lib/bench.mjs";
import { orientationFor, linkPath } from "../static/sim/bench.mjs";

const CONFIG = new URL("../config.json", import.meta.url);
const CATALOG = new URL("../../docs/catalog.md", import.meta.url);
const catalog = readFileSync(CATALOG, "utf8");
const profile = () => structuredClone(JSON.parse(readFileSync(CONFIG, "utf8")).profiles.find((p) => p.slug === "cable-qr"));
const signer = (b) => b.modules.find((m) => m.id === "signer");

test("the real bench is valid and matches the catalog", () => {
  const p = profile();
  assert.deepEqual(validateBench(p.bench), []);
  assert.deepEqual(checkBenchAgainstCatalog(p.bench, catalog, p.id), []);
});
test("a capability the catalog does not list is refused", () => {
  const p = profile();
  signer(p.bench).capabilities.push("OUT-AIR");
  assert.ok(checkBenchAgainstCatalog(p.bench, catalog, p.id).some((e) => e.includes("OUT-AIR")));
});
test("a missing I/O capability is refused", () => {
  const p = profile();
  signer(p.bench).ports = signer(p.bench).ports.filter((x) => x.id !== "qr");
  for (const m of p.bench.modes) m.links = m.links.filter((l) => !l.from.startsWith("signer.qr"));
  assert.ok(checkBenchAgainstCatalog(p.bench, catalog, p.id).some((e) => e.includes("OUT-QR")));
});
test("a link between ports of different types is refused", () => {
  const p = profile();
  p.bench.modes[0].links.push({ from: "wallet.camera", to: "signer.in", channel: "x" });
  assert.ok(validateBench(p.bench).length > 0);
});
test("a link to a port that does not exist is refused", () => {
  const p = profile();
  p.bench.modes[0].links.push({ from: "wallet.nope", to: "signer.in", channel: "x" });
  assert.ok(validateBench(p.bench).some((e) => e.includes("wallet.nope")));
});
test("a link running against its ports' directions is refused", () => {
  const p = profile();
  p.bench.modes[0].links.push({ from: "signer.in", to: "wallet.jack", channel: "x" });
  assert.ok(validateBench(p.bench).length > 0);
});
test("orientation turns at 720 px", () => {
  assert.equal(orientationFor(719), "v");
  assert.equal(orientationFor(720), "h");
});
test("aligned ports give a straight line; others one right-angle elbow", () => {
  assert.equal(linkPath({ x: 0, y: 10 }, { x: 100, y: 10 }, "h"), "M0 10 H100");
  assert.equal(linkPath({ x: 0, y: 10 }, { x: 100, y: 40 }, "h"), "M0 10 H50 V40 H100");
  assert.equal(linkPath({ x: 10, y: 0 }, { x: 10, y: 100 }, "v"), "M10 0 V100");
  assert.equal(linkPath({ x: 10, y: 0 }, { x: 40, y: 100 }, "v"), "M10 0 V50 H40 V100");
});
test("profileCapabilities reads a wrapped line", () => {
  assert.deepEqual([...profileCapabilities(catalog, "PROFILE-CABLE-QR")].sort(),
    ["BTC-INPUT-AMOUNTS", "IN-BUTTONS", "IN-LINE", "OUT-QR", "TONES-SW", "VERIFY-SCREEN"]);
});
test("the build refuses a bench that contradicts the catalog", async () => {
  const { checkBenches } = await import("../build.mjs");
  const p = profile();
  signer(p.bench).capabilities.push("OUT-AIR");
  assert.throws(() => checkBenches([p], catalog), /OUT-AIR/);
  assert.doesNotThrow(() => checkBenches([profile()], catalog));
});

test("the signer's LED needs a pin and a board", () => {
  const p = profile();
  signer(p.bench).led = { pin: 3 };
  assert.ok(validateBench(p.bench).some((e) => e.includes("led")));
});

const centralita = () => structuredClone(JSON.parse(readFileSync(CONFIG, "utf8")).profiles.find((p) => p.slug === "centralita"));
const portOf = (b, id) => { const [m, p] = id.split("."); return b.modules.find((x) => x.id === m).ports.find((x) => x.id === p); };

test("sound through the air is its own port type; the telephone line stays phone", () => {
  const p = centralita();
  assert.deepEqual(validateBench(p.bench), []);
  for (const id of ["phone.mic", "phone.ear", "sim-wallet.speaker-near", "sim-wallet.mic-near", "your-wallet.speaker-near", "your-wallet.mic-near"])
    assert.equal(portOf(p.bench, id).type, "air", id);
  assert.equal(portOf(p.bench, "signer.line").type, "phone");
  assert.equal(portOf(p.bench, "phone.line").type, "phone");
  for (const m of p.bench.modes) for (const l of m.links) assert.equal(l.external, undefined, "the port's type says it, not a flag");
});
test("an air port linked to an audio port is refused", () => {
  const p = centralita();
  const wallet = p.bench.modules.find((m) => m.id === "sim-wallet");
  wallet.ports.find((x) => x.id === "speaker-near").type = "audio";
  assert.ok(validateBench(p.bench).some((e) => /audio to air/.test(e)), validateBench(p.bench).join("\n"));
  const q = profile();
  q.bench.modules.find((m) => m.id === "wallet").ports.push({ id: "spk", edge: "top", type: "air", dir: "out", label: "x" });
  q.bench.modes[0].links.push({ from: "wallet.spk", to: "signer.in", channel: "x" });
  assert.ok(validateBench(q.bench).some((e) => /air to audio/.test(e)));
});
test("an unknown port type is refused", () => {
  const p = profile();
  signer(p.bench).ports[0].type = "smoke";
  assert.ok(validateBench(p.bench).some((e) => e.includes("type smoke")));
});
