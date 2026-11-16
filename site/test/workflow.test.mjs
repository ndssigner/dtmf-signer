import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const yml = readFileSync(new URL("../../.github/workflows/site.yml", import.meta.url), "utf8");

test("only the deploy job is serialized: pull requests never cancel a pending deploy", () => {
  assert.doesNotMatch(yml, /^concurrency:/m, "no workflow-wide concurrency group");
  const pages = yml.slice(yml.indexOf("\n  pages:"));
  assert.match(pages, /\n    concurrency:\n      group: pages\n      cancel-in-progress: false/);
});
