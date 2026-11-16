"""Voice pack tooling: IMA ADPCM, the DSV1 pack layout, the committed header.

Pure Python; needs neither Piper nor numpy.
"""

import json
import math
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "voice"))

import adpcm  # noqa: E402
import make_pack  # noqa: E402


def _vector():
    return json.loads((ROOT / "vectors" / "adpcm-v0.json").read_text())


class AdpcmTest(unittest.TestCase):
    def test_adpcm_matches_vector(self):
        v = _vector()
        data = adpcm.encode(v["pcm"], v["pred"], v["index"])
        self.assertEqual(data.hex(), v["adpcm_hex"])
        self.assertEqual(adpcm.decode(data, v["pred"], v["index"]), v["decoded"])

    def test_vector_is_the_fixed_input(self):
        self.assertEqual(_vector()["pcm"], adpcm.sample_pcm())

    def test_roundtrip_error_small(self):
        # The step size adapts up from index 0; the first 20 samples are the
        # warm-up (a clip starts with 30 ms of silence), so they are skipped.
        pcm = adpcm.sample_pcm()[20:]
        out = adpcm.decode(adpcm.encode(adpcm.sample_pcm()))[20:]
        err = math.sqrt(sum((a - b) ** 2 for a, b in zip(pcm, out)) / len(pcm))
        rms = math.sqrt(sum(a * a for a in pcm) / len(pcm))
        self.assertLess(err / rms, 0.06)


class PackTest(unittest.TestCase):
    def test_pack_layout(self):
        clips = [
            [int(3000 * math.sin(i / 5)) for i in range(160)],
            [],
            [int(2000 * math.sin(i / 3)) for i in range(81)],
        ]
        pack = make_pack.build_pack("es", clips)
        self.assertEqual(pack[:4], b"DSV1")
        self.assertEqual(pack[4:6], b"es")
        (count,) = struct.unpack_from("<H", pack, 6)
        self.assertEqual(count, 3)
        entries = make_pack.parse_pack(pack)
        self.assertEqual(len(entries), 3)
        data_start = 8 + 12 * count
        end = data_start
        for e, clip in zip(entries, clips):
            self.assertEqual(e["offset"], end)
            self.assertEqual(e["bytes"], (len(clip) + 1) // 2)
            self.assertEqual(e["pad"], 0)
            end += e["bytes"]
        self.assertEqual(end, len(pack))
        # The data decodes with the stored state.
        e = entries[0]
        out = adpcm.decode(pack[e["offset"]:e["offset"] + e["bytes"]], e["pred"], e["index"])
        self.assertEqual(len(out), 160)


class HeaderTest(unittest.TestCase):
    def test_committed_header_matches_phrases(self):
        phrases = json.loads((ROOT / "tools" / "voice" / "phrases.json").read_text())
        text = (ROOT / "app" / "voice" / "ds_prompts.h").read_text()
        names = re.findall(r"^\s*(DS_P_[A-Z0-9_]+),?\s*$", text, re.M)
        self.assertEqual(names[-1], "DS_P_COUNT")
        self.assertEqual(names[:-1], ["DS_P_" + k.upper() for k in phrases])
        for k, v in phrases.items():
            self.assertRegex(k, r"^[a-z0-9_]+$")
            self.assertEqual(set(v), {"es", "en"})
        self.assertIn("ds_pack_es", text)
        self.assertIn("ds_pack_en_len", text)


if __name__ == "__main__":
    unittest.main()
