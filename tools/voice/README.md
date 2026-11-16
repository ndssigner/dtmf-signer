# Voice packs

Generates the spoken prompts of the telephone build: Piper text-to-speech,
resampled to 8 kHz and encoded as IMA ADPCM (about 4 KB per second), in the
`DSV1` pack format described at the top of `make_pack.py`.

The generated files are committed (`app/voice/`), so building the firmware
needs none of this. To regenerate them:

```sh
python3 -m venv ~/.cache/dtmf-signer/venv        # any location outside the repository
~/.cache/dtmf-signer/venv/bin/pip install -r tools/voice/requirements.txt
V=~/.cache/dtmf-signer/venv/bin/python
$V tools/voice/make_pack.py --lang es --out-c app/voice/pack_es.c
$V tools/voice/make_pack.py --lang en --out-c app/voice/pack_en.c
python3 tools/voice/make_pack.py --header app/voice/ds_prompts.h
python3 tools/voice/make_pack.py --adpcm-vector vectors/adpcm-v0.json
```

The two voice models are downloaded once into `~/.cache/dtmf-signer/voices/`
(or `$DTMF_VOICE_CACHE`) and checked against the SHA-256 in `voices.json`.
Licences are in `app/voice/VOICES.md`.

Adding or changing a phrase: edit `phrases.json` (ASCII lowercase IDs; the
C names are `DS_P_` plus the ID in capitals) and regenerate the header and
both packs. A word a language lacks has an empty string and is never used
there.

`python3 -m unittest discover -s tests` checks the ADPCM codec, the pack
layout and that the committed header matches `phrases.json`; it needs
nothing beyond the standard library.
