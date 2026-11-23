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
import { loadSite, t } from "./lib/i18n.mjs";
import { escapeHtml, renderMarkdown } from "./lib/markdown.mjs";
import { CSP, layout, rootPage, specHref, translator } from "./lib/page.mjs";
import { bomTable, parseCsv } from "./lib/bom.mjs";
import { parseStatus, renderStageDots, renderStatusSequence } from "./lib/status.mjs";
import { galleryList, photoMetadata, photoHref } from "./lib/gallery.mjs";
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

// The home page: the headline and a keypad to play, the builds at a glance
// (each a row with its two actions), then why, the origin and the donate block.
function homePage(site, lang, tr) {
  const { html, fallback } = content(site, lang, "home");
  const e = (k, params) => escapeHtml(tr(k, params));
  const spec = specHref(site, lang);
  const ur = site.config.urtones;
  const rows = site.config.profiles.map((p) => {
    const status = parseStatus(readFileSync(join(site.dir, "..", p.hardware, "status.json"), "utf8"));
    const slug = escapeHtml(p.slug);
    const chips = (p.talks ?? []).map((k) => `<span class="chip">${e(`talk.${k}`)}</span>`).join("\n");
    return `<article class="build">
<img src="../img/builds/${slug}.svg" alt="">
<div class="what">
<h3>${e(`profile.${p.slug}.title`)}</h3>
<p>${e(`profile.${p.slug}.lede`)}</p>
<p class="talk">
${chips}
</p>
${renderStageDots(status, "hardware", tr)}
</div>
<div class="acts">
<a class="main" href="${slug}/"><span aria-hidden="true">▶</span> ${e("tab.try")}</a>
<a class="alt" href="${slug}/parts/">${e("home.build_it")}</a>
</div>
</article>`;
  });
  const builds = rows.length
    ? `<h2 id="builds">${e("home.builds")}</h2>\n<div class="builds">\n${rows.join("\n")}\n</div>\n` : "";
  const specCta = spec ? `<a class="alt" href="${escapeHtml(spec)}" rel="noopener noreferrer">${e("home.cta_spec")}</a>` : "";
  const urLine = ur ? `<p class="urtones-line">${e("home.urtones")} <a href="${escapeHtml(ur.spec)}" rel="noopener noreferrer">${e("footer.urtones_spec")}</a> · <a href="${escapeHtml(ur.web)}" rel="noopener noreferrer">${e("footer.urtones_web")}</a></p>` : "";
  const hero = `<section class="hero home">
<div>
<h1>${e("home.title")}</h1>
<p class="lede">${e("home.lede")}</p>
<div class="cta">
<a class="main" href="#builds"><span aria-hidden="true">▶</span> ${e("home.cta_try")}</a>
${specCta}
</div>
${urLine}
</div>
<div class="dtmf" data-keypad data-hint="${e("home.keypad_hint")}" data-caption="${e("home.keypad_caption")}" role="group" aria-label="${e("home.keypad")}"></div>
</section>
<script type="module" src="../dtmf-keypad.mjs"></script>
`;
  // How it started (the last section), with the sister projects beside it.
  const cut = html.lastIndexOf("<h2");
  const sib = site.config.siblings ?? [];
  const aside = sib.length ? `<aside class="siblings" aria-labelledby="siblings">
<h2 id="siblings">${e("home.siblings")}</h2>
<ul>
${sib.map((x) => `<li><a href="${escapeHtml(x.href)}" rel="noopener noreferrer">${escapeHtml(x.name)}</a><br><span>${e(`sibling.${x.id}`)}</span></li>`).join("\n")}
</ul>
</aside>` : "";
  const prose = cut > 0 && aside
    ? `<div class="prose">\n${html.slice(0, cut)}\n</div>\n<div class="origin-row">\n<div class="prose">\n${html.slice(cut)}\n</div>\n${aside}\n</div>`
    : `<div class="prose">\n${html}\n</div>`;
  return { title: "DTMF-Signer", body: `${hero}${builds}${prose}\n${donateBlock(site, tr, "../")}`, fallback };
}

// A page left at a build's old address: it sends the visitor on to the
// new one (from <lang>/<from> to <lang>/<to>, both ending in /), and asks
// search engines not to index it.
export function movedPage(from, to) {
  const href = escapeHtml("../".repeat(from.split("/").filter(Boolean).length) + to);
  return `<!doctype html>
<html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="${CSP}">
<meta name="robots" content="noindex">
<meta http-equiv="refresh" content="0; url=${href}">
<title>DTMF-Signer</title></head>
<body><p><a href="${href}">${href}</a></p></body></html>
`;
}

// The tabs of a build, each its own page under <lang>/<slug>/: the
// simulator at the build's own address, the rest in language-neutral folders.
export const TABS = [["", "tab.try"], ["what/", "tab.what"], ["parts/", "tab.parts"], ["build/", "tab.build"],
  ["gallery/", "tab.gallery"], ["status/", "tab.status"], ["downloads/", "tab.downloads"]];

// The SHA-256 of a file, as hex: the one hash the site shows for downloads.
export const packHash = (bytes) => createHash("sha256").update(bytes).digest("hex");

