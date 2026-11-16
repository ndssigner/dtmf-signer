// The site's languages: one flat JSON file of strings per language
// (site/i18n/<code>.json) and one folder of prose (site/content/<code>/).
// English is complete and is the fallback for everything else.
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

export function loadSite(siteDir) {
  const config = JSON.parse(readFileSync(join(siteDir, "config.json"), "utf8"));
  config.profiles ??= [];
  for (const { code } of config.languages)
    if (!/^[a-z]{2,3}(-[a-z0-9]+)?$/.test(code)) throw new Error(`language code ${JSON.stringify(code)}: use a plain code such as "pt" or "pt-br"`);
  const strings = {};
  for (const { code } of config.languages) {
    const file = join(siteDir, "i18n", code + ".json");
    if (existsSync(file)) strings[code] = JSON.parse(readFileSync(file, "utf8"));
  }
  return { dir: siteDir, config, strings };
}

export function t(site, lang, key, params = {}) {
  let text = site.strings[lang]?.[key];
  const fallback = text === undefined;
  if (fallback) text = site.strings.en?.[key];
  if (text === undefined) throw new Error("missing key " + key);
  text = text.replace(/\{(\w+)\}/g, (all, name) => (name in params ? String(params[name]) : all));
  return { text, fallback };
}

const placeholders = (s) => [...s.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort().join(",");

function contentFiles(siteDir, code) {
  const dir = join(siteDir, "content", code);
  return existsSync(dir) ? readdirSync(dir).filter((f) => f.endsWith(".md")).sort() : [];
}

export function checkI18n(siteDir) {
  const errors = [], warnings = [];
  const site = loadSite(siteDir);
  const en = site.strings.en ?? {};
  const enContent = contentFiles(siteDir, "en");
  for (const { code, complete } of site.config.languages) {
    const strings = site.strings[code];
    if (!strings) {
      errors.push(`${code}: no i18n/${code}.json`);
      continue;
    }
    if (!("lang.name" in strings)) errors.push(`${code}: missing key lang.name (the language's own name, shown in the language switch)`);
    const missing = (what) => (complete ? errors : warnings).push(`${code}: missing ${what}`);
    for (const key of Object.keys(en)) {
      if (!(key in strings)) missing(`key ${key}`);
      else if (placeholders(strings[key]) !== placeholders(en[key]))
        errors.push(`${code}: key ${key} has placeholders {${placeholders(strings[key])}}, English has {${placeholders(en[key])}}`);
    }
    for (const key of Object.keys(strings))
      if (!(key in en)) errors.push(`${code}: key ${key} is not in English`);
    const have = new Set(contentFiles(siteDir, code));
    for (const f of enContent) if (!have.has(f)) missing(`content/${code}/${f}`);
  }
  return { errors, warnings };
}
