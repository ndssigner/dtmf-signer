import { test } from "node:test";
import assert from "node:assert/strict";
import { SimPhone } from "../static/sim/phone.mjs";

function fakeSim(qr) { return { qrShown: () => qr, queued: 0, queueAudio() {} }; }
const el = () => ({ out: { value: "", hidden: true }, status: { textContent: "" } });

test("scanning with no QR on screen says so", () => {
  const ph = new SimPhone({ sim: fakeSim(false), U: null, data: {}, el: el() });
  assert.equal(ph.scan(), "no-qr");
});
test("a QR but nothing signed yet: still reading", () => {
  const ph = new SimPhone({ sim: fakeSim(true), U: null, data: {}, el: el() });
  assert.equal(ph.scan(), "waiting");
});
test("after the signed PSBT, scanning shows it", () => {
  const e = el();
  const ph = new SimPhone({ sim: fakeSim(true), U: null, data: {}, el: e });
  for (const l of ["signed-psbt-begin 1 3", "signed-psbt AQID", "signed-psbt-end 1"]) ph.onEcho(l);
  assert.equal(ph.scan(), "ok");
  assert.equal(e.out.value, "AQID");
  assert.equal(e.out.hidden, false);
});