// Every build's language packs, read once: where each is published, its
// size and its hash (the build tabs and the downloads page show them).
export function packList(site, repoRoot) {
  return site.config.profiles.flatMap((p) => Object.entries(p.simulator?.packs ?? {}).sort().map(([lang, path]) => {
    const bytes = readFileSync(join(repoRoot, path));
    return { slug: p.slug, lang, name: `${lang}.bin`, href: `sim/packs/${lang}.bin`, bytes, size: bytes.length, sha256: packHash(bytes) };
  }));
}

const kib = (n) => `${Math.round(n / 1024)} KiB`;
// A language's own name: the site's, or, for a pack in a language the site
// is not translated into, the one the platform knows ("português").
const langName = (site, code) => {
  if (site.strings[code]?.["lang.name"]) return site.strings[code]["lang.name"];
  const own = new Intl.DisplayNames([code], { type: "language" }).of(code);
  return own && own !== code ? own.charAt(0).toLocaleUpperCase(code) + own.slice(1) : code;
};
// Whether a build has a published firmware: its status says so.
const released = (status) => status.firmware.find((s) => s.stage === "release")?.state === "done";

// The pages of one build: the same header and tab bar, one tab each.
// page(path, make) is build()'s, which wraps a body in the layout.
function buildPages(site, lang, p, page) {
  const repoRoot = join(site.dir, "..");
  const sim = Boolean(p.simulator && site.modules.has(simModule(p)));
  // A build not yet on a breadboard has no parts list, layout or files yet.
  const bom = join(repoRoot, p.hardware, "bom.csv");
  const rows = existsSync(bom) ? parseCsv(readFileSync(bom, "utf8")) : null;
  const status = parseStatus(readFileSync(join(repoRoot, p.hardware, "status.json"), "utf8"));
  const slug = escapeHtml(p.slug);
  const files = (root) => buildFiles(p, root);

  for (const [tab, key] of TABS) {
    page(`${p.slug}/${tab}`, (tr, root) => {
      const e = (k, params) => escapeHtml(tr(k, params));
      const title = tr(`profile.${p.slug}.title`);
      const here = `${root}${lang}/${slug}/`;
      const notYet = `<p class="not-yet">${e("build.not_yet")} <a href="${here}status/">${e("tab.status")}</a></p>`;
      const tabs = TABS.map(([t, k]) => {
        const label = k === "tab.try" ? `<span aria-hidden="true">▶</span> ${e(k)}` : e(k);
        return `<a href="${here}${t}"${t === tab ? ' aria-current="page"' : ""}>${label}</a>`;
      }).join("\n");
      let body, fallback = false;
      if (tab === "") {
        body = sim ? simulatorBlock(site, tr, p, root, lang) : `<div class="simulator">
<p>${e("sim.soon")}</p>
<p>${e("sim.built", { commit: site.commit })}</p>
</div>`;
      } else if (tab === "what/") {
        ({ html: body, fallback } = content(site, lang, p.slug));
      } else if (tab === "parts/") {
        body = rows ? bomTable(rows, tr) : notYet;
      } else if (tab === "build/") {
        const readme = p.readme ? `\n<li>${ext(blob(site, perLang(p.readme, lang)), tr("profile.readme"))}</li>` : "";
        const sch = p.schematic && `${root}img/${slug}/${escapeHtml(basename(p.schematic))}`;
        const schematic = sch ? `<h2 id="schematic">${e("profile.schematic")}</h2>
<p><a href="${sch}"><img class="schematic" src="${sch}" alt="${e("profile.schematic_alt")}"></a></p>
` : "";
        const plan = p.image ? `<h2 id="layout">${e("profile.plan")}</h2>
<p><img src="${root}img/${slug}/${escapeHtml(basename(p.image))}" alt="${e("profile.plan")}"></p>`
          : sch ? `<h2 id="layout">${e("profile.plan")}</h2>\n${notYet}` : notYet;
        const fileList = p.files?.length ? `
<p>${e("profile.files")}</p>
<ul>
${files(root)}
</ul>` : "";
        body = `${schematic}${plan}${fileList}
<h2 id="details">${e("profile.details")}</h2>
<ul>${readme}
<li>${ext(blob(site, perLang(p.docs, lang)), tr("profile.catalog"))}</li>
</ul>`;
      } else if (tab === "gallery/") {
        body = galleryList(p.gallery, tr, root, p.slug)
          ?? `<p class="not-yet">${e("gallery.none")} <a href="${here}status/">${e("tab.status")}</a></p>`;
      } else if (tab === "status/") {
        body = renderStatusSequence(status, tr);
      } else {
        body = downloadsTab(site, tr, lang, p, status, root, files(root), notYet);
      }
      const head = `<div class="build-head">
<img src="${root}img/builds/${slug}.svg" alt="">
<div><h1>${escapeHtml(title)}</h1>
<p class="lede">${e(`profile.${p.slug}.lede`)} <code class="profile-id">${escapeHtml(p.id)}</code></p></div>
</div>
<nav class="tabs" aria-label="${e("build.tabs")}">
${tabs}
</nav>
`;
      const name = tab ? `${tr(key)} · ` : "";
      return { title: `${name}${title} · DTMF-Signer`, body: head + body, fallback, wasm: tab === "" && sim };
    });
  }
}

