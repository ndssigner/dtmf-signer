// Builds a site into a fresh temporary folder and checks its links.
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, posix } from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "../build.mjs";

export const SITE = fileURLToPath(new URL("../", import.meta.url));
export const FIX = fileURLToPath(new URL("fixtures/i18n/", import.meta.url));

export function buildInto(siteDir = SITE, commit = "abc1234") {
  return build({ siteDir, outDir: mkdtempSync(join(tmpdir(), "site-")), commit });
}

export const text = (files, path) => files.get(path).toString("utf8");

// Every href/src of every page that is not external, mailto or an anchor,
// resolved against the page's folder ("x/" means "x/index.html").
export function brokenLinks(files) {
  const broken = [];
  for (const [path, bytes] of files) {
    if (!path.endsWith(".html")) continue;
    for (const m of bytes.toString("utf8").matchAll(/(?:href|src)="([^"]*)"/g)) {
      const url = m[1].replace(/#.*$/, "");
      if (url === "" || /^(https?:|mailto:)/.test(m[1])) continue;
      let target = posix.normalize(posix.join(posix.dirname(path), url));
      if (url.endsWith("/") || target === ".") target = posix.join(target, "index.html");
      if (!files.has(target)) broken.push(`${path}: ${m[1]}`);
    }
  }
  return broken;
}
