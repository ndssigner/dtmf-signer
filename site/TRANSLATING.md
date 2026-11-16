# Translating the website

The website (`site/`) can be in any language. The technical documents of
the repository stay in English and Spanish; the website is for showing
what DTMF-Signer is, so it is where more languages help most.

Everything you need is two kinds of files, and no programming:

- `site/i18n/<code>.json`: the short texts (menus, titles, labels), one
  key per text.
- `site/content/<code>/*.md`: the longer texts of each page, in plain
  Markdown (titles, paragraphs, lists, bold, italic, links, code).

## Adding a language

1. Pick the language's code (`fr`, `pt`, `de`…).
2. Copy `site/i18n/en.json` to `site/i18n/<code>.json` and translate the
   texts on the right. Keep the keys on the left as they are. Start with
   `lang.name`, the language's own name: the checks require it even in a
   language half done.
3. Copy `site/content/en/` to `site/content/<code>/` and translate each
   file.
4. Add the language to `site/config.json`, under `languages`:
   `{ "code": "<code>", "complete": false }`.
5. Check it: `node site/check-i18n.mjs` (it lists what is missing or wrong),
   then build it: `node site/build.mjs`, and open `build/site/index.html`
   through a local web server, for example
   `python3 -m http.server -d build/site`.
6. Open a pull request.

You can send a language half done: whatever is missing is shown in English,
with a note inviting readers to help. Once everything is translated, set
`"complete": true`, and the checks will keep it complete.

## What not to translate

- Identifiers such as `PROFILE-CABLE-QR`, file names and commands.
- Placeholders in braces, such as `{commit}`: keep them exactly, anywhere
  in the sentence.
- The parts lists: they come from `hardware/*/bom.csv` and stay in English;
  only their column titles are translated.
- In the simulator's texts (`sim.*`, `bench.*`, `wallet.*`, `mic.*`,
  `status.*`), the names the firmware shows on its screen (“Load seed”,
  “Sign PSBT”): its screen is in English, so keep them as they are.
  `{seconds}` is a placeholder. Keep these texts short: the simulator
  explains with icons and tooltips, not paragraphs.
- “This computer” is the computer the simulator runs on (as in “the
  microphone of this computer”): keep that wording, not “your computer”.

Translate the meaning, not word for word, and keep the tone plain.
