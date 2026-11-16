// Builds the website into build/site/:
//     node site/build.mjs
// Deterministic: no dates and nothing random, so the same commit gives the
// same bytes. The commit shown in the footer comes from SITE_COMMIT, or
// from git. Prints the SHA-256 of every file, then of that list.
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { basename, dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { loadSite } from "./lib/i18n.mjs";
import { escapeHtml, renderMarkdown } from "./lib/markdown.mjs";
import { layout, rootPage, translator } from "./lib/page.mjs";
import { bomTable, parseCsv } from "./lib/bom.mjs";
import { parseStatus, statusTable } from "./lib/status.mjs";
import { validateBench, checkBenchAgainstCatalog } from "./lib/bench.mjs";

const HERE = fileURLToPath(new URL(".", import.meta.url));

// The prose of a page in a language, or the English one (fallback: true).
function content(site, lang, name) {
  for (const code of [lang, "en"]) {
    const file = join(site.dir, "content", code, name + ".md");
    if (!existsSync(file)) continue;
    const html = renderMarkdown(readFileSync(file, "utf8"));
    // English shown on another language's page says so, for screen readers.
    return code === lang ? { html, fallback: false } : { html: `<div lang="en">\n${html}\n</div>`, fallback: true };
  }
  throw new Error(`no content/en/${name}.md`);
}

const blob = (site, path) => escapeHtml(`${site.config.repo}/blob/main/${path}`);
const ext = (href, label) => `<a href="${href}" rel="noopener noreferrer">${escapeHtml(label)}</a>`;
// A per-language path from config ({ en, es, … }), English when missing.
const perLang = (paths, lang) => paths[lang] ?? paths.en;

function homePage(site, lang, tr) {
  const { html, fallback } = content(site, lang, "home");
  const builds = site.config.profiles.map((p) =>
    `<li><a href="${escapeHtml(p.slug)}/">${escapeHtml(tr(`profile.${p.slug}.title`))}</a> <code>${escapeHtml(p.id)}</code></li>`);
  const list = builds.length
    ? `\n<h2 id="builds">${escapeHtml(tr("home.builds"))}</h2>\n<ul class="builds">\n${builds.join("\n")}\n</ul>` : "";
  return { title: "DTMF-Signer", body: html + list + "\n" + donateBlock(site, tr, "../"), fallback };
}

function profilePage(site, lang, tr, p, root) {
  const { html, fallback } = content(site, lang, p.slug);
  const sim = Boolean(p.simulator && site.wasm);
  const repoRoot = join(site.dir, "..");
  const rows = parseCsv(readFileSync(join(repoRoot, p.hardware, "bom.csv"), "utf8"));
  const status = parseStatus(readFileSync(join(repoRoot, p.hardware, "status.json"), "utf8"));
  const title = tr(`profile.${p.slug}.title`);
  const files = p.files.map((f) => {
    const name = basename(f);
    return `<li><a href="${root}files/${escapeHtml(p.slug)}/${escapeHtml(name)}">${escapeHtml(name)}</a></li>`;
  }).join("\n");
  const body = `<h1>${escapeHtml(title)}</h1>
<p class="profile-id"><code>${escapeHtml(p.id)}</code></p>
${html}
<h2 id="status">${escapeHtml(tr("profile.status"))}</h2>
${statusTable(status, tr)}
<h2 id="simulator">${escapeHtml(tr("profile.sim"))}</h2>
${sim ? simulatorBlock(site, tr, p, root) : `<div class="simulator">
<p>${escapeHtml(tr("sim.soon"))}</p>
<p>${escapeHtml(tr("sim.built", { commit: site.commit }))}</p>
</div>`}
<h2 id="parts">${escapeHtml(tr("profile.bom"))}</h2>
${bomTable(rows, tr)}
<h2 id="layout">${escapeHtml(tr("profile.plan"))}</h2>
<p><img src="${root}img/${escapeHtml(p.slug)}/${escapeHtml(basename(p.image))}" alt="${escapeHtml(tr("profile.plan"))}"></p>
<p>${escapeHtml(tr("profile.files"))}</p>
<ul>
${files}
</ul>
<h2 id="details">${escapeHtml(tr("profile.details"))}</h2>
<ul>
<li>${ext(blob(site, perLang(p.readme, lang)), tr("profile.readme"))}</li>
<li>${ext(blob(site, perLang(p.docs, lang)), tr("profile.catalog"))}</li>
</ul>`;
  return { title: `${title} · DTMF-Signer`, body, fallback, wasm: sim };
}

function donateBlock(site, tr, root) {
  if (!site.config.donate) return "";
  const { bitcoin, lightning } = site.config.donate;
  const fig = (img, name, addr) => `<figure><img src="${root}img/${img}" alt="${name}" width="180" height="180">
<figcaption><b>${name}</b><br><code>${escapeHtml(addr)}</code></figcaption></figure>`;
  return `<section class="donate">
<h2 id="donate">${escapeHtml(tr("donate.title"))}</h2>
<p>${escapeHtml(tr("donate.text"))}</p>
<div class="donate-grid">
${fig("donate-bitcoin.png", "Bitcoin", bitcoin)}
${fig("donate-lightning.png", "Lightning", lightning)}
</div>
</section>`;
}

function downloadsPage(site, lang, tr, root) {
  const { html, fallback } = content(site, lang, "downloads");
  // A pinned tag, not /releases/latest: that one skips pre-releases.
  const { tag, uf2, sums } = site.config.release;
  const latest = `${site.config.repo}/releases/download/${tag}/`;
  const layouts = site.config.profiles.map((p) =>
    `<li><a href="../${p.slug}/#layout">${escapeHtml(tr(`profile.${p.slug}.title`))}</a> <code>${p.id}</code></li>`).join("\n");
  const body = `<h1>${escapeHtml(tr("downloads.title"))}</h1>
<h2 id="firmware">${escapeHtml(tr("downloads.firmware", { tag }))}</h2>
<ul>
<li>${ext(latest + uf2, uf2)}</li>
<li>${ext(latest + sums, sums)}</li>
<li>${ext(`${site.config.repo}/releases`, tr("downloads.all"))}</li>
</ul>
${html}
<h2 id="layouts">${escapeHtml(tr("downloads.layouts"))}</h2>
<ul>
${layouts}
</ul>
${donateBlock(site, tr, root)}`;
  return { title: `${tr("downloads.title")} · DTMF-Signer`, body, fallback };
}

// The firmware as WebAssembly (tools/build-wasm.sh), when it has been built.
const WASM = join(HERE, "..", "build", "wasm", "dtmf-signer.mjs");
const UR_TONES_JS = join(HERE, "..", "third_party", "ur-tones", "web", "src");
// The texts ui.mjs needs ({seconds} is filled in by ui.mjs).
const SIM_KEYS = ["sim.unavailable", "wallet.idle", "wallet.sending", "wallet.no_qr", "wallet.waiting", "wallet.got",
  "wallet.qr_repo", "wallet.qr_account", "wallet.qr_xpub", "wallet.no_broadcast", "mic.listen", "mic.stop", "mic.denied", "mic.unsupported", "status.idle", "status.line_in", "status.listening",
  "status.qr", "status.camera", "status.mic", "wallet.check_seed", "wallet.out", "wallet.change", "wallet.fee",
  "wallet.sig_ok", "wallet.sig_bad", "wallet.bytes"];
const BUTTONS = [["left", "◀"], ["up", "▲"], ["ok", "●"], ["down", "▼"], ["right", "▶"]];
const LEGEND = [["power", "bench.legend.power"], ["audio", "bench.legend.audio"], ["light", "bench.legend.light"]];

// A tooltip that works with the mouse, the keyboard (focus) and a tap.
// focus: false for elements that take the focus already (buttons, labels).
const tip = (text, focus = true) => `data-tip="${escapeHtml(text)}"${focus ? ' tabindex="0"' : ""}`;

// The inside of each kind of block.
export function blockBody(kind, tr, sim = {}, module = {}) {
  const e = (k, params) => escapeHtml(tr(k, params));
  switch (kind) {
    case "charger":
      return `<p class="blk-icon" aria-hidden="true">🔌</p>`;
    case "signer": {
      const buttons = BUTTONS.map(([b, mark]) => `<button type="button" data-btn="${b}" aria-label="${e(`sim.btn.${b}`)}">${mark}</button>`).join("\n");
      return `<div class="sim-device" tabindex="0" aria-label="${e("sim.device")}" ${tip(tr("sim.keyboard"), false)}>
<canvas class="sim-oled" width="128" height="64" aria-hidden="true"></canvas>${module.led ? `
<span class="sim-led" ${tip(tr("sim.led", module.led))} aria-label="${e("sim.led", module.led)}"><span class="led-dot" aria-hidden="true"></span>LED</span>` : ""}
<div class="sim-buttons">
${buttons}
</div>
</div>`;
    }
    case "sim-wallet":
      return `<div class="wd-screen">
<p class="wd-title">${e("wallet.wallet")}</p>
<p class="wd-row"><label ${tip(tr("wallet.network_tip"), false)}><span aria-hidden="true">🌐</span><span class="visually-hidden">${e("wallet.network")}</span>
<select class="wd-net" aria-label="${e("wallet.network")}">
<option value="mainnet">mainnet</option>
<option value="testnet">testnet</option>
</select></label>
<button type="button" class="wd-stop" aria-label="${e("wallet.stop")}" ${tip(tr("wallet.stop"), false)}><span aria-hidden="true">⏹</span></button></p>
<button type="button" class="wd-seed" ${tip(tr("wallet.seed_tip"), false)}><span aria-hidden="true">🌱</span> ${e("wallet.seed")}</button>
<p class="wd-row"><button type="button" class="wd-psbt" ${tip(tr("wallet.psbt_tip"), false)}><span aria-hidden="true">₿</span> ${e("wallet.psbt")}</button>
<span class="wd-warn" ${tip(tr("wallet.mismatch"))} aria-label="${e("wallet.mismatch")}" hidden>⚠</span></p>
<button type="button" class="wd-scan" ${tip(tr("wallet.scan_tip"), false)}><span aria-hidden="true">📷</span> ${e("wallet.scan")}</button>
<p class="wd-status" aria-live="polite">${e("wallet.idle")}</p>
<p class="wd-check" aria-live="polite"></p>
<textarea class="wd-out" readonly rows="3" hidden aria-label="${e("wallet.got")}"></textarea>
<p class="wd-opts"><label ${tip(tr("sim.sound"), false)}><input type="checkbox" class="wd-sound" checked> <span aria-hidden="true">🔊</span><span class="visually-hidden">${e("sim.sound")}</span></label>
<label ${tip(tr("sim.speed"), false)}><input type="checkbox" class="wd-fast"> <span aria-hidden="true">⏩</span><span class="visually-hidden">${e("sim.speed")}</span></label></p>
</div>`;
    case "your-wallet":
      return `<div class="wd-screen yp">
<p class="yw-cam"><span aria-hidden="true">📷</span> ${e("yw.camera")}</p>
<p class="yw-mic"><span aria-hidden="true">🎙</span> ${e("yw.mic")}</p>
<button type="button" class="mic-toggle" aria-pressed="false">${e("mic.listen")}</button>
<meter class="mic-level" min="-80" max="0" value="-80" aria-label="${e("mic.level")}"></meter>
<p class="mic-note">${e("mic.note")}</p>
<p class="yw-tools"><a class="yw-urtones" href="${escapeHtml(sim.tools?.urtones ?? "")}" rel="noopener noreferrer" ${tip(tr("yw.tones_tip"), false)}><span aria-hidden="true">🎵</span> ${e("yw.tones")}</a>
<a class="yw-scan" href="${escapeHtml(sim.tools?.nunchuk ?? "")}" rel="noopener noreferrer" ${tip(tr("yw.scan_tip"), false)}><span aria-hidden="true">📷</span> ${e("yw.scan")}</a></p>
</div>`;
    default:
      throw new Error(`no block for kind ${kind}`);
  }
}

// One test pair per network for the simulated wallet: the seed of a keys
// vector and a PSBT vector made for it, with what the signer should show.
export function simNetworks(site, p) {
  const read = (f) => JSON.parse(readFileSync(join(site.dir, "..", "vectors", f), "utf8")).vectors;
  const psbts = read("psbt-v0.json"), keys = read("keys-v0.json");
  const out = {};
  for (const [net, pair] of Object.entries(p.simulator.networks)) {
    const v = psbts.find((x) => x.name === pair.psbt_vector);
    const k = keys.find((x) => x.name === pair.keys_vector);
    if (!v) throw new Error(`${p.slug}: no PSBT vector named ${pair.psbt_vector}`);
    if (!k) throw new Error(`${p.slug}: no keys vector named ${pair.keys_vector}`);
    if (v.entropy !== k.entropy || v.passphrase !== k.passphrase)
      throw new Error(`${p.slug}: ${net}: the PSBT vector is not for that seed`);
    const testnet = v.testnet === true || String(v.testnet) === "True";
    if (testnet !== (net === "testnet")) throw new Error(`${p.slug}: ${net}: the PSBT vector is for the other network`);
    out[net] = { seed: k.entropy, fingerprint: k.wallet_fingerprint, psbt: v.psbt, fee: Number(v.fee),
      outputs: v.outputs, signatures: v.signatures };
  }
  return out;
}

// The simulator: the bench of the build (blocks, ports, links drawn by
// ui.mjs), the mode switch, the legend, and its data as JSON (not run).
function simulatorBlock(site, tr, p, root) {
  const s = p.simulator;
  const bench = p.bench;
  const strings = Object.fromEntries(SIM_KEYS.map((k) => [k, tr(k)]));
  const links = Object.fromEntries(bench.modes.map((m) => [m.id, m.links]));
  const data = JSON.stringify({ pin: s.pin, networks: simNetworks(site, p), tools: s.tools, repo: site.config.repo, strings, links }).replace(/</g, "\\u003c");
  const e = (k, params) => escapeHtml(tr(k, params));
  const first = bench.modes[0];
  const label = (l) => (l.startsWith("bench.") ? tr(l) : l);
  const modesOf = (id) => bench.modes.filter((m) => m.modules.includes(id)).map((m) => m.id).join(" ");
  const block = (m) => {
    const ports = (m.ports ?? []).map((pt) => {
      const t = tr(`bench.tip.${m.id}.${pt.id}`);
      return `<span class="port edge-${pt.edge} type-${pt.type}" data-port="${m.id}.${pt.id}" data-type="${pt.type}" data-dir="${pt.dir}" ${tip(t)} aria-label="${escapeHtml(label(pt.label) + ": " + t)}">${escapeHtml(label(pt.label))}</span>`;
    }).join("\n");
    const hidden = first.modules.includes(m.id) ? "" : " hidden";
    return `<section class="blk blk-${m.kind}${m.external ? " external" : ""}" data-module="${m.id}" data-column="${m.column}" data-modes="${modesOf(m.id)}" aria-label="${e(m.title)}"${hidden}>
<h3 class="blk-title">${e(m.title)}</h3>
${ports}
${blockBody(m.kind, tr, s, m)}
</section>`;
  };
  const column = (c) => `<div class="col col-${c}">\n${bench.modules.filter((m) => m.column === c).map(block).join("\n")}\n</div>`;
  const modes = bench.modes.map((m, i) => `<button type="button" role="tab" data-mode="${m.id}" aria-selected="${i === 0}">${e(m.title)}</button>`).join("\n");
  const legend = LEGEND.map(([t, k]) => `<li class="lg-${t}"><span class="lg-line" aria-hidden="true"></span>${e(k)}</li>`).join("\n");
  const connections = bench.modes.map((m) => `<ul class="connections visually-hidden" data-modes="${m.id}"${m === first ? "" : " hidden"}>
${m.links.map((l) => `<li>${e(`bench.link.${m.id}.${l.channel}`)}</li>`).join("\n")}
</ul>`).join("\n");
  return `<div class="simulator" id="sim">
<p class="sim-mobile" hidden><span aria-hidden="true">📱</span> ${e("sim.mobile")} <button type="button" class="sim-mobile-close" aria-label="${e("sim.close")}">✕</button></p>
<p class="sim-warning" ${tip(tr("sim.warning"))}><span aria-hidden="true">⚠</span> ${e("sim.warning_short")}</p>
<div class="sim-modes" role="tablist" aria-label="${e("bench.modes")}">
${modes}
</div>
<div class="bench" data-mode="${first.id}" data-orient="h">
${column("power")}
${column("signer")}
${column("peer")}
<svg class="links" aria-hidden="true"></svg>
</div>
<p class="sim-status" role="status" aria-live="polite">${e("sim.loading")}</p>
<ul class="legend">
${legend}
</ul>
${connections}
<pre class="screen-text visually-hidden" aria-live="polite"></pre>
<details class="sim-echo"><summary><span aria-hidden="true">›_</span> ${e("sim.echo")}</summary><pre class="echo"></pre></details>
<noscript><p>${e("sim.unavailable")}</p></noscript>
<p class="sim-built">${e("sim.built_from", { commit: site.commit })}</p>
</div>
<script type="application/json" id="sim-data">${data}</script>
<script src="${root}sim/ur-tones-crypto.js"></script>
<script src="${root}sim/ur-tones.js"></script>
<script type="module" src="${root}sim/ui.mjs"></script>`;
}

// Every profile's bench must be sound and say what the catalog says.
export function checkBenches(profiles, catalogMd) {
  const errors = profiles.filter((p) => p.bench)
    .flatMap((p) => [...validateBench(p.bench), ...checkBenchAgainstCatalog(p.bench, catalogMd, p.id)]);
  if (errors.length) throw new Error("bench:\n" + errors.join("\n"));
}

export function build({ siteDir, outDir, commit }) {
  const site = loadSite(siteDir);
  site.commit = commit;
  site.wasm = existsSync(WASM);
  if (site.config.profiles.some((p) => p.bench))
    checkBenches(site.config.profiles, readFileSync(join(siteDir, "..", "docs", "catalog.md"), "utf8"));
  if (!site.wasm && site.config.profiles.some((p) => p.simulator))
    console.error("warning: no simulator (build/wasm/dtmf-signer.mjs missing: run tools/build-wasm.sh)");
  const files = new Map();
  // Downloads and donations are optional in config.json ("release", "donate").
  const nav = [{ key: "nav.home", href: "" }];
  if (site.config.release) nav.push({ key: "nav.downloads", href: "downloads/" });

  files.set("index.html", Buffer.from(rootPage(site)));
  // The shared files at the top of static/ (static/sim/ goes with a simulator).
  for (const entry of readdirSync(join(HERE, "static"), { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name)))
    if (entry.isFile()) files.set(entry.name, readFileSync(join(HERE, "static", entry.name)));

  const repoRoot = join(siteDir, "..");
  if (site.config.donate)
    for (const img of ["donate-bitcoin.png", "donate-lightning.png"])
      files.set(`img/${img}`, readFileSync(join(repoRoot, "docs", "images", img)));
  for (const p of site.config.profiles) {
    files.set(`img/${p.slug}/${basename(p.image)}`, readFileSync(join(repoRoot, p.image)));
    for (const f of p.files) files.set(`files/${p.slug}/${basename(f)}`, readFileSync(join(repoRoot, f)));
  }
  if (site.wasm && site.config.profiles.some((p) => p.simulator)) {
    // Every file of static/sim/, so a new module cannot be left behind.
    for (const f of readdirSync(join(HERE, "static", "sim"), { withFileTypes: true }).filter((e) => e.isFile()).map((e) => e.name).sort())
      files.set(`sim/${f}`, readFileSync(join(HERE, "static", "sim", f)));
    files.set("sim/dtmf-signer.mjs", readFileSync(WASM));
    files.set("sim/ur-tones-crypto.js", readFileSync(join(UR_TONES_JS, "crypto.js")));
    files.set("sim/ur-tones.js", readFileSync(join(UR_TONES_JS, "ur-tones.js")));
  }

  for (const { code: lang } of site.config.languages) {
    const page = (path, make) => {
      const tr = translator(site, lang);
      const root = "../".repeat(1 + path.split("/").filter(Boolean).length);
      const { title, body, fallback, wasm } = make(tr);
      files.set(`${lang}/${path}index.html`,
        Buffer.from(layout({ site, lang, tr, root, title, body, path, nav, fallback, wasm })));
    };
    page("", (tr) => homePage(site, lang, tr));
    if (site.config.release) page("downloads/", (tr) => downloadsPage(site, lang, tr, "../../"));
    for (const p of site.config.profiles)
      page(`${p.slug}/`, (tr) => profilePage(site, lang, tr, p, "../../"));
  }

  rmSync(outDir, { recursive: true, force: true });
  for (const [path, bytes] of files) {
    mkdirSync(dirname(join(outDir, path)), { recursive: true });
    writeFileSync(join(outDir, path), bytes);
  }
  return files;
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const commit = process.env.SITE_COMMIT ||
    execFileSync("git", ["rev-parse", "--short", "HEAD"], { cwd: HERE, encoding: "utf8" }).trim();
  const files = build({ siteDir: HERE, outDir: join(HERE, "..", "build", "site"), commit });
  const sha = (b) => createHash("sha256").update(b).digest("hex");
  const list = [...files.keys()].sort().map((p) => `${sha(files.get(p))}  ${p}`).join("\n") + "\n";
  process.stdout.write(list);
  console.log(`${sha(list)}  (the list above): build/site/`);
}
