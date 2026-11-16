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

const HERE = fileURLToPath(new URL(".", import.meta.url));

// The prose of a page in a language, or the English one (fallback: true).
function content(site, lang, name) {
  for (const code of [lang, "en"]) {
    const file = join(site.dir, "content", code, name + ".md");
    if (existsSync(file)) return { html: renderMarkdown(readFileSync(file, "utf8")), fallback: code !== lang };
  }
  throw new Error(`no content/en/${name}.md`);
}

const blob = (site, path) => `${site.config.repo}/blob/main/${path}`;
const ext = (href, label) => `<a href="${href}" rel="noopener noreferrer">${escapeHtml(label)}</a>`;
// A per-language path from config ({ en, es, … }), English when missing.
const perLang = (paths, lang) => paths[lang] ?? paths.en;

function homePage(site, lang, tr) {
  const { html, fallback } = content(site, lang, "home");
  const builds = site.config.profiles.map((p) =>
    `<li><a href="${p.slug}/">${escapeHtml(tr(`profile.${p.slug}.title`))}</a> <code>${p.id}</code></li>`);
  const list = builds.length ? `\n<ul class="builds">\n${builds.join("\n")}\n</ul>` : "";
  return { title: "DTMF-Signer", body: html + list + "\n" + donateBlock(site, tr, "../"), fallback };
}

function profilePage(site, lang, tr, p, root) {
  const { html, fallback } = content(site, lang, p.slug);
  const repoRoot = join(site.dir, "..");
  const rows = parseCsv(readFileSync(join(repoRoot, p.hardware, "bom.csv"), "utf8"));
  const title = tr(`profile.${p.slug}.title`);
  const files = p.files.map((f) => {
    const name = basename(f);
    return `<li><a href="${root}files/${name}">${escapeHtml(name)}</a></li>`;
  }).join("\n");
  const body = `<h1>${escapeHtml(title)}</h1>
<p class="profile-id"><code>${p.id}</code></p>
${html}
<h2 id="simulator">${escapeHtml(tr("profile.sim"))}</h2>
<div class="simulator">
<p>${escapeHtml(tr("sim.soon"))}</p>
<p>${escapeHtml(tr("sim.built", { commit: site.commit }))}</p>
</div>
<h2 id="parts">${escapeHtml(tr("profile.bom"))}</h2>
${bomTable(rows, tr)}
<h2 id="layout">${escapeHtml(tr("profile.plan"))}</h2>
<p><img src="${root}img/${basename(p.image)}" alt="${escapeHtml(tr("profile.plan"))}"></p>
<p>${escapeHtml(tr("profile.files"))}</p>
<ul>
${files}
</ul>
<h2 id="details">${escapeHtml(tr("profile.details"))}</h2>
<ul>
<li>${ext(blob(site, perLang(p.readme, lang)), tr("profile.readme"))}</li>
<li>${ext(blob(site, perLang(p.docs, lang)), tr("profile.catalog"))}</li>
</ul>`;
  return { title: `${title} · DTMF-Signer`, body, fallback };
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
  const { uf2, sums } = site.config.release;
  const latest = `${site.config.repo}/releases/latest/download/`;
  const layouts = site.config.profiles.map((p) =>
    `<li><a href="../${p.slug}/#layout">${escapeHtml(tr(`profile.${p.slug}.title`))}</a> <code>${p.id}</code></li>`).join("\n");
  const body = `<h1>${escapeHtml(tr("downloads.title"))}</h1>
<h2 id="firmware">${escapeHtml(tr("downloads.firmware"))}</h2>
<ul>
<li>${ext(latest + uf2, uf2)}</li>
<li>${ext(latest + sums, sums)}</li>
</ul>
${html}
<h2 id="layouts">${escapeHtml(tr("downloads.layouts"))}</h2>
<ul>
${layouts}
</ul>
${donateBlock(site, tr, root)}`;
  return { title: `${tr("downloads.title")} · DTMF-Signer`, body, fallback };
}

export function build({ siteDir, outDir, commit }) {
  const site = loadSite(siteDir);
  site.commit = commit;
  const files = new Map();
  // Downloads and donations are optional in config.json ("release", "donate").
  const nav = [{ key: "nav.home", href: "" }];
  if (site.config.release) nav.push({ key: "nav.downloads", href: "downloads/" });

  files.set("index.html", Buffer.from(rootPage(site)));
  for (const name of readdirSync(join(HERE, "static")).sort())
    files.set(name, readFileSync(join(HERE, "static", name)));

  const repoRoot = join(siteDir, "..");
  if (site.config.donate)
    for (const img of ["donate-bitcoin.png", "donate-lightning.png"])
      files.set(`img/${img}`, readFileSync(join(repoRoot, "docs", "images", img)));
  for (const p of site.config.profiles) {
    files.set(`img/${basename(p.image)}`, readFileSync(join(repoRoot, p.image)));
    for (const f of p.files) files.set(`files/${basename(f)}`, readFileSync(join(repoRoot, f)));
  }

  for (const { code: lang } of site.config.languages) {
    const page = (path, make) => {
      const tr = translator(site, lang);
      const root = "../".repeat(1 + path.split("/").filter(Boolean).length);
      const { title, body, fallback } = make(tr);
      files.set(`${lang}/${path}index.html`,
        Buffer.from(layout({ site, lang, tr, root, title, body, path, nav, fallback })));
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
