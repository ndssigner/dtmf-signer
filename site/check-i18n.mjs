// Checks the site's translations against English:
//     node site/check-i18n.mjs
// Errors (exit 1): a key or a content file missing in a complete language,
// a key English lacks, placeholders that differ. Warnings: what an
// incomplete language still lacks (it shows the English text meanwhile).
import { fileURLToPath } from "node:url";
import { checkI18n } from "./lib/i18n.mjs";

const { errors, warnings } = checkI18n(fileURLToPath(new URL(".", import.meta.url)));
for (const w of warnings) console.log("warning: " + w);
for (const e of errors) console.log("error: " + e);
console.log(`${errors.length} errors, ${warnings.length} warnings`);
process.exit(errors.length ? 1 : 0);
