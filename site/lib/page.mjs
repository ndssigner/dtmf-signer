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

function head(lang, title, root, wasm = false) {
  return `<!doctype html>
<html lang="${lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="${wasm ? CSP_WASM : CSP}">
<title>${escapeHtml(title)}</title>
<link rel="stylesheet" href="${root}style.css">
<script type="module" src="${root}lang.js"></script>
</head>`;
}

// nav: [{ key, href }] relative to the language folder ("" is its home).
// path: this page's path inside the language folder ("" or "cable-qr/").
// fallback: true if the page's own content came from English.
// wasm: the page runs WebAssembly (the simulator), and only that page may.
export function layout({ site, lang, tr, root, title, body, path, nav, fallback, wasm = false }) {
  const repo = escapeHtml(site.config.repo);
  const links = nav.map(({ key, href }) =>
    `<a href="${root}${lang}/${href}">${escapeHtml(tr(key))}</a>`).join("\n");
  const langs = site.config.languages.map(({ code }) => {
    const name = escapeHtml(t(site, code, "lang.name").text);
    return code === lang
      ? `<span aria-current="true">${name}</span>`
      : `<a href="${root}${code}/${path}" hreflang="${code}" data-lang="${code}">${name}</a>`;
  }).join("\n");
  const footer = escapeHtml(tr("footer.built", { commit: site.commit }));
  const repoLink = `<a href="${repo}" rel="noopener noreferrer">${escapeHtml(tr("nav.repo"))}</a>`;
  // Read last: tr() above may have used English.
  const notice = fallback || tr.fallback
    ? `<p class="notice">${escapeHtml(t(site, lang, "fallback.notice").text)} <a href="${repo}/blob/main/site/TRANSLATING.md" rel="noopener noreferrer">TRANSLATING.md</a></p>\n`
    : "";
  return `${head(lang, title, root, wasm)}
<body>
<header>
<a class="brand" href="${root}${lang}/">DTMF-Signer</a>
<nav class="site">
${links}
${repoLink}
</nav>
<nav class="langs">
${langs}
</nav>
</header>
${notice}<main>
${body}
</main>
<footer><p>${footer}</p></footer>
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
  return head("en", "DTMF-Signer", "").replace('<html lang="en">', `<html lang="en" data-langs="${codes}">`) + `
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
