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
// The texts ui.mjs needs (line_busy keeps its {seconds} for ui.mjs to fill).
const SIM_KEYS = ["unavailable", "line_idle", "line_busy"];
const BUTTONS = [["left", "◀"], ["up", "▲"], ["ok", "●"], ["down", "▼"], ["right", "▶"]];

// The simulator: the device (screen and buttons), the test wallet, the echo,
// and its data for ui.mjs as JSON (not run: CSP allows only our files).
function simulatorBlock(site, tr, p, root) {
  const s = p.simulator;
  const vectors = JSON.parse(readFileSync(join(site.dir, "..", "vectors", "psbt-v0.json"), "utf8")).vectors;
  const vector = vectors.find((v) => v.name === s.psbt_vector);
  if (!vector) throw new Error(`${p.slug}: no PSBT vector named ${s.psbt_vector}`);
  const strings = Object.fromEntries(SIM_KEYS.map((k) => [k, tr(`sim.${k}`)]));
  const data = JSON.stringify({ seed: s.seed, pin: s.pin, psbt: vector.psbt, strings }).replace(/</g, "\\u003c");
  const e = (k, params) => escapeHtml(tr(`sim.${k}`, params));
  const buttons = BUTTONS.map(([b, mark]) => `<button type="button" data-btn="${b}" aria-label="${e(`btn.${b}`)}">${mark}</button>`).join("\n");
  const steps = [1, 2, 3, 4, 5, 6].map((n) => `<li>${e(`step.${n}`, { pin: s.pin })}</li>`).join("\n");
  return `<div class="simulator" id="sim">
<p class="sim-warning">${e("warning")}</p>
<p class="sim-status" role="status">${e("loading")}</p>
<div class="sim-device" tabindex="0" aria-label="${e("device")}">
<canvas class="sim-oled" width="128" height="64" aria-hidden="true"></canvas>
<div class="sim-buttons">
${buttons}
</div>
</div>
<p class="sim-hint">${e("keyboard")}</p>
<details class="sim-text"><summary>${e("screen_text")}</summary><pre class="screen-text" aria-live="polite"></pre></details>
<div class="sim-wallet">
<h3>${e("wallet")}</h3>
<ol>
${steps}
</ol>
<p class="sim-actions"><button type="button" class="sim-send-seed">${e("send_seed")}</button>
<button type="button" class="sim-send-psbt">${e("send_psbt")}</button></p>
<p class="sim-options"><label><input type="checkbox" class="sim-sound" checked> ${e("sound")}</label>
<label><input type="checkbox" class="sim-fast"> ${e("speed")}</label></p>
<p class="sim-line">${e("line_idle")}</p>
<div class="sim-result" hidden><p>${e("signed")}</p><textarea class="sim-signed" readonly rows="5"></textarea></div>
</div>
<details class="sim-echo"><summary>${e("echo")}</summary><pre class="echo"></pre></details>
<noscript><p>${e("unavailable")}</p></noscript>
<p class="sim-built">${escapeHtml(tr("sim.built_from", { commit: site.commit }))}</p>
</div>
<script type="application/json" id="sim-data">${data}</script>
<script src="${root}sim/ur-tones-crypto.js"></script>
<script src="${root}sim/ur-tones.js"></script>
<script type="module" src="${root}sim/ui.mjs"></script>`;
}

export function build({ siteDir, outDir, commit }) {
  const site = loadSite(siteDir);
  site.commit = commit;
  site.wasm = existsSync(WASM);
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
    for (const f of ["core.mjs", "wallet.mjs", "ui.mjs"]) files.set(`sim/${f}`, readFileSync(join(HERE, "static", "sim", f)));
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
