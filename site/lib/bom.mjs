// The parts list of a build, from its hardware/<build>/bom.csv (the one
// source; tests/test_bom.py checks it against the layout).
import { escapeHtml } from "./markdown.mjs";

// RFC 4180 enough for our files: commas, quotes, doubled quotes, newlines.
export function parseCsv(text) {
  const records = [];
  let field = "", record = [], quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"' && text[i + 1] === '"') { field += '"'; i++; }
      else if (c === '"') quoted = false;
      else field += c;
    } else if (c === '"') quoted = true;
    else if (c === ",") { record.push(field); field = ""; }
    else if (c === "\n" || c === "\r") {
      if (c === "\r" && text[i + 1] === "\n") i++;
      record.push(field); field = "";
      if (record.some((f) => f !== "")) records.push(record);
      record = [];
    } else field += c;
  }
  record.push(field);
  if (record.some((f) => f !== "")) records.push(record);
  const [header, ...rows] = records;
  return rows.map((r) => Object.fromEntries(header.map((h, i) => [h, r[i] ?? ""])));
}

const COLUMNS = [["part", "bom.part"], ["qty", "bom.qty"], ["value", "bom.value"],
  ["notes", "bom.notes"], ["price_usd", "bom.price"]];

const price = (p) => (p ? "~" + p.replace("-", "–") + " US$" : "");

export function bomTable(rows, label) {
  const head = COLUMNS.map(([, k]) => `<th>${escapeHtml(label(k))}</th>`).join("");
  const body = rows.map((r) => "<tr>" + COLUMNS.map(([c]) =>
    `<td>${escapeHtml(c === "price_usd" ? price(r[c]) : r[c] ?? "")}</td>`).join("") + "</tr>").join("\n");
  return `<table class="bom">\n<thead><tr>${head}</tr></thead>\n<tbody>\n${body}\n</tbody>\n</table>`;
}
