// The wallet's screen after each action: a card with what you need for what
// you just did (the PIN and fingerprint after the seed, what you pay after
// the PSBT, what came back after scanning), as a normal wallet would show
// it. Built from data, every value escaped; ui.mjs only picks the card.
import { fill } from "./checks.mjs";

const ESC = { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" };
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ESC[c]);

// As the signer's review shows them, so they compare at a glance: amounts
// in BTC with eight decimals, addresses in groups of four.
export const btc = (n) => (Number.isSafeInteger(n) && n >= 0
  ? `${Math.floor(n / 1e8)}.${String(n % 1e8).padStart(8, "0")} BTC` : String(n));
export const grouped = (a) => String(a).replace(/(\S{4})(?=\S)/g, "$1 ");
const row = (label, value) => `<div class="row"><span>${esc(label)}</span><code>${esc(value)}</code></div>`;
const card = (cls, title, body = "", note = "") =>
  `<div class="card${cls ? " " + cls : ""}"><h4>${esc(title)}</h4>${body}${note ? `<p class="note">${esc(note)}</p>` : ""}</div>`;

export function walletCard(kind, info = {}, S) {
  switch (kind) {
    case "sending": {
      const pct = Math.round(100 * Math.min(1, Math.max(0, Number(info.progress) || 0)));
      // The seconds tick in place (CardView), in a timer that is not live:
      // the card's details are announced once.
      const [before, after = ""] = fill(S["card.sending"], { what: info.what }).split("{seconds}");
      const title = `${esc(before)}<span class="secs" role="timer" aria-live="off">${esc(info.seconds)}</span>${esc(after)}`;
      return `<div class="card"><h4>${title}</h4><progress max="100" value="${pct}"></progress></div>`;
    }
    case "seed":
      return card("", S["card.seed_title"], row(S["card.pin"], info.pin) + row(S["card.fp"], info.fingerprint), S["card.seed_note"]);
    case "psbt": {
      const outputs = info.outputs ?? [];
      const isChange = (o) => o.kind === "change" || o.change;
      const rows = outputs.map((o) => {
        const to = o.address ? grouped(o.address) : `script ${o.script ?? ""}`;
        return row(S[isChange(o) ? "card.change" : "card.send"], btc(o.amount)) + `<div class="row addr"><span>${esc(S["card.to"])}</span><code>${esc(to)}</code></div>`;
      }).join("");
      const hasFee = info.fee !== null && info.fee !== undefined;
      const fee = hasFee ? row(S["card.fee"], btc(info.fee)) : "";
      // What leaves the wallet, as the signer's last page says it.
      const out = outputs.filter((o) => !isChange(o)).reduce((a, o) => a + o.amount, hasFee ? info.fee : 0);
      const total = Number.isSafeInteger(out) ? row(S["card.total"], btc(out)) : "";
      return card("", S["card.psbt_title"], rows + fee + total, S["card.psbt_note"]);
    }
    case "scan": {
      // A signed PSBT says whether it carries the expected signature; the
      // other QR codes (the account, the repository) only what they hold.
      const signed = info.ok === true || info.ok === false;
      const title = signed ? S[info.ok ? "card.scan_ok" : "card.scan_bad"] : S["card.scan_qr"];
      const lines = (info.lines ?? []).map((l) => `<p class="line">${esc(l)}</p>`).join("");
      const size = info.bytes === undefined ? "" : row(S["card.size"], fill(S["wallet.bytes"], { bytes: info.bytes }));
      return card(signed ? (info.ok ? "ok" : "bad") : "", title, lines + size, signed ? S["wallet.no_broadcast"] : "");
    }
    case "reading":
      return card("", S["wallet.waiting"]);
    case "noqr":
      return card("", S["wallet.no_qr"], "", S["card.noqr_note"]);
    case "hearing": {
      // The simulated wallet at the earpiece: the frames heard so far.
      const parts = Number(info.parts) > 0 ? row(S["card.parts"], info.parts) : "";
      return card("", S["card.hearing"], parts);
    }
    case "listening":
      return card("", S["card.listening"], "", S["card.listening_note"]);
    default:
      throw new Error(`no wallet card ${kind}`);
  }
}

// The card area (a polite live region): written only when what it shows
// changes; a send's countdown and progress move in place.
export class CardView {
  constructor(el, S) { this.el = el; this.S = S; this.key = null; }
  show(...cards) {
    cards = cards.filter(Boolean);
    const key = JSON.stringify(cards.map((c) => [c.kind, c.kind === "sending" ? { what: c.info.what } : c.info ?? null]));
    if (key !== this.key) {
      this.key = key;
      this.el.innerHTML = cards.map((c) => walletCard(c.kind, c.info, this.S)).join("");
      return;
    }
    const s = cards.find((c) => c.kind === "sending");
    if (!s) return;
    const secs = this.el.querySelector(".secs"), bar = this.el.querySelector("progress");
    const text = String(s.info.seconds), pct = Math.round(100 * Math.min(1, Math.max(0, Number(s.info.progress) || 0)));
    if (secs && secs.textContent !== text) secs.textContent = text;
    if (bar && bar.value !== pct) bar.value = pct;
  }
}