// The links to a build's layout and parts files.
const buildFiles = (p, root) => (p.files ?? []).map((f) => {
  const name = escapeHtml(basename(f));
  return `<li><a href="${root}files/${escapeHtml(p.slug)}/${name}">${name}</a></li>`;
}).join("\n");

// A build's downloads: its released firmware (or, until there is one, how
// to build it), its language packs with their size and hash and how to
// install them, and the files for building it. On the downloads page the
// same block sits under the build's name, one heading level lower (h = 3)
// and with ids prefixed by the build.
function downloadsTab(site, tr, lang, p, status, root, files, notYet, h = 2, pre = "") {
  const e = (k, params) => escapeHtml(tr(k, params));
  // One release carries every build's UF2 (site.config.release.uf2, by
  // build) and one SHA256SUMS; a build shows it once its status says so.
  const rel = site.config.release;
  const uf2 = rel?.uf2?.[p.slug];
  const published = Boolean(uf2) && released(status);
  const guide = p.firmware_guide ? ext(blob(site, perLang(p.firmware_guide, lang)), tr("downloads.guide")) : "";
  let firmware = guide ? `<p class="not-yet">${e("downloads.unpublished")} ${guide}</p>` : notYet;
  if (published) {
    const base = `${site.config.repo}/releases/download/${rel.tag}/`;
    // A build with an installer (the PEX: firmware and language packs onto the
    // Pico in one go) offers it beside the UF2, with its three steps.
    const installer = p.installer ? escapeHtml(basename(p.installer)) : "";
    firmware = `<ul>
<li>${ext(base + uf2, uf2)}</li>${installer ? `\n<li><a href="${root}files/${escapeHtml(p.slug)}/${installer}" download>${installer}</a></li>` : ""}
<li>${ext(base + rel.sums, rel.sums)}</li>
<li>${ext(`${site.config.repo}/releases`, tr("downloads.all"))}</li>${guide ? `\n<li>${guide}</li>` : ""}
</ul>${installer ? `
<ol class="install-steps">
<li>${e("downloads.step_files", { uf2, installer })}</li>
<li>${e("downloads.step_run")} <code>python3 ${installer}</code></li>
<li>${e("downloads.step_bootsel")}</li>
</ol>` : ""}`;
  }
  const fwTitle = published ? e("downloads.firmware", { tag: rel.tag }) : e("downloads.firmware_none");
  const packs = site.packs.filter((k) => k.slug === p.slug).map((k) =>
    `<li class="pack"><a href="${root}${escapeHtml(k.href)}" download>${escapeHtml(k.name)}</a> (${escapeHtml(langName(site, k.lang))}, ${kib(k.size)})<br>
<span class="sha-label">SHA-256</span> <code class="sha">${k.sha256}</code></li>`).join("\n");
  const packBlock = packs ? `
<h${h} id="${pre}packs">${e("downloads.packs")}</h${h}>
<ul class="packs">
${packs}
</ul>
<p class="verify"><span aria-hidden="true">🔎</span> ${e("downloads.verify")}</p>
<p class="install">${p.packs_guide ? ext(blob(site, perLang(p.packs_guide, lang)), tr("downloads.packs_guide")) : ""}</p>${p.packs_contribute ? `
<p class="contribute"><span aria-hidden="true">🗣</span> ${ext(blob(site, perLang(p.packs_contribute, lang)), tr("downloads.packs_contribute"))}</p>` : ""}` : "";
  const fileBlock = files ? `
<h${h} id="${pre}files">${e("downloads.files")}</h${h}>
<ul>
${files}
</ul>` : "";
  return `<h${h} id="${pre}firmware">${fwTitle}</h${h}>
${firmware}${packBlock}${fileBlock}`;
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
  const repoRoot = join(site.dir, "..");
  const builds = site.config.profiles.map((p) => {
    const status = parseStatus(readFileSync(join(repoRoot, p.hardware, "status.json"), "utf8"));
    const notYet = `<p class="not-yet">${escapeHtml(tr("build.not_yet"))} <a href="../${escapeHtml(p.slug)}/status/">${escapeHtml(tr("tab.status"))}</a></p>`;
    return `<section class="dl-build" id="${escapeHtml(p.slug)}">
<h2>${escapeHtml(tr(`profile.${p.slug}.title`))}</h2>
${downloadsTab(site, tr, lang, p, status, root, buildFiles(p, root), notYet, 3, `${escapeHtml(p.slug)}-`)}
</section>`;
  }).join("\n");
  const body = `<h1>${escapeHtml(tr("downloads.title"))}</h1>
${builds}
${html}
${donateBlock(site, tr, root)}`;
  return { title: `${tr("downloads.title")} · DTMF-Signer`, body, fallback };
}

