// The page template: every page of the site goes through layout(), which
// sets the language, the Content-Security-Policy (no network beyond the
// site's own files), the navigation, the language switch, the notice for
// text shown in English, and the footer with the commit.
import { escapeHtml } from "./markdown.mjs";
import { t } from "./i18n.mjs";

export const CSP = "default-src 'none'; img-src 'self'; style-src 'self'; script-src 'self'; base-uri 'none'; form-action 'none'";

// A translator for one page: tr(key, params) gives the text and remembers
// whether any of it came from English.
export function translator(site, lang) {
  const tr = (key, params) => {
    const r = t(site, lang, key, params);
    if (r.fallback) tr.fallback = true;
    return r.text;
  };
  tr.fallback = false;
  return tr;
}

export const CSP_WASM = CSP.replace("script-src 'self'", "script-src 'self' 'wasm-unsafe-eval'");

// The preview a link to the site gets when it is shared (Open Graph and
// Twitter cards): only with config.url, since they need absolute
// addresses. Nothing here is loaded by the page itself.
function shareMeta(share) {
  if (!share) return "";
  const e = escapeHtml;
  return `
<meta name="description" content="${e(share.description)}">
<meta property="og:type" content="website">
<meta property="og:site_name" content="DTMF-Signer">
<meta property="og:title" content="${e(share.title)}">
<meta property="og:description" content="${e(share.description)}">
<meta property="og:url" content="${e(share.url)}">
<meta property="og:image" content="${e(share.image)}">
<meta property="og:image:width" content="1280">
<meta property="og:image:height" content="640">
<meta property="og:image:alt" content="${e(share.alt)}">
<meta name="twitter:card" content="summary_large_image">`;
}

// share: { url, title, description, image, alt }, or nothing.
export function shareFor(site, lang, title, path) {
  const base = site.config.url;
  if (!base) return null;
  return {
    url: `${base}${lang}/${path}`,
    title,
    description: t(site, lang, "home.lede").text,
    image: `${base}img/og.jpg`,
    alt: t(site, lang, "share.alt").text,
  };
}

function head(lang, title, root, wasm = false, share = null) {
  return `<!doctype html>
<html lang="${lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="${wasm ? CSP_WASM : CSP}">
<title>${escapeHtml(title)}</title>${shareMeta(share)}
<link rel="stylesheet" href="${root}style.css">
<script type="module" src="${root}lang.js"></script>
</head>`;
}

// nav: [{ key, href }] relative to the language folder ("" is its home).
// path: this page's path inside the language folder ("" or "cable-qr/").
// fallback: true if the page's own content came from English.
// wasm: the page runs WebAssembly (the simulator), and only that page may.
// The specification's address in the repository, in the page's language
// when it has one (config.spec: language -> path, English as the fallback).
export function specHref(site, lang) {
  const specPath = site.config.spec?.[lang] ?? site.config.spec?.en;
  return specPath ? `${site.config.repo}/blob/main/${specPath}` : null;
}

export function layout({ site, lang, tr, root, title, body, path, nav, fallback, wasm = false }) {
  const repo = escapeHtml(site.config.repo);
  const spec = specHref(site, lang);
  const links = nav.map(({ key, href }) =>
    `<a href="${root}${lang}/${href}">${escapeHtml(tr(key))}</a>`);
  if (spec)
    links.push(`<a href="${escapeHtml(spec)}" rel="noopener noreferrer">${escapeHtml(tr("nav.spec"))}</a>`);
  // The language switch: a dropdown (a <details>, no script needed) whose
  // links keep the page the reader is on.
  const current = escapeHtml(t(site, lang, "lang.name").text);
  const langs = `<details class="langs">
<summary aria-label="${escapeHtml(tr("nav.lang"))}: ${current}"><span aria-hidden="true">🌐</span> ${current}</summary>
<ul>
${site.config.languages.map(({ code }) => {
    const name = escapeHtml(t(site, code, "lang.name").text);
    return code === lang
      ? `<li><span aria-current="true" lang="${code}">${name}</span></li>`
      : `<li><a href="${root}${code}/${path}" hreflang="${code}" data-lang="${code}" lang="${code}">${name}</a></li>`;
  }).join("\n")}
</ul>
</details>`;
  const footer = escapeHtml(tr("footer.built", { commit: site.commit }));
  const ur = site.config.urtones;
  const urLinks = ur
    ? `<span class="urtones">${escapeHtml(tr("footer.urtones"))}: <a href="${escapeHtml(ur.spec)}" rel="noopener noreferrer">${escapeHtml(tr("footer.urtones_spec"))}</a> · <a href="${escapeHtml(ur.web)}" rel="noopener noreferrer">${escapeHtml(tr("footer.urtones_web"))}</a></span>`
    : "";
  const repoLink = `<a href="${repo}" rel="noopener noreferrer">${escapeHtml(tr("nav.repo"))}</a>`;
  // Read last: tr() above may have used English.
  const notice = fallback || tr.fallback
    ? `<p class="notice">${escapeHtml(t(site, lang, "fallback.notice").text)} <a href="${repo}/blob/main/site/TRANSLATING.md" rel="noopener noreferrer">TRANSLATING.md</a></p>\n`
    : "";
  return `${head(lang, title, root, wasm, shareFor(site, lang, title, path))}
<body>
<header class="top">
<a class="brand" href="${root}${lang}/">DTMF-Signer</a>
<nav class="site">
${links.join("\n")}
</nav>
${langs}
</header>
${notice}<main>
${body}
</main>
<footer><p>${footer}</p>
${repoLink}
${urLinks}</footer>
</body>
</html>
`;
}

// The root page: one link per language, readable with no script; lang.js
// sends a visitor on to the remembered or preferred language.
export function rootPage(site) {
  const items = site.config.languages.map(({ code }) =>
    `<li><a href="${code}/" hreflang="${code}" data-lang="${code}">${escapeHtml(t(site, code, "lang.name").text)}</a></li>`).join("\n");
  const codes = site.config.languages.map((l) => l.code).join(" ");
  const share = site.config.url && { ...shareFor(site, "en", "DTMF-Signer", ""), url: site.config.url };
  return head("en", "DTMF-Signer", "", false, share).replace('<html lang="en">', `<html lang="en" data-langs="${codes}">`) + `
<body>
<main class="root">
<h1>DTMF-Signer</h1>
<ul class="languages">
${items}
</ul>
</main>
</body>
</html>
`;
}
