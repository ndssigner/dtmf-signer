English · [Español](es/voice-packs.md)

# A voice pack in your language

The Phone Exchange (PEX) speaks: it reads every output of a transaction
aloud before you sign. It speaks English, built into the firmware, and
Spanish, as a pack. **We are looking for people to make it speak their own
language.** Brazilian Portuguese, German and French would reach many
people, but any language is welcome.

A pack is not just a translation. What the signer says is what you check
before signing, so every phrase and every number must sound right to
someone who speaks the language. That is why a pack is best made, or at
least heard and approved, by a native speaker: you.

Open an issue first ("A voice pack: <language>") to say you are on it, so
two people do not do the same work.

## How a pack is made

Nobody records anything. The phrases are written in
[`tools/voice/phrases.json`](../tools/voice/phrases.json), spoken by a
free [Piper](https://github.com/OHF-Voice/piper1-gpl) voice, filtered so
that no phrase sounds like a telephone key, and compressed into a pack of
about 300 KB with [`tools/voice/make_pack.py`](../tools/voice/make_pack.py).
Three files say everything about a language:

| File | What you add |
| --- | --- |
| [`tools/voice/phrases.json`](../tools/voice/phrases.json) | Your language's text for every phrase, and its number words |
| [`tools/voice/grammar.json`](../tools/voice/grammar.json) | How your language builds numbers from those words |
| [`tools/voice/voices.json`](../tools/voice/voices.json) | The Piper voice that speaks it, pinned by its SHA-256 |

Your language takes its two-letter code (`pt`, `de`, `fr`...) everywhere.

## 1. The voice

Pick a voice for your language from
[rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices). It
must:

- be under a licence that lets the audio be redistributed with the project
  (public domain, CC0 or CC BY, as English and Spanish are), with its
  dataset known;
- never be the voice of a real person who has not agreed to it, nor of
  anyone in this project;
- sound clear at 8 kHz, the quality of a telephone line: try a few phrases
  first (step 4).

Add it to `voices.json` as the others are: its name, the speaker if the
model has several, the dataset, the licence, and each file's URL at a fixed
revision with its SHA-256. If the voice is bassy, `"eq": true` applies the
same correction the Spanish voice uses (a high-pass and a gentle dip).

### No free voice in your language: your own

If Piper has no voice for your language under a free licence, you can
record the phrases yourself and share the recordings under a free licence
(CC0 or CC BY). In `voices.json`, instead of the files, give the folder:

```json
"pt": { "name": "recorded: <your name or pseudonym>", "recorded": "tools/voice/recorded/pt", "license": "CC BY 4.0" }
```

with one WAV per phrase, named after it (`greeting.wav`, `n_um.wav`...):
16-bit, mono, 8 kHz or more. `make_pack.py` gives them the same treatment
as a synthesised voice (resampled, `"eq"` if you set it, the DTMF notch,
the silence trimmed at both ends) and lists any phrase still missing.

- **Record in a quiet room,** with the same microphone, distance and level
  for every phrase, so they sound like one voice on one day. Leave a moment
  of silence before and after each.
- **It is about six minutes of speech** in some 230 phrases. And it is a
  commitment: a phrase added or changed later must be recorded again by
  you, or your language falls behind.
- **Your voice becomes public, for good,** tied to this project and free
  for anyone to reuse. That is your choice; make it knowingly. If you use
  a pseudonym elsewhere, a recording of your voice can undo it.

## 2. The phrases

Add your language's text to every entry of `phrases.json`, next to `en` and
`es`, and to its `long` version where there is one (the help, said once if
the user hesitates). Keep the meaning, the order of the options and the
digits ("1, load the seed." keeps its "1,"). Keep it short: it is heard
over a telephone, and people act on it.

**Write it as it must sound.** Piper reads spelling, and it gets foreign
words, acronyms and Bitcoin terms wrong. Spell them the way a speaker of
your language says them. The Spanish pack does:

| Says | Written in `phrases.json` |
| --- | --- |
| DTMF signer | `de te eme efe sáiner` |
| testnet | `téstnet` |
| mainnet | `méinnet` |
| wallet | `guálet` |

Listen to each phrase (step 4) and respell until it sounds right; accents,
hyphens, commas and full stops change how Piper says it.

**The number words.** Each language has its own: the Spanish ones are
`n_cero`, `n_uno`..., empty in English; the English ones are `n_zero`,
`n_one`..., empty in Spanish. Add yours as new entries (`n_` and the word,
ASCII), with text only in your language: whole words for every number your
language says as one word, and the pieces it builds the others from.

Every phrase that is not a number word must have text in your language:
`make_pack.py` refuses the pack and lists the ones still missing.

## 3. The numbers

Amounts and addresses are read aloud, so the numbers matter most. In
`grammar.json`, your language says how to build any number from its number
words:

- `small`: the word for each number from 0 to 99 that has its own (in
  German, every number up to 99 can be one, `einundzwanzig` included);
- `scales`: hundred, thousand, million..., with their singular and plural
  and whether "one" is said before them;
- `irregulars`: forms that change with what follows (the Spanish `treinta`
  alone, `treinta y` before a unit).

Look at `es` and `en` there, and at `say_number()` in `make_pack.py`, which
is the reference the firmware follows. The tests say every number from 0 to
2000, and much larger ones, through your grammar.

## 4. Make it and listen

```bash
python3 -m venv ~/.cache/dtmf-signer/venv
~/.cache/dtmf-signer/venv/bin/pip install -r tools/voice/requirements.txt
~/.cache/dtmf-signer/venv/bin/python tools/voice/make_pack.py --lang pt \
    --out-bin app/voice/packs/pt.bin --wav-dir /tmp/pt-wav
```

`--wav-dir` writes every phrase as a WAV, as the signer will play it: listen
to all of them, the numbers above all. Then hear the pack in context: in
the website's simulator built locally, or on a Pico with
[`tools/install_pex.py`](../tools/install_pex.py).

New number words change the firmware's prompt table
(`python3 tools/voice/make_pack.py --header app/voice/ds_prompts.h --names
app/voice/ds_prompt_names.c`), so your pack ships with the next firmware
version, and the other packs are made again with it: say so in the pull
request, and the maintainers will do it.

## 5. Check and send

- `python3 -m unittest discover -s tests` and the C tests (`ctest`, see
  [CONTRIBUTING](../CONTRIBUTING.md)) must pass. Add your pack to the
  talk-off test, which plays every phrase and checks that none sounds like a
  key: see how `es.bin` is embedded in `tests/c/CMakeLists.txt` and used in
  `test_no_prompt_makes_a_key_at_any_alignment` (`tests/c/test_voice.c`).
- Add it to the website: in [`site/config.json`](../site/config.json),
  under the PEX's `simulator`, `packs` (`"pt": "app/voice/packs/pt.bin"`),
  so the simulator speaks it and the downloads offer it.
- Credit the voice in [`app/voice/VOICES.md`](../app/voice/VOICES.md) and
  [`THIRD_PARTY.md`](../THIRD_PARTY.md).
- In the pull request, say who has listened to the whole pack, and that the
  numbers sound right. A pack nobody has heard is not published.