// The firmware as WebAssembly (tools/build-wasm.sh), when it has been built:
// the screen build, and the voice build (the PEX).
const WASM_DIR = join(HERE, "..", "build", "wasm");
const simModule = (p) => p.simulator?.module ?? "dtmf-signer.mjs";
const PHRASES = join(HERE, "..", "tools", "voice", "phrases.json");
const UR_TONES_JS = join(HERE, "..", "third_party", "ur-tones", "web", "src");
// The texts ui.mjs needs ({seconds} is filled in by ui.mjs).
const SIM_KEYS = ["sim.unavailable", "wallet.idle", "wallet.sending", "wallet.no_qr", "wallet.waiting", "wallet.got",
  "wallet.qr_repo", "wallet.qr_account", "wallet.qr_xpub", "wallet.no_broadcast", "mic.listen", "mic.stop", "mic.denied", "mic.unsupported", "status.idle", "status.line_in", "status.listening",
  "status.qr", "status.camera", "status.mic", "wallet.check_seed", "wallet.out", "wallet.change", "wallet.fee",
  "wallet.sig_ok", "wallet.sig_bad", "wallet.bytes", "card.sending", "card.what_seed", "card.what_psbt", "card.seed_title",
  "card.pin", "card.fp", "card.seed_note", "card.psbt_title", "card.send", "card.to", "card.change", "card.fee", "card.total", "card.psbt_note",
  "card.scan_ok", "card.scan_bad", "card.scan_qr", "card.size", "card.noqr_note", "card.listening", "card.listening_note"];
// The PEX's (voice-ui.mjs): the same wallet's card, by air.
const VOICE_SIM_KEYS = ["sim.unavailable", "wallet.out", "wallet.change", "wallet.fee", "wallet.sig_ok", "wallet.sig_bad",
  "wallet.bytes", "wallet.no_broadcast", "wallet.got", "phone.lift", "phone.hang", "status.on_hook", "status.speaking",
  "status.tones", "status.air_in", "status.air_out", "status.keypad", "status.listening_line", "say.pin_char", "say.tones",
  "status.mic", "mic.listen", "mic.stop", "mic.denied", "mic.unsupported", "card.sending", "card.what_seed", "card.what_psbt",
  "card.pin", "card.fp", "card.send", "card.to", "card.change", "card.fee", "card.total", "card.scan_ok", "card.scan_bad",
  "card.size", "card.listening", "card.listening_note", "card.hearing", "card.parts"];
// Its card titles and notes say "through the air" where Cable y QR's say "by cable".
const VOICE_CARD_AIR = { "card.seed_title": "card.seed_title_air", "card.seed_note": "card.seed_note_air",
  "card.psbt_title": "card.psbt_title_air", "card.psbt_note": "card.psbt_note_air", "card.scan_qr": "card.heard_air" };
const BUTTONS = [["left", "◀"], ["up", "▲"], ["ok", "●"], ["down", "▼"], ["right", "▶"]];
// A telephone's keypad, with the letters multi-tap types.
const KEYPAD = [["1", ""], ["2", "ABC"], ["3", "DEF"], ["4", "GHI"], ["5", "JKL"], ["6", "MNO"], ["7", "PQRS"], ["8", "TUV"], ["9", "WXYZ"], ["*", ""], ["0", ""], ["#", ""]];
// What each kind of link carries, in the legend's order: a port's type is
// its cable's (sound through the air is a type of its own).
const LEGEND = [["power", "bench.legend.power"], ["phone", "bench.legend.phone"], ["audio", "bench.legend.audio"],
  ["light", "bench.legend.light"], ["air", "bench.legend.air"]];

// A tooltip that works with the mouse, the keyboard (focus) and a tap.
// focus: false for elements that take the focus already (buttons, labels).
const tip = (text, focus = true) => `data-tip="${escapeHtml(text)}"${focus ? ' tabindex="0"' : ""}`;

// The inside of each kind of block.
export function blockBody(kind, tr, sim = {}, module = {}) {
  const e = (k, params) => escapeHtml(tr(k, params));
  switch (kind) {
    case "charger":
      return `<p class="mod-icon" aria-hidden="true">🔌</p>`;
    case "you":
      // You, by the handset: the voice is for you, the tones for the wallet.
      return `<p class="you-who"><span class="mod-icon" aria-hidden="true">🧑</span><span class="mod-title">${e(module.title)}</span> <small>${e("bench.you_what")}</small></p>`;
    case "voice-signer":
      return `<div class="vs-device" ${tip(tr("vsim.device"))} aria-label="${e("vsim.device")}">
<p class="mod-icon" aria-hidden="true">🗣</p>${module.led ? `
<span class="sim-led" ${tip(tr("sim.led", module.led))} aria-label="${e("sim.led", module.led)}"><span class="led-dot" aria-hidden="true"></span>LED</span>` : ""}
</div>`;
    case "phone": {
      const keys = KEYPAD.map(([k, abc]) => `<button type="button" data-key="${escapeHtml(k)}" aria-label="${escapeHtml(k)}">${escapeHtml(k)}<small aria-hidden="true">${abc}</small></button>`).join("\n");
      return `<div class="ph-phone">
<button type="button" class="ph-hook" aria-pressed="false" ${tip(tr("phone.hook_tip"), false)}><span aria-hidden="true">📞</span> <span class="ph-hook-label">${e("phone.lift")}</span></button>
<div class="ph-keypad" tabindex="0" role="group" aria-label="${e("phone.keypad")}" ${tip(tr("phone.keyboard"), false)}>
${keys}
</div>
<p class="ph-row"><button type="button" class="ph-flash" aria-label="${e("phone.flash")}" ${tip(tr("phone.flash"), false)}>R</button>
<button type="button" class="ph-type" aria-pressed="false" disabled ${tip(tr("phone.type_tip"), false)}><span aria-hidden="true">⌨</span> ${e("phone.type")}</button>
<label ${tip(tr("phone.sound"), false)}><input type="checkbox" class="ph-sound" checked> <span aria-hidden="true">🔊</span><span class="visually-hidden">${e("phone.sound")}</span></label></p>
</div>`;
    }
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
    case "wallet":
      return walletBody(tr, sim, module);
    default:
      throw new Error(`no block for kind ${kind}`);
  }
}

