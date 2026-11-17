# Voices of the spoken prompts

The prompt packs (`pack_en.c`, built in, and `packs/es.bin`) are synthesised with
[Piper](https://github.com/OHF-Voice/piper1-gpl) and free voices from the
[rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices) collection.
The texts are in `tools/voice/phrases.json`; the voices, their sources and
SHA-256 are pinned in `tools/voice/voices.json`.

| Language | Voice | Dataset | Licence | Source |
|---|---|---|---|---|
| Spanish | `es_ES-sharvard-medium` (speaker 0) | [Sharvard corpus](https://datashare.ed.ac.uk/handle/10283/574) | CC BY 3.0 | https://huggingface.co/rhasspy/piper-voices/tree/main/es/es_ES/sharvard/medium |
| English | `en_US-ljspeech-medium` | [LJ Speech](https://keithito.com/LJ-Speech-Dataset/) | public domain | https://huggingface.co/rhasspy/piper-voices/tree/main/en/en_US/ljspeech/medium |

Spanish voice: Piper es_ES-sharvard-medium (speaker 0), trained on the Sharvard corpus (Aubanel et al., University of Edinburgh DataShare, https://datashare.ed.ac.uk/handle/10283/574), CC BY 3.0 — http://creativecommons.org/licenses/by/3.0/

The Spanish audio is equalised after synthesis (200 Hz high-pass, a gentle dip
around 400 Hz) and resampled; see `tools/voice/make_pack.py`.

The generated audio is distributed with the project under its licence. Never
the voice of anyone in this project.
