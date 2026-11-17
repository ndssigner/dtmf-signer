import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { buildInto, brokenLinks, text, SITE } from "./helpers.mjs";
import { freqs, wavePoints, caption, play, KEYS, LOW, HIGH } from "../static/dtmf-keypad.mjs";

const files = buildInto();
const BUILDS = ["cable-qr", "centralita"];
const HARDWARE = { "cable-qr": "breadboard", centralita: "phone" };
const URTONES = { spec: "https://github.com/ndssigner/ur-tones/blob/main/SPEC.md", web: "https://ndssigner.github.io/ur-tones/" };

test("the keys and their two tones", () => {
  assert.deepEqual(KEYS, ["123A", "456B", "789C", "*0#D"]);
  assert.deepEqual(freqs("9"), [852, 1477]);
  assert.deepEqual(freqs("D"), [941, 1633]);
  assert.deepEqual(freqs("1"), [LOW[0], HIGH[0]]);
  for (const bad of ["x", "", "12", "a"]) assert.throws(() => freqs(bad));
});
test("wavePoints: three lines of 201 points inside the 400 x 54 box", () => {
  const w = wavePoints(852, 1477);
  for (const k of ["low", "high", "sum"]) {
    const pts = w[k].split(" ").map((p) => p.split(",").map(Number));
    assert.equal(pts.length, 201, k);
    for (const [x, y] of pts) assert.ok(x >= 0 && x <= 400 && y >= 0 && y <= 54, `${k} ${x},${y}`);
  }
  assert.equal(wavePoints(697, 1209, 10).sum.split(" ").length, 11);
});
test("caption fills in the key and both tones", () => {
  assert.equal(caption("«{key}» = {low} Hz + {high} Hz", "9"), "«9» = 852 Hz + 1477 Hz");
});
test("play: both tones, 200 ms, 10 ms ramps, gain 0.18", () => {
  const log = [];
  const param = (name) => ({ setValueAtTime: (v, t) => log.push([name, "set", v, t]), linearRampToValueAtTime: (v, t) => log.push([name, "ramp", v, t]) });
  const ctx = { currentTime: 1, destination: "out",
    createGain: () => ({ gain: param("gain"), connect: (d) => log.push(["connect", d]) }),
    createOscillator: () => { const o = { frequency: {}, connect() {}, start: (t) => log.push(["start", o.frequency.value, t]), stop: (t) => log.push(["stop", o.frequency.value, t]) }; return o; } };
  play(ctx, 852, 1477);
  assert.deepEqual(log.filter((l) => l[0] === "start").map((l) => l[1]), [852, 1477]);
  const ramps = log.filter((l) => l[1] === "ramp");
  assert.deepEqual(ramps.map((l) => l[2]), [0.18, 0]);
  assert.ok(Math.abs(ramps[0][3] - 1.01) < 1e-9 && Math.abs(ramps[1][3] - 1.2) < 1e-9);
});

test("the keypad is a module script from the site, and mounts on its element", () => {
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/index.html`);
    assert.match(html, /<script type="module" src="\.\.\/dtmf-keypad\.mjs"><\/script>/);
    assert.match(html, /<div class="dtmf" data-keypad data-hint="[^"]+" data-caption="[^"]*\{key\}[^"]*\{low\}[^"]*\{high\}[^"]*"/);
    assert.ok(files.has("dtmf-keypad.mjs"));
    assert.doesNotMatch(html, /<script(?![^>]*\bsrc=)/);
  }
});
test("the hero: title, two calls to action, ur-tones links from the config", () => {
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/index.html`);
    assert.match(html, /<h1>[^<]+<\/h1>/);
    const cta = /<div class="cta">([\s\S]*?)<\/div>/.exec(html)[1];
    assert.equal([...cta.matchAll(/<a /g)].length, 2);
    assert.match(cta, /href="#builds"/);
    assert.match(cta, lang === "es" ? /docs\/es\/especificacion\.md/ : /blob\/main\/SPEC\.md/);
    const main = html.match(/<main>[\s\S]*<\/main>/)[0];
    assert.ok(main.includes(`href="${URTONES.spec}"`) && main.includes(`href="${URTONES.web}"`));
  }
});
test("the builds section has the id the header links to, and one row per build", () => {
  for (const lang of ["en", "es"]) {
    const html = text(files, `${lang}/index.html`);
    assert.match(html, /<h2 id="builds">/);
    assert.match(html, /<a href="\.\.\/en\/#builds"|href="[^"]*#builds"/);
    const rows = html.match(/<article class="build">[\s\S]*?<\/article>/g);
    assert.equal(rows.length, BUILDS.length);
    rows.forEach((row, i) => {
      const slug = BUILDS[i];
      assert.match(row, new RegExp(`<img src="\\.\\./img/builds/${slug}\\.svg" alt="">`));
      assert.equal([...row.matchAll(/<span class="chip">/g)].length, 3);
      for (const href of [`${slug}/`, `${slug}/parts/`]) {
        assert.ok(row.includes(`href="${href}"`), `${lang} ${href}`);
        assert.ok(files.has(`${lang}/${href}index.html`), href);
      }
    });
  }
});
test("each row's stage dots match its status.json, and are not colour-only", () => {
  const CLASS = { done: "done", doing: "wip", planned: "planned", na: "na" };
  for (const lang of ["en", "es"]) {
    const rows = text(files, `${lang}/index.html`).match(/<article class="build">[\s\S]*?<\/article>/g);
    BUILDS.forEach((slug, i) => {
      const status = JSON.parse(readFileSync(`${SITE}../hardware/${HARDWARE[slug]}/status.json`, "utf8"));
      const dots = [...rows[i].matchAll(/<li class="(\w+)"><span class="dot" title="([^"]+)" aria-hidden="true"><\/span><span class="visually-hidden">([^<]+)<\/span><\/li>/g)];
      assert.deepEqual(dots.map((d) => d[1]), status.hardware.map((s) => CLASS[s.state]), `${lang} ${slug}`);
      for (const d of dots) assert.equal(d[2], d[3]);
    });
  }
});
test("the donate block and the origin story stay", () => {
  const html = text(files, "es/index.html");
  assert.match(html, /<section class="donate">/);
  assert.match(html, /Catch Catch/);
});
test("home links resolve", () => {
  assert.deepEqual(brokenLinks(files), []);
});
test("the home page names its sister projects beside how it started", () => {
  for (const lang of ["en", "es"]) {
    const html = text(buildInto(), `${lang}/index.html`);
    const row = /<div class="origin-row">([\s\S]*?)<\/aside>/.exec(html);
    assert.ok(row, lang);
    assert.match(row[1], /Catch Catch/);
    for (const r of ["nds-signer", "ur-tones", "seedcraft"])
      assert.match(row[1], new RegExp(`<aside class="siblings"[\\s\\S]*href="https://github.com/ndssigner/${r}" rel="noopener noreferrer"`), `${lang}: ${r}`);
  }
});