// The wallet (Cable y QR): one phone with an app, its connectors on its
// edge; who runs the app is chosen inside it, the simulated wallet or your
// own device (this computer's microphone hearing its speaker). The card
// area shows what you need for what you just did (ui.mjs fills it).
// Held to a telephone's handset (module.air, the PEX): it plays and
// listens through the air, so its third action is Listen, not Scan; yours
// is this computer's microphone and speakers in place of the handset's.
function walletBody(tr, sim, module) {
  const e = (k, params) => escapeHtml(tr(k, params));
  const air = Boolean(module.air);
  // The page starts on mainnet, as the selector does; ui.mjs follows it.
  const fp = sim.nets?.mainnet?.fingerprint ?? "";
  const act = (cls, icon, label, tipKey, params) =>
    `<button type="button" class="${cls}" ${tip(tr(tipKey, params), false)}><span aria-hidden="true">${icon}</span>${e(label)}</button>`;
  return `<div class="wl-screen">
<div class="wl-bar"><h3 class="mod-title">${e(module.title)}</h3>
<div class="seg" role="group" aria-label="${e("bench.who")}"><button type="button" data-who="sim" aria-pressed="true" ${tip(tr("wallet.who_sim_tip"), false)}>${e("wallet.who_sim")}</button><button type="button" data-who="yours" aria-pressed="false" ${tip(tr("wallet.who_yours_tip"), false)}>${e("wallet.who_yours")}</button></div>
</div>
<div class="wl-sim" data-panel="sim">
<dl class="wl-acct"><dt>${e("wallet.account")}</dt><dd>${e("wallet.account_test")}</dd>
<dt>${e("wallet.fingerprint")}</dt><dd class="wl-fp">${escapeHtml(fp)}</dd>
<dt>${e("wallet.pin")}</dt><dd>${escapeHtml(sim.pin ?? "")}</dd>
<dt>${e("wallet.net")}</dt><dd><span class="wl-net">mainnet</span> <span class="wd-warn" ${tip(tr("wallet.mismatch"))} aria-label="${e("wallet.mismatch")}" hidden>⚠</span></dd></dl>
<div class="wl-acts">${act("wd-seed", "🌱", "wallet.act_seed", air ? "wallet.seed_air_tip" : "wallet.seed_tip", { pin: sim.pin ?? "" })}${act("wd-psbt", "₿", "wallet.act_pay", "wallet.psbt_tip")}${air
    ? `<button type="button" class="wd-listen" aria-pressed="false" ${tip(tr("wallet.listen_tip"), false)}><span aria-hidden="true">👂</span>${e("wallet.listen")}</button>`
    : act("wd-scan", "📷", "wallet.act_scan", "wallet.scan_tip")}</div>
<div class="wl-card" aria-live="polite"></div>
<textarea class="wd-out" readonly rows="3" hidden aria-label="${e("wallet.got")}"></textarea>
<div class="wl-foot"><label ${tip(tr("wallet.network_tip"), false)}>${e("wallet.net")}
<select class="wd-net" autocomplete="off" aria-label="${e("wallet.network")}">
<option value="mainnet">mainnet</option>
<option value="testnet">testnet</option>
</select></label>
<button type="button" class="wd-stop" aria-label="${e("wallet.stop")}" ${tip(tr("wallet.stop"), false)}><span aria-hidden="true">⏹</span></button>
${air ? "" : `<label ${tip(tr("sim.sound"), false)}><input type="checkbox" class="wd-sound" checked> <span aria-hidden="true">🔊</span><span class="visually-hidden">${e("sim.sound")}</span></label>
`}<label ${tip(tr("sim.speed"), false)}><input type="checkbox" class="wd-fast"> <span aria-hidden="true">⏩</span><span class="visually-hidden">${e("sim.speed")}</span></label></div>
</div>
<div class="wl-yours" data-panel="yours" hidden>
<p class="yw-mic"><span aria-hidden="true">🎙</span> ${e("yw.mic")}</p>
${air ? `<p class="yw-ear"><span aria-hidden="true">🔊</span> ${e("yw.air_speakers")}</p>` : `<p class="yw-cam"><span aria-hidden="true">📷</span> ${e("yw.camera")}</p>`}
<button type="button" class="mic-toggle" aria-pressed="false">${e("mic.listen")}</button>
<meter class="mic-level" min="-80" max="0" value="-80" aria-label="${e("mic.level")}"></meter>
<div class="wl-card" aria-live="polite"></div>
<p class="yw-tools"><a class="yw-urtones" href="${escapeHtml(sim.tools?.urtones ?? "")}" rel="noopener noreferrer" ${tip(tr("yw.tones_tip"), false)}><span aria-hidden="true">🎵</span> ${e("yw.tones")}</a>
${air ? `<a class="yw-listen" href="${escapeHtml(sim.tools?.urtones ?? "")}" rel="noopener noreferrer" ${tip(tr("yw.listen_tip"), false)}><span aria-hidden="true">👂</span> ${e("yw.listen")}</a>`
    : `<a class="yw-scan" href="${escapeHtml(sim.tools?.nunchuk ?? "")}" rel="noopener noreferrer" ${tip(tr("yw.scan_tip"), false)}><span aria-hidden="true">📷</span> ${e("yw.scan")}</a>`}</p>
<p class="mic-note">${e(air ? "yw.handset_note" : "mic.note")}</p>
</div>
</div>`;
}

