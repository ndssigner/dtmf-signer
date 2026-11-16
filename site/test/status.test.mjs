import { test } from "node:test";
import assert from "node:assert/strict";
import { parseStatus } from "../lib/status.mjs";
import { buildInto, brokenLinks, text } from "./helpers.mjs";

const ok = () => ({
  hardware: ["design", "simulation", "breadboard", "perfboard", "pcb"].map((stage) => ({ stage, state: "planned" })),
  firmware: ["core", "port", "simulator", "release", "review"].map((stage) => ({ stage, state: "planned" })),
});

test("a complete status parses", () => {
  assert.equal(parseStatus(JSON.stringify(ok())).hardware.length, 5);
});
test("stages are the fixed lists, in order", () => {
  const s = ok(); s.hardware.reverse();
  assert.throws(() => parseStatus(JSON.stringify(s)), /hardware/);
  const t = ok(); t.firmware.pop();
  assert.throws(() => parseStatus(JSON.stringify(t)), /firmware/);
});
test("unknown states are refused", () => {
  const s = ok(); s.hardware[0].state = "almost";
  assert.throws(() => parseStatus(JSON.stringify(s)), /almost/);
});
test("links must be http(s)", () => {
  const s = ok(); s.firmware[3].link = "javascript:alert(1)";
  assert.throws(() => parseStatus(JSON.stringify(s)), /link/);
});

const files = buildInto();
test("the build page shows its status, translated, with notes and links", () => {
  const html = text(files, "es/cable-qr/index.html");
  assert.match(html, /<h2 id="status">/);
  assert.match(html, /Placa perforada/);
  assert.match(html, /class="state done"/);
  assert.match(html, /class="state planned"/);
  assert.match(html, /href="https:\/\/github\.com\/ndssigner\/dtmf-signer\/releases\/tag\/v0\.2\.0-alpha"/);
});
test("links still resolve", () => {
  assert.deepEqual(brokenLinks(files), []);
});
