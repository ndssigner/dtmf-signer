# Voice packs

Generates the spoken prompts of the telephone build: Piper text-to-speech,
resampled to 8 kHz and encoded as Speex narrowband (fixed point, quality 2:
5.95 kbit/s, under 1 KB per second), in the `DSV3` pack format described at
the top of `make_pack.py`. The encoder is `spxtool` (`spxtool.c`), built
from the firmware's own vendored libspeex (`third_party/speex`), so what is
encoded is decoded by the same code on every build; `make_pack.py` builds it
itself (`cmake --build build/voice-tools --target spxtool`).

English is built into the firmware (`app/voice/pack_en.c`); every other
language is a separate pack (`app/voice/packs/<lang>.bin`) written to the
voice flash region and found at boot. Each pack carries its number grammar
(`grammar.json`: whole clips for the small numbers a language has, scale
words, irregular forms), so a new language needs no firmware change.

The generated files are committed (`app/voice/`), so building the firmware
needs none of this. To regenerate them:

```sh
python3 -m venv ~/.cache/dtmf-signer/venv        # any location outside the repository
~/.cache/dtmf-signer/venv/bin/pip install -r tools/voice/requirements.txt
V=~/.cache/dtmf-signer/venv/bin/python
$V tools/voice/make_pack.py --lang en --out-c app/voice/pack_en.c
$V tools/voice/make_pack.py --lang es --out-bin app/voice/packs/es.bin
python3 tools/voice/make_pack.py --header app/voice/ds_prompts.h
```

The Spanish voice is Piper `es_ES-sharvard-medium`, speaker 0 (Sharvard corpus,
CC BY 3.0, credited in `app/voice/VOICES.md` and `THIRD_PARTY.md`); it is bassy,
so its audio gets a high-pass and a gentle dip before the DTMF notch
(`"eq"` in `voices.json`). The two voice models are downloaded once into `~/.cache/dtmf-signer/voices/`
(or `$DTMF_VOICE_CACHE`) and checked against the SHA-256 in `voices.json`.
Licences are in `app/voice/VOICES.md`.

The voice region is 1 MB (flash offset 0x100000); packs sit back to back in it,
each starting on a 4 KB boundary, and the tools refuse a pack larger than the
region. On the Pico, `make_uf2.py` wraps a pack for drag-and-drop:

```sh
python3 tools/voice/make_uf2.py app/voice/packs/es.bin --offset 0 es.uf2
```

Adding or changing a phrase: edit `phrases.json` (ASCII lowercase IDs; the
C names are `DS_P_` plus the ID in capitals) and regenerate the header and
every pack (a pack with another phrase count is ignored by the firmware). A
word a language lacks has an empty string and is never used there.

`python3 -m unittest discover -s tests` checks the pack layout and grammar,
the UF2 wrapper, the committed Spanish pack and that the committed header
matches `phrases.json`; it needs nothing beyond the standard library (the
`spxtool` check runs once the host build has it). The C tests check that no
clip, decoded as the signer plays it, is heard as a DTMF key.