// The first time on a simulator: a static, foldable list of what to try,
// closed in the page; ui.mjs opens it on a first visit only. Nothing in it
// tracks, highlights or blocks: the bench is free to use from the start.
function firstHint(tr, voice = false) {
  const b = (icon, key) => `<b>${icon} ${escapeHtml(tr(key))}</b>`;
  const step = (key, params = {}) => {
    let t = escapeHtml(tr(key));
    for (const [k, v] of Object.entries(params)) t = t.replaceAll(`{${k}}`, v);
    return `<li>${t}</li>`;
  };
  // The PEX: the telephone's keys, the wallet by the handset.
  const keys = { hash: "<b>#</b>", one: "<b>1</b>", two: "<b>2</b>", flash: "<b>R</b>", type: b("⌨", "phone.type"),
    seed: b("🌱", "wallet.act_seed"), pay: b("₿", "wallet.act_pay"), listen: b("👂", "wallet.listen"),
    // The test seed's keypad code: voice-ui.mjs fills it from what ⌨ Type code dials.
    code: `<details class="kcode"><summary>${escapeHtml(tr("first.show_code"))}</summary><code class="keypad-code"></code></details>` };
  const steps = voice ? [1, 2, 3, 4].map((n) => step(`first.voice.${n}`, keys)) : [
    step("first.1", { seed: b("🌱", "wallet.act_seed") }), step("first.2"),
    step("first.3", { pay: b("₿", "wallet.act_pay") }), step("first.4", { scan: b("📷", "wallet.act_scan") })];
  return `<details class="first">
<summary>${escapeHtml(tr("first.title"))}</summary>
<ol>
${steps.join("\n")}
</ol>
</details>`;
}

// What the signer can say on a page, for the text alternative: the phrases
// of each language it can speak there (English, built in, and the page's
// pack), without the letters and number words (the echo spells those).
export function voicePhrases(langs) {
  const all = JSON.parse(readFileSync(PHRASES, "utf8"));
  const out = {};
  for (const l of langs) {
    out[l] = {};
    for (const [id, t] of Object.entries(all)) {
      if (/^[cn]_/.test(id)) continue;
      if (t[l]) out[l][id] = t[l];
      if (t.long?.[l]) out[l][`${id}_long`] = t.long[l];   // "say <id>_long": the longer help
    }
  }
  return out;
}

