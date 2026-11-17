// The stylesheet's rules about colour: every token has a dark value, the
// cable colours belong to cables and connectors, teal to what can be pressed.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { SITE } from "./helpers.mjs";

const strip = (css) => css.replace(/\/\*[\s\S]*?\*\//g, "");
const CABLE = ["--power", "--audio", "--light", "--line-c"];
const TEAL = ["--press", "--press-fg", "--press-soft", "--press-ink", "--accent"];

// [selector, body] of every innermost rule (media wrappers are dropped).
function rules(css) {
  const out = [];
  for (const m of strip(css).matchAll(/([^{}]+)\{([^{}]*)\}/g)) out.push([m[1].trim(), m[2]]);
  return out;
}
const decls = (body) => Object.fromEntries([...body.matchAll(/(--[\w-]+)\s*:\s*([^;]+);?/g)].map((m) => [m[1], m[2].trim()]));
const isHex = (v) => /^#[0-9a-f]{3,8}$/i.test(v);
const parts = (sel) => sel.split(",").map((p) => p.trim()).filter(Boolean);

// The :root blocks: the first is light, the one inside the dark media query dark.
function themes(css) {
  const c = strip(css);
  const dark = c.match(/@media \(prefers-color-scheme: dark\) \{\s*:root \{([^{}]*)\}/)?.[1] ?? "";
  const light = c.replace(/@media \(prefers-color-scheme: dark\) \{\s*:root \{[^{}]*\}/, "").match(/:root \{([^{}]*)\}/)?.[1] ?? "";
  return { light: decls(light), dark: decls(dark) };
}

// Every spelling of a hex colour: the hex (full and short) and rgb()/rgba().
function spellings(hex) {
  const h = hex.replace("#", "").toLowerCase();
  const full = h.length === 3 ? [...h].map((x) => x + x).join("") : h;
  const [r, g, b] = [0, 2, 4].map((i) => parseInt(full.slice(i, i + 2), 16));
  const out = [new RegExp("#" + full + "(?![0-9a-f])", "i"), new RegExp(`rgba?\\(\\s*${r}[\\s,]+${g}[\\s,]+${b}\\b`, "i")];
  if (full[0] === full[1] && full[2] === full[3] && full[4] === full[5])
    out.push(new RegExp("#" + full[0] + full[2] + full[4] + "(?![0-9a-f])", "i"));
  return out;
}
const usesVar = (body, names) => names.some((n) => new RegExp(`var\\(\\s*${n}\\s*[,)]`).test(body));

const CABLE_PART = /(^|[\s>+~])\.(c-[\w-]+|p-[\w-]+|port(?![\w])|port-[\w-]*|legend(?![\w-])|l-[\w-]+)/;
const PRESS_PART = new RegExp("(^|[\\s>+~])(button|a|select|input|textarea|summary)(?![\\w-])|\\[role=\"?tab\"?\\]|(^|[\\s>+~])\\.(seg|cta)(?![\\w-])|^:focus-visible$");

// Rules that use a colour where it does not belong, as messages.
export function colourViolations(css) {
  const bad = [];
  const { light, dark } = themes(css);
  const cableLiterals = CABLE.flatMap((n) => [light[n], dark[n]]).filter(Boolean).flatMap(spellings);
  const usesCable = (body) => usesVar(body, CABLE) || cableLiterals.some((re) => re.test(body));
  const usesTeal = (body) => usesVar(body, TEAL);
  for (const [sel, body] of rules(css)) {
    // Every colour token lives in :root, where the guard sees both themes;
    // none may be named after a cable but the cable tokens themselves.
    for (const name of Object.keys(decls(body))) {
      if (/^--(c|lg)-|power|audio|light|phone|air/.test(name) && !CABLE.includes(name)) bad.push(`${sel}: ${name} is a second cable token`);
      if (sel !== ":root" && /#[0-9a-f]{3,8}\b|rgba?\(|hsla?\(|color-mix\(/i.test(decls(body)[name]))
        bad.push(`${sel}: ${name} is a colour token outside :root (cable colour hidden from the guard)`);
    }
    if (sel === ":root") {
      // Token definitions: a cable or teal token may be defined, not reused by another token.
      for (const [name, value] of Object.entries(decls(body))) {
        if (!CABLE.includes(name) && usesCable(value)) bad.push(`${sel}: ${name} reuses a cable colour`);
        if (!TEAL.includes(name) && usesTeal(value)) bad.push(`${sel}: ${name} reuses a teal token`);
      }
      continue;
    }
    for (const p of parts(sel)) {
      if (usesCable(body) && !CABLE_PART.test(p)) bad.push(`${p}: cable colour outside cables and connectors`);
      // Cables, connectors and the legend take their colour from the cable tokens only.
      if (CABLE_PART.test(p) && /#[0-9a-f]{3,8}\b|rgba?\(/i.test(body)) bad.push(`${p}: cable colour literal, not a cable token`);
      if (usesTeal(body) && !PRESS_PART.test(p)) bad.push(`${p}: teal on something not pressable`);
    }
  }
  return bad;
}

const css = readFileSync(SITE + "static/style.css", "utf8");

test("every colour token has a dark value", () => {
  const { light: l, dark: d } = themes(css);
  const names = Object.keys(l).filter((k) => isHex(l[k]));
  for (const n of ["--bg", "--surface", "--sunk", "--line", "--module", "--fg", "--muted", "--press", "--press-fg",
    "--power", "--audio", "--light", "--line-c", "--ok", "--bad"]) assert.ok(names.includes(n), `${n} defined`);
  for (const n of names) assert.ok(isHex(d[n] ?? ""), `${n} has a dark value`);
  for (const n of Object.keys(d)) assert.ok(n in l, `${n} dark-only`);
});

test("cable colours are used only by cable/connector rules", () => {
  assert.deepEqual(colourViolations(css).filter((m) => /cable/.test(m)), []);
});

test("teal only on pressables", () => {
  assert.deepEqual(colourViolations(css).filter((m) => /teal/.test(m)), []);
});

const ROOT = ":root { --audio: #2f6fb0; --press: #0b7a70; --press-ink: #076158; --accent: var(--press-ink); }\n";
const bites = (rest, re) => assert.ok(colourViolations(ROOT + rest).some((m) => re.test(m)), rest);
const quiet = (rest) => assert.deepEqual(colourViolations(ROOT + rest), [], rest);

test("the cable guard bites on every part, token reuse and other spellings", () => {
  quiet(".c-audio { stroke: var(--audio); } .bench .port.p-audio, .legend .l-audio { border-color: var(--audio); }");
  bites(".module, .c-x { border-color: var(--audio); }", /^\.module: cable/);
  bites(".portrait { color: var(--audio); }", /portrait/);
  bites(".module { color: rgb(47, 111, 176); }", /cable/);
  bites(".module { color: #2F6FB0; }", /cable/);
  bites(".module { background: color-mix(in srgb, var(--audio) 20%, var(--bg)); }", /cable/);
  bites(":root { --accent: var(--audio); }", /reuses a cable/);
  bites(":root { --x: #2f6fb0; }", /reuses a cable/);
  assert.ok(spellings("#fa0").some((re) => re.test("#ffaa00")) && spellings("#ffaa00").some((re) => re.test("#fa0")));
});

test("the teal guard bites on every part, element boundaries and the accent alias", () => {
  quiet("button, .cta a, [role=tab], select:focus, a:hover { color: var(--press); } :focus-visible { outline: 3px solid var(--press); }");
  quiet("nav.site a:hover { border-color: var(--press); } .seg.on { background: var(--press-soft); }");
  bites(".selected { color: var(--press); }", /\.selected/);
  bites(".input-note { color: var(--press); }", /input-note/);
  bites(".segment { background: var(--press); }", /segment/);
  bites("button, .card { color: var(--press); }", /^\.card: teal/);
  bites(".card { color: var(--press-ink); }", /teal/);
  bites(".card { color: var(--accent); }", /teal/);
  bites(":root { --x: var(--press); }", /reuses a teal/);
});

test("the cable guard sees every cable colour: no second token, none hidden outside :root, no literals on cables", () => {
  bites(".bench { --c-power: #f0883e; } .blk-charger { border-color: var(--c-power); }", /second cable token/);
  bites(".bench { --glow: #f0883e; }", /outside :root/);
  bites(":root { --phone-ink: #2aa198; }", /second cable token/);
  bites(".legend .l-power { border-color: #f0883e; }", /literal/);
  quiet(".legend .l-power { border-color: var(--power); } .c-power { stroke: var(--power); }");
});

const CONFIG = JSON.parse(readFileSync(SITE + "config.json", "utf8"));
const TYPES = ["power", "audio", "air", "light", "phone"];
const TOKEN = { power: "--power", audio: "--audio", air: "--audio", light: "--light", phone: "--line-c" };

test("every cable type has its cable, connector and legend rule, from its token", () => {
  const all = rules(css);
  for (const t of TYPES) for (const cls of [`c-${t}`, `p-${t}`, `l-${t}`]) {
    const r = all.filter(([sel]) => parts(sel).some((p) => new RegExp(`\\.${cls}(?![\\w-])`).test(p)));
    assert.ok(r.some(([, body]) => usesVar(body, [TOKEN[t]])), `${cls} uses ${TOKEN[t]}`);
  }
  // air is the audio colour, dashed, on the cable and in the legend
  assert.ok(all.some(([sel, body]) => /\.c-air/.test(sel) && /stroke-dasharray/.test(body)));
  assert.ok(all.some(([sel, body]) => /\.l-air/.test(sel) && /dashed/.test(body)));
});

test("modules are neutral: one outline token, no cable colour anywhere on them", () => {
  const mod = rules(css).filter(([sel]) => parts(sel).some((p) => /(^|\s)\.mod(?![\w-])|\.mod-[\w-]+/.test(p) && !CABLE_PART.test(p)));
  assert.ok(mod.some(([, body]) => /border:[^;]*var\(--module\)/.test(body)), ".mod outlined with --module");
  for (const [sel, body] of mod) assert.ok(!usesVar(body, CABLE), sel);
  assert.doesNotMatch(strip(css), /\.blk|--c-(power|audio|light|phone|idle)|\.lg-|\.link\b|type-(power|audio|light|phone)/);
});

test("narrow layout: every port position class has a rule when the bench is stacked", () => {
  const edges = new Set(CONFIG.profiles.flatMap((p) => p.bench?.modules ?? []).flatMap((m) => m.ports ?? []).map((p) => p.edge));
  assert.ok(edges.size >= 5);
  const narrow = rules(css).filter(([sel]) => sel.includes('.bench[data-orient="v"]'));
  for (const e of edges) assert.ok(narrow.some(([sel]) => new RegExp(`\\.edge-${e}(?![\\w-])`).test(sel)), `edge-${e}`);
});

test("the home keypad's frequency labels do not use the bench's layout classes", () => {
  const src = readFileSync(SITE + "static/dtmf-keypad.mjs", "utf8");
  assert.match(src, /"hz hz-col"/);
  assert.match(src, /"hz hz-row"/);
  assert.match(src, /\.hz-row\[data-r=/);
  assert.doesNotMatch(src, /"hz (col|row)"|`\.(col|row)\[/);
  assert.match(css, /\.dtmf \.hz-row \{/);
  assert.doesNotMatch(css, /\.hz\.(row|col)/);
});
