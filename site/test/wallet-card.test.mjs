// The wallet's card: what you need for what you just did, built from data,
// everything escaped. And the first-time hint, which never breaks the page.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { walletCard, CardView } from "../static/sim/wallet-card.mjs";
import { firstVisit, seenFirst } from "../static/sim/ui.mjs";

const S = JSON.parse(readFileSync(new URL("../i18n/es.json", import.meta.url), "utf8"));
const EVIL = "<img src=x onerror=alert(1)>";
const plain = (h) => h.replace(/<[^>]+>/g, "");

test("sending: what, the seconds left and the progress", () => {
  const h = walletCard("sending", { what: S["card.what_seed"], seconds: 7, progress: 0.4 }, S);
  assert.ok(plain(h).includes("la semilla"), h);
  assert.ok(plain(h).includes("7 s"), h);
  assert.match(h, /<progress max="100" value="40"><\/progress>/);
  assert.match(walletCard("sending", { what: "x", seconds: 1, progress: 3 }, S), /value="100"/, "clamped");
});
test("seed: the PIN to type and the fingerprint the signer must show", () => {
  const h = walletCard("seed", { pin: "1234", fingerprint: "73c5da0a" }, S);
  assert.match(h, /<code>1234<\/code>/);
  assert.match(h, /<code>73c5da0a<\/code>/);
  assert.ok(plain(h).includes("PIN"));
});
test("psbt: what you send, to whom, the change and the fee", () => {
  const outputs = [{ amount: 60000, address: "bc1qkd33yck74lg0kaq4tdcmu3hk4yruhjayxpe9ug", kind: "external" },
    { amount: 39000, address: "bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el", kind: "change" }];
  const h = walletCard("psbt", { outputs, fee: 1000 }, S);
  // As the signer's review shows them: BTC with eight decimals, addresses in fours.
  for (const t of ["0.00060000 BTC", "0.00039000 BTC", "0.00001000 BTC", "bc1q kd33 yck7 4lg0 kaq4 tdcm u3hk 4yru hjay xpe9 ug",
    "bc1q 8c6f shw2 dlwu n7ek n9qw f37c u2rn 755u pcp6 el", "Cambio", "Comisión"])
    assert.ok(plain(h).includes(t), t);
  assert.match(plain(h), /Total, comisión incluida0\.00061000 BTC/, "the total out, as the signer's last page");
});
test("scan: the signed PSBT's checks, its size, and that it is not broadcast", () => {
  const h = walletCard("scan", { ok: true, bytes: 372, lines: ["✅ Firma esperada (0330d54f…af3c)"] }, S);
  assert.ok(plain(h).includes("372 bytes"));
  assert.ok(plain(h).includes("Firma esperada"));
  assert.match(plain(h), /No se emite/);
  assert.match(h, /class="card ok"/);
  assert.match(walletCard("scan", { ok: false, bytes: 10, lines: [] }, S), /class="card bad"/);
  const qr = walletCard("scan", { lines: ["👛 Cuenta 73c5da0a · 84'/0'/0'"] }, S);
  assert.ok(plain(qr).includes("73c5da0a"));
  assert.doesNotMatch(qr, /No se emite/, "an account QR is not a payment");
});
test("no QR, and listening", () => {
  assert.ok(plain(walletCard("noqr", {}, S)).includes("No veo ningún QR"));
  assert.ok(plain(walletCard("listening", {}, S)).length > 5);
});
test("every value is escaped", () => {
  const cards = [walletCard("sending", { what: EVIL, seconds: EVIL, progress: 0 }, S),
    walletCard("seed", { pin: EVIL, fingerprint: EVIL }, S),
    walletCard("psbt", { outputs: [{ amount: EVIL, address: EVIL, kind: "external" }], fee: EVIL }, S),
    walletCard("scan", { ok: true, bytes: EVIL, lines: [EVIL] }, S)];
  for (const h of cards) {
    assert.doesNotMatch(h, /<img/, h);
    assert.ok(h.includes("&lt;img"), h);
  }
});
test("an unknown card is a mistake", () => {
  assert.throws(() => walletCard("nope", {}, S));
});

test("first-time hint survives blocked storage", () => {
  const blocked = { getItem() { throw new Error("SecurityError"); }, setItem() { throw new Error("SecurityError"); } };
  assert.equal(firstVisit(blocked), true);
  assert.equal(firstVisit(undefined), true);
  assert.equal(firstVisit(null), true);
  assert.doesNotThrow(() => seenFirst(blocked));
  assert.doesNotThrow(() => seenFirst(undefined));
});
test("the hint opens on the first visit only", () => {
  const store = new Map();
  const storage = { getItem: (k) => store.get(k) ?? null, setItem: (k, v) => store.set(k, String(v)) };
  assert.equal(firstVisit(storage), true);
  seenFirst(storage);
  assert.equal(store.get("dtmf-first-seen"), "1");
  assert.equal(firstVisit(storage), false);
});

// The card area is a polite live region: a send must announce its details
// once, not each time the countdown ticks.
function fakeArea() {
  const secs = { textContent: "" }, bar = { value: 0 };
  let html = "", writes = 0;
  return {
    secs, bar, get writes() { return writes; },
    get innerHTML() { return html; },
    set innerHTML(v) { html = v; writes++; secs.textContent = /<span class="secs"[^>]*>([^<]*)</.exec(v)?.[1] ?? ""; bar.value = Number(/value="(\d+)"/.exec(v)?.[1] ?? 0); },
    querySelector: (sel) => (sel === ".secs" ? secs : sel === "progress" ? bar : null),
  };
}
test("while sending, the card is written once; the seconds and the progress change in place", () => {
  const area = fakeArea(), view = new CardView(area, S);
  const seed = { kind: "seed", info: { pin: "1234", fingerprint: "73c5da0a" } };
  for (let left = 8; left >= 1; left--)
    for (const step of [0, 0.5]) view.show({ kind: "sending", info: { what: "la semilla", seconds: left, progress: (8 - left + step) / 8 } }, seed);
  assert.equal(area.writes, 1, "the details once");
  assert.equal(area.secs.textContent, "1");
  assert.equal(area.bar.value, 94);
  assert.match(area.innerHTML, /<span class="secs" role="timer" aria-live="off">/);
  view.show(seed);
  assert.equal(area.writes, 2, "once more when it changes kind");
  view.show(seed);
  view.show(null);
  assert.equal(area.writes, 3);
  assert.equal(area.innerHTML, "");
});