// The voices' credits (app/voice/VOICES.md): the Spanish one is CC BY 3.0.
function voiceCredits(tr) {
  const a = (href, text) => ext(escapeHtml(href), text);
  const line = (key, voice, corpus, licence) => escapeHtml(tr(key)).replace("{voice}", voice).replace("{corpus}", corpus).replace("{licence}", licence);
  return `<ul class="sim-credits">
<li><span aria-hidden="true">🗣</span> ${line("voice.credit_en", a("https://huggingface.co/rhasspy/piper-voices/tree/main/en/en_US/ljspeech/medium", "Piper en_US-ljspeech-medium"),
    a("https://keithito.com/LJ-Speech-Dataset/", "LJ Speech"), escapeHtml(tr("voice.public_domain")))}</li>
<li><span aria-hidden="true">🗣</span> ${line("voice.credit_es", a("https://huggingface.co/rhasspy/piper-voices/tree/main/es/es_ES/sharvard/medium", "Piper es_ES-sharvard-medium"),
    a("https://datashare.ed.ac.uk/handle/10283/574", tr("voice.sharvard")), a("https://creativecommons.org/licenses/by/3.0/", "CC BY 3.0"))}</li>
</ul>`;
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
// ui.mjs or, for the voice build, voice-ui.mjs), the mode switch, the
// legend, and its data as JSON (not run).
function simulatorBlock(site, tr, p, root, lang) {
  const s = p.simulator;
  const bench = p.bench;
  const voice = simModule(p) === "dtmf-signer-voice.mjs";
  const strings = Object.fromEntries((voice ? VOICE_SIM_KEYS : SIM_KEYS).map((k) => [k, tr(k)]));
  if (voice) for (const [k, air] of Object.entries(VOICE_CARD_AIR)) strings[k] = tr(air);
  const links = Object.fromEntries(bench.modes.map((m) => [m.id, m.links]));
  // The voice build: the page's language pack, if there is one (English is
  // built in), and what it can say.
  const pack = voice && s.packs?.[lang] ? lang : null;
  const extra = voice ? { pack, phrases: voicePhrases(pack ? ["en", pack] : ["en"]) } : { tools: s.tools };
  const nets = simNetworks(site, p);
  const data = JSON.stringify({ pin: s.pin, networks: nets, ...extra, repo: site.config.repo, strings, links }).replace(/</g, "\\u003c");
  const e = (k, params) => escapeHtml(tr(k, params));
  const first = bench.modes[0];
  const label = (l) => (l.startsWith("bench.") ? tr(l) : l);
  const modesOf = (id) => bench.modes.filter((m) => m.modules.includes(id)).map((m) => m.id).join(" ");
  const block = (m) => {
    const ports = (m.ports ?? []).map((pt) => {
      const t = tr(`bench.tip.${m.id}.${pt.id}`);
      return `<span class="port p-${pt.type} edge-${pt.edge}" data-port="${m.id}.${pt.id}" data-type="${pt.type}" data-dir="${pt.dir}" ${tip(t)} aria-label="${escapeHtml(label(pt.label) + ": " + t)}">${escapeHtml(label(pt.label))}</span>`;
    }).join("\n");
    const hidden = first.modules.includes(m.id) ? "" : " hidden";
    return `<section class="mod mod-${m.kind}" data-module="${m.id}"${m.air ? " data-air" : ""} data-column="${m.column}" data-modes="${modesOf(m.id)}" aria-label="${e(m.title)}"${hidden}>
${m.kind === "wallet" || m.kind === "you" ? "" : `<h3 class="mod-title">${e(m.title)}</h3>\n`}${ports}
${blockBody(m.kind, tr, { ...s, nets }, m)}
<p class="mod-help" aria-hidden="true"></p>
</section>`;
  };
  const column = (c) => `<div class="col col-${c}">\n${bench.modules.filter((m) => m.column === c).map(block).join("\n")}\n</div>`;
  // Only what this bench's links carry.
  const portType = new Map(bench.modules.flatMap((m) => (m.ports ?? []).map((pt) => [`${m.id}.${pt.id}`, pt.type])));
  const carried = new Set(bench.modes.flatMap((m) => m.links.map((l) => portType.get(l.from))));
  const legend = LEGEND.filter(([t]) => carried.has(t)).map(([t, k]) => `<li class="l-${t}"><i aria-hidden="true"></i>${e(k)}</li>`).join("\n");
  const connections = bench.modes.map((m) => `<ul class="connections visually-hidden" data-modes="${m.id}"${m === first ? "" : " hidden"}>
${m.links.map((l) => `<li>${e(`bench.link.${m.id}.${l.channel}`)}</li>`).join("\n")}
</ul>`).join("\n");
  return `<div class="simulator" id="sim"${voice ? ' data-firmware="voice"' : ""}>
<p class="sim-mobile" hidden><span aria-hidden="true">📱</span> ${e("sim.mobile")} <button type="button" class="sim-mobile-close" aria-label="${e("sim.close")}">✕</button></p>
<p class="sim-warning" ${tip(tr("sim.warning"))}><span aria-hidden="true">⚠</span> ${e("sim.warning_short")}</p>
${bench.modules.some((m) => m.kind === "wallet") ? firstHint(tr, voice) + "\n" : ""}<div class="bench" data-mode="${first.id}" data-orient="h"${bench.turn ? ` data-turn="${Number(bench.turn)}"` : ""}>
${column("power")}
${column("signer")}
${column("peer")}
${bench.modules.some((m) => m.column === "far") ? column("far") + "\n" : ""}<svg class="cables" aria-hidden="true"></svg>${voice ? `
<p class="bubble" aria-live="polite"><small>${e("bench.bubble")}</small><span class="said"></span></p>` : ""}
</div>
<p class="sim-status" role="status" aria-live="polite">${e("sim.loading")}</p>
<ul class="legend">
${legend}
</ul>
${connections}
${voice ? "" : `<pre class="screen-text visually-hidden" aria-live="polite"></pre>\n`}<details class="sim-echo"><summary><span aria-hidden="true">›_</span> ${e("sim.echo")}</summary><pre class="echo"></pre></details>
<noscript><p>${e("sim.unavailable")}</p></noscript>
${voice ? voiceCredits(tr) + "\n" : ""}<p class="sim-built">${e("sim.built_from", { commit: site.commit })}</p>
</div>
<script type="application/json" id="sim-data">${data}</script>
<script src="${root}sim/ur-tones-crypto.js"></script>
<script src="${root}sim/ur-tones.js"></script>
<script type="module" src="${root}sim/${voice ? "voice-ui" : "ui"}.mjs"></script>`;
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
  // The modules each simulator needs that have been built.
  site.modules = new Set(site.config.profiles.filter((p) => p.simulator).map(simModule).filter((f) => existsSync(join(WASM_DIR, f))));
  if (site.config.profiles.some((p) => p.bench))
    checkBenches(site.config.profiles, readFileSync(join(siteDir, "..", "docs", "catalog.md"), "utf8"));
  for (const p of site.config.profiles)
    if (p.simulator && !site.modules.has(simModule(p)))
      console.error(`warning: no simulator for ${p.slug} (build/wasm/${simModule(p)} missing: run tools/build-wasm.sh)`);
  const files = new Map();
  // Downloads and donations are optional in config.json ("release", "donate").
  const nav = [{ key: "nav.builds", href: "#builds" }];
  if (site.config.release) nav.push({ key: "nav.downloads", href: "downloads/" });

  files.set("index.html", Buffer.from(rootPage(site)));
  // The shared files at the top of static/ (static/sim/ goes with a simulator).
  for (const entry of readdirSync(join(HERE, "static"), { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name)))
    if (entry.isFile()) files.set(entry.name, readFileSync(join(HERE, "static", entry.name)));

  const repoRoot = join(siteDir, "..");
  site.packs = packList(site, repoRoot);
  // The picture a shared link shows (see shareFor in lib/page.mjs).
  if (site.config.url) {
    const og = readFileSync(join(HERE, "static", "img", "og.jpg"));
    const meta = photoMetadata(og);
    if (meta) throw new Error(`static/img/og.jpg: remove its metadata (${meta}) before publishing it`);
    files.set("img/og.jpg", og);
  }
  // Each build's pictogram.
  for (const f of readdirSync(join(HERE, "static", "img", "builds")).filter((f) => f.endsWith(".svg")).sort())
    files.set(`img/builds/${f}`, readFileSync(join(HERE, "static", "img", "builds", f)));
  if (site.config.donate)
    for (const img of ["donate-bitcoin.png", "donate-lightning.png"])
      files.set(`img/${img}`, readFileSync(join(repoRoot, "docs", "images", img)));
  for (const p of site.config.profiles) {
    if (p.image) files.set(`img/${p.slug}/${basename(p.image)}`, readFileSync(join(repoRoot, p.image)));
    if (p.schematic) files.set(`img/${p.slug}/${basename(p.schematic)}`, readFileSync(join(repoRoot, p.schematic)));
    for (const f of p.files ?? []) files.set(`files/${p.slug}/${basename(f)}`, readFileSync(join(repoRoot, f)));
    if (p.installer) files.set(`files/${p.slug}/${basename(p.installer)}`, readFileSync(join(repoRoot, p.installer)));
    for (const it of (p.gallery ?? []).filter((g) => g.kind === "photo")) {
      const bytes = readFileSync(join(repoRoot, it.src));
      const meta = photoMetadata(bytes);
      if (meta) throw new Error(`${it.src}: remove its metadata (${meta}) before publishing it`);
      files.set(photoHref(p.slug, it.src), bytes);
    }
  }
  if (site.modules.size) {
    // Every file of static/sim/, so a new module cannot be left behind.
    for (const f of readdirSync(join(HERE, "static", "sim"), { withFileTypes: true }).filter((e) => e.isFile()).map((e) => e.name).sort())
      files.set(`sim/${f}`, readFileSync(join(HERE, "static", "sim", f)));
    for (const m of [...site.modules].sort()) files.set(`sim/${m}`, readFileSync(join(WASM_DIR, m)));
    files.set("sim/ur-tones-crypto.js", readFileSync(join(UR_TONES_JS, "crypto.js")));
    files.set("sim/ur-tones.js", readFileSync(join(UR_TONES_JS, "ur-tones.js")));
  }
  // The language packs: the .bin as the Pico takes it, always (a download),
  // and, for a built simulator, the same bytes as a module the page can
  // import (its CSP lets it load scripts from the site, and fetch nothing).
  for (const k of site.packs) {
    files.set(k.href, k.bytes);
    const p = site.config.profiles.find((x) => x.slug === k.slug);
    if (site.modules.has(simModule(p)))
      files.set(`sim/packs/${k.lang}.mjs`, Buffer.from(`// ${basename(p.simulator.packs[k.lang])}, base64 (the same bytes as ${k.lang}.bin).\nexport default "${k.bytes.toString("base64")}";\n`));
  }

  for (const { code: lang } of site.config.languages) {
    const page = (path, make) => {
      const tr = translator(site, lang);
      const root = "../".repeat(1 + path.split("/").filter(Boolean).length);
      const { title, body, fallback, wasm } = make(tr, root);
      files.set(`${lang}/${path}index.html`,
        Buffer.from(layout({ site, lang, tr, root, title, body, path, nav, fallback, wasm })));
    };
    page("", (tr) => homePage(site, lang, tr));
    if (site.config.release) page("downloads/", (tr) => downloadsPage(site, lang, tr, "../../"));
    for (const p of site.config.profiles) buildPages(site, lang, p, page);
    // A build's old addresses (moved_from) still lead to it, tab by tab.
    for (const p of site.config.profiles) for (const old of p.moved_from ?? [])
      for (const [tab] of TABS) files.set(`${lang}/${old}/${tab}index.html`, Buffer.from(movedPage(`${old}/${tab}`, `${p.slug}/${tab}`)));
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
