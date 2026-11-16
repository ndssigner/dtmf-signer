// Builds the website into build/site/:
//     node site/build.mjs
// Deterministic: no dates and nothing random, so the same commit gives the
// same bytes. The commit shown in the footer comes from SITE_COMMIT, or
// from git. Prints the SHA-256 of every file, then of that list.
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { loadSite } from "./lib/i18n.mjs";
import { renderMarkdown } from "./lib/markdown.mjs";
import { layout, rootPage, translator } from "./lib/page.mjs";

const HERE = fileURLToPath(new URL(".", import.meta.url));

// The prose of a page in a language, or the English one (fallback: true).
function content(site, lang, name) {
  for (const code of [lang, "en"]) {
    const file = join(site.dir, "content", code, name + ".md");
    if (existsSync(file)) return { html: renderMarkdown(readFileSync(file, "utf8")), fallback: code !== lang };
  }
  throw new Error(`no content/en/${name}.md`);
}

function homePage(site, lang, tr) {
  const { html, fallback } = content(site, lang, "home");
  return { title: "DTMF-Signer", body: html, fallback };
}

export function build({ siteDir, outDir, commit }) {
  const site = loadSite(siteDir);
  site.commit = commit;
  const files = new Map();
  const nav = [{ key: "nav.home", href: "" }];

  files.set("index.html", Buffer.from(rootPage(site)));
  for (const name of readdirSync(join(HERE, "static")).sort())
    files.set(name, readFileSync(join(HERE, "static", name)));

  for (const { code: lang } of site.config.languages) {
    const page = (path, make) => {
      const tr = translator(site, lang);
      const root = "../".repeat(1 + path.split("/").filter(Boolean).length);
      const { title, body, fallback } = make(tr);
      files.set(`${lang}/${path}index.html`,
        Buffer.from(layout({ site, lang, tr, root, title, body, path, nav, fallback })));
    };
    page("", (tr) => homePage(site, lang, tr));
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
