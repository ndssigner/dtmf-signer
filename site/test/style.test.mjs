// The stylesheet's rules about colour: every token has a dark value, the
// cable colours belong to cables and connectors, teal to what can be pressed.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { SITE } from "./helpers.mjs";

const css = readFileSync(SITE + "static/style.css", "utf8").replace(/\/\*[\s\S]*?\*\//g, "");

// Top-level rules as [selector, body]; the dark media block is read apart.
function rules(text) {
  const out = [];
  const re = /([^{}]+)\{([^{}]*)\}/g;
  let m;
  while ((m = re.exec(text))) out.push([m[1].trim().replace(/^@media[^{]*\{/, "").trim(), m[2]]);
  return out;
}
const decls = (body) => Object.fromEntries([...body.matchAll(/(--[\w-]+)\s*:\s*([^;]+);?/g)].map((m) => [m[1], m[2].trim()]));
const dark = css.match(/@media \(prefers-color-scheme: dark\) \{\s*:root \{([^{}]*)\}/)[1];
const light = css.match(/:root \{([^{}]*)\}/)[1];
const isHex = (v) => /^#[0-9a-f]{3,8}$/i.test(v);

test("every colour token has a dark value", () => {
  const l = decls(light), d = decls(dark);
  const names = Object.keys(l).filter((k) => isHex(l[k]));
  for (const n of ["--bg", "--surface", "--sunk", "--line", "--module", "--fg", "--muted", "--press", "--press-fg",
    "--power", "--audio", "--light", "--line-c", "--ok", "--bad"]) assert.ok(names.includes(n), `${n} defined`);
  for (const n of names) assert.ok(isHex(d[n] ?? ""), `${n} has a dark value`);
  for (const n of Object.keys(d)) assert.ok(n in l, `${n} dark-only`);
});

test("cable colours are used only by cable/connector rules", () => {
  const l = decls(light), d = decls(dark);
  const cable = ["--power", "--audio", "--light", "--line-c"].flatMap((n) => [l[n], d[n]]);
  assert.equal(cable.length, 8);
  const all = rules(css.replace(/@media[^{]*\{\s*:root \{[^{}]*\}\s*\}/, ""));
  for (const [sel, body] of all) {
    if (sel === ":root") continue;
    const uses = cable.some((h) => body.toLowerCase().includes(h.toLowerCase())) ||
      /var\(--(power|audio|light|line-c)\)/.test(body);
    if (uses) assert.match(sel, /\.c-|\.p-|\.port|\.legend/, `${sel} uses a cable colour`);
  }
});

test("teal only on pressables", () => {
  const all = rules(css.replace(/@media[^{]*\{\s*:root \{[^{}]*\}\s*\}/, ""));
  for (const [sel, body] of all) {
    if (sel === ":root") continue;
    if (/var\(--press\)|var\(--press-fg\)|var\(--press-soft\)/.test(body))
      assert.match(sel, /button|(^|[\s,>])a\b|\[role=tab\]|\.seg|\.cta|select|input|:focus-visible/, `${sel} uses teal`);
  }
});
