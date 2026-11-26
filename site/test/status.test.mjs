import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { parseStatus, renderStatusSequence } from "../lib/status.mjs";
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
// The class each state of status.json gets on the page.
const CLASS = { done: "done", doing: "wip", planned: "planned", na: "na" };
test("the status tab shows each part as a sequence of stages, translated, with notes and links", () => {
  for (const [b, dir] of [["cable-qr", "breadboard"], ["pex", "phone"]]) {
    const status = JSON.parse(readFileSync(new URL(`../../hardware/${dir}/status.json`, import.meta.url), "utf8"));
    const html = text(files, `es/${b}/status/index.html`);
    const lists = [...html.matchAll(/<ol class="stages">([\s\S]*?)<\/ol>/g)].map((m) => m[1]);
    assert.equal(lists.length, 2, b);
    lists.forEach((ol, i) => {
      const part = ["hardware", "firmware"][i];
      const got = [...ol.matchAll(/<li class="([^"]+)">/g)].map((m) => m[1]);
      assert.deepEqual(got, status[part].map((s) => CLASS[s.state]), `${b} ${part}`);
    });
  }
  const html = text(files, "es/cable-qr/status/index.html");
  assert.match(html, /Placa perforada/);
  assert.match(html, /SPICE/);
  assert.match(html, /href="https:\/\/github\.com\/ndssigner\/dtmf-signer\/releases\/tag\/v0\.4\.0-alpha"/);
});
test("a stage in progress is marked wip", () => {
  const s = ok(); s.hardware[1].state = "doing"; s.firmware[0].state = "done";
  const html = renderStatusSequence(s, (k) => k);
  assert.match(html, /<li class="wip">[\s\S]*?status\.hardware\.simulation/);
  assert.match(html, /<li class="done">[\s\S]*?status\.firmware\.core/);
});
test("links still resolve", () => {
  assert.deepEqual(brokenLinks(files), []);
});
