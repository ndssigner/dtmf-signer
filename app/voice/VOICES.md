# Voices of the spoken prompts

The prompt packs (`pack_en.c`, built in, and `packs/es.bin`) are synthesised with
[Piper](https://github.com/OHF-Voice/piper1-gpl) and free voices from the
[rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices) collection.
The texts are in `tools/voice/phrases.json`; the voices, their sources and
SHA-256 are pinned in `tools/voice/voices.json`.

| Language | Voice | Dataset | Licence | Source |
|---|---|---|---|---|
| Spanish | `es_ES-davefx-medium` | [OHF-Voice voice-datasets](https://github.com/OHF-Voice/voice-datasets) | CC0 | https://huggingface.co/rhasspy/piper-voices/tree/main/es/es_ES/davefx/medium |
| English | `en_US-ljspeech-medium` | [LJ Speech](https://keithito.com/LJ-Speech-Dataset/) | public domain | https://huggingface.co/rhasspy/piper-voices/tree/main/en/en_US/ljspeech/medium |

The generated audio is distributed with the project under its licence. No
recording of any person's own voice is used.
