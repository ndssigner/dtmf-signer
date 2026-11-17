// A build's bench: the blocks around the signer, their ports and the links
// between them, as site/config.json describes them for each mode. The site's
// build checks it, and checks it against the profile in docs/catalog.md, so
// the drawing can never show a port the specification does not have.

// far: what is held to a peer (your ear and the wallet at a telephone's handset).
const COLUMNS = new Set(["power", "signer", "peer", "far"]);
// What a link carries: power, audio by cable, sound through the air, light
// (a QR to a camera), a telephone line; and a serial line.
const TYPES = new Set(["power", "audio", "air", "light", "uart", "phone"]);
const IO = /^(IN|OUT|VERIFY)-/;

function ports(bench) {
  const out = new Map();
  for (const m of bench.modules) for (const p of m.ports ?? []) out.set(`${m.id}.${p.id}`, p);
  return out;
}

export function validateBench(bench) {
  const errors = [];
  const ids = new Set(bench.modules.map((m) => m.id));
  const all = ports(bench);
  for (const m of bench.modules) {
    if (!COLUMNS.has(m.column)) errors.push(`${m.id}: column ${m.column}`);
    if (m.led !== undefined && (typeof m.led?.pin !== "string" || typeof m.led?.board !== "string"))
      errors.push(`${m.id}: led needs a pin and a board`);
    for (const p of m.ports ?? []) {
      if (!TYPES.has(p.type)) errors.push(`${m.id}.${p.id}: type ${p.type}`);
      if (!["in", "out", "both"].includes(p.dir)) errors.push(`${m.id}.${p.id}: dir ${p.dir}`);
    }
  }
  if (bench.modules.filter((m) => m.column === "signer").length !== 1) errors.push("exactly one module in the signer column");
  for (const mode of bench.modes) {
    for (const id of mode.modules) if (!ids.has(id)) errors.push(`${mode.id}: no module ${id}`);
    for (const l of mode.links) {
      const what = `${mode.id}: ${l.from} → ${l.to}`;
      const a = all.get(l.from), b = all.get(l.to);
      if (!a) { errors.push(`${what}: no port ${l.from}`); continue; }
      if (!b) { errors.push(`${what}: no port ${l.to}`); continue; }
      if (a.type !== b.type) errors.push(`${what}: ${a.type} to ${b.type}`);
      if (a.dir === "in" || b.dir === "out") errors.push(`${what}: runs against its ports' directions`);
      for (const end of [l.from, l.to]) if (!mode.modules.includes(end.split(".")[0])) errors.push(`${what}: ${end} is not in the mode`);
    }
  }
  return errors;
}

// The signer's capabilities the bench shows: its ports' and its own.
export function benchCapabilities(bench) {
  const s = bench.modules.find((m) => m.column === "signer");
  return new Set([...(s.capabilities ?? []), ...(s.ports ?? []).map((p) => p.capability).filter(Boolean)]);
}

// "- **Capabilities:** `A`, `B`,\n  `C`." under "### `ID`" in the catalog.
export function profileCapabilities(catalogMd, id) {
  const at = catalogMd.indexOf("### `" + id + "`");
  if (at < 0) throw new Error(`no ${id} in the catalog`);
  const section = catalogMd.slice(at).split("\n### ")[0];
  const m = /- \*\*Capabilities:\*\*([\s\S]*?)\.\s*(\n|$)/.exec(section);
  if (!m) throw new Error(`${id}: no Capabilities line`);
  return new Set([...m[1].matchAll(/`([A-Z0-9-]+)`/g)].map((x) => x[1]));
}

export function checkBenchAgainstCatalog(bench, catalogMd, id) {
  const errors = [];
  const listed = profileCapabilities(catalogMd, id);
  const shown = benchCapabilities(bench);
  for (const c of shown) if (!listed.has(c)) errors.push(`${id}: the bench shows ${c}, the catalog does not list it`);
  for (const c of listed) if (IO.test(c) && !shown.has(c)) errors.push(`${id}: the catalog lists ${c}, the bench does not show it`);
  return errors;
}
