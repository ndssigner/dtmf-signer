"""Voice pack tooling: IMA ADPCM, the DSV2 pack layout and grammar, UF2, the
committed header and Spanish pack.

Pure Python; needs neither Piper nor numpy.
"""

import json
import math
import re
import struct
import sys
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "voice"))

import adpcm  # noqa: E402
import make_pack  # noqa: E402
import make_uf2  # noqa: E402


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


def _phrase_ids():
    return {k: i for i, k in enumerate(make_pack.load_phrases())}


def _grammar(lang):
    g = json.loads((ROOT / "tools" / "voice" / "grammar.json").read_text(encoding="utf-8"))
    return make_pack.resolve_grammar(g[lang], _phrase_ids())


class PackTest(unittest.TestCase):
    def test_pack_v2_layout(self):
        clips = [
            [int(3000 * math.sin(i / 5)) for i in range(160)],
            [],
            [int(2000 * math.sin(i / 3)) for i in range(81)],
        ]
        grammar = make_pack.encode_grammar(_grammar("es"))
        pack = make_pack.build_pack("es", clips, grammar)
        magic, lang, fmt, count, total, crc, goff = struct.unpack_from("<4s2sHHIII", pack, 0)
        self.assertEqual((magic, lang, fmt, count), (b"DSV2", b"es", 2, 3))
        self.assertEqual(total, len(pack))
        self.assertEqual(crc, zlib.crc32(pack[:14] + bytes(4) + pack[18:]))
        self.assertEqual(goff, 22 + 12 * count)
        self.assertEqual(pack[goff:goff + len(grammar)], grammar)
        info = make_pack.parse_pack(pack)
        self.assertTrue(info["crc_ok"])
        end = goff + len(grammar)
        for e, clip in zip(info["entries"], clips):
            self.assertEqual(e["offset"], end)
            self.assertEqual(e["bytes"], (len(clip) + 1) // 2)
            self.assertEqual(e["pad"], 0)
            end += e["bytes"]
        self.assertEqual(end, len(pack))
        e = info["entries"][0]
        out = adpcm.decode(pack[e["offset"]:e["offset"] + e["bytes"]], e["pred"], e["index"])
        self.assertEqual(len(out), 160)
        # A flipped bit anywhere is caught.
        bad = bytearray(pack)
        bad[-1] ^= 1
        self.assertFalse(make_pack.parse_pack(bytes(bad))["crc_ok"])

    def test_grammar_encoding(self):
        g = _grammar("es")
        ids = _phrase_ids()
        blob = make_pack.encode_grammar(g)
        self.assertEqual(len(blob), 200 + 1 + 18 * len(g["scales"]) + 2 + 16 * len(g["irregulars"]))
        back = make_pack.decode_grammar(blob)
        self.assertEqual(back, {"small": g["small"], "scales": [tuple(s) for s in g["scales"]],
                                "irregulars": [tuple(r) for r in g["irregulars"]]})
        self.assertEqual([s[0] for s in back["scales"]], [10 ** 6, 1000, 100, 10])
        self.assertEqual(back["scales"][0][1:], (ids["n_millon"], ids["n_millones"], True))
        self.assertEqual(back["scales"][1][1:], (ids["n_mil"], make_pack.NONE, False))
        self.assertIn((100, ids["n_cien"], 1), back["irregulars"])
        self.assertIn((100, ids["n_ciento"], 2), back["irregulars"])
        self.assertIn((30, ids["n_treinta_y"], 2), back["irregulars"])
        self.assertIn((1, ids["n_un"], 3), back["irregulars"])
        self.assertEqual(back["small"][29], ids["n_veintinueve"])
        self.assertEqual(back["small"][30], make_pack.NONE)

    def test_numbers_reference(self):
        """say_number, the reference of the C algorithm, on the same vectors."""
        ids = _phrase_ids()
        es, en = _grammar("es"), _grammar("en")

        def say(g, n):
            names = {v: k for k, v in ids.items()}
            return [names[c] for c in make_pack.say_number(g, n)]
        self.assertEqual(say(es, 60000), ["n_sesenta", "n_mil"])
        self.assertEqual(say(es, 39000), ["n_treinta_y", "n_nueve", "n_mil"])
        self.assertEqual(say(es, 1001), ["n_mil", "n_uno"])
        self.assertEqual(say(es, 101), ["n_ciento", "n_uno"])
        self.assertEqual(say(es, 1000000), ["n_un", "n_millon"])
        self.assertEqual(say(es, 2500000), ["n_dos", "n_millones", "n_quinientos", "n_mil"])
        self.assertEqual(say(es, 21000), ["n_veintiun", "n_mil"])
        self.assertEqual(say(es, 31000), ["n_treinta_y", "n_un", "n_mil"])
        self.assertEqual(say(es, 101000), ["n_ciento", "n_un", "n_mil"])
        self.assertEqual(say(es, 0), ["n_cero"])
        self.assertEqual(say(en, 1234), ["n_one", "n_thousand", "n_two", "n_hundred", "n_thirty", "n_four"])
        self.assertEqual(say(en, 0), ["n_zero"])

    def test_every_language_says_every_amount_shape(self):
        for lang in ("es", "en"):
            g = _grammar(lang)
            for n in list(range(0, 2000)) + [10 ** k + d for k in range(3, 19) for d in (0, 1, 21, 999)]:
                self.assertTrue(make_pack.say_number(g, n), (lang, n))


class Uf2Test(unittest.TestCase):
    def test_uf2_blocks(self):
        pack = bytes(range(256)) * 3 + b"xyz"
        uf2 = make_uf2.to_uf2(pack, 1)
        self.assertEqual(len(uf2), 4 * 512)
        for n in range(4):
            blk = uf2[n * 512:(n + 1) * 512]
            m0, m1, flags, addr, size, no, total, fam = struct.unpack_from("<8I", blk, 0)
            self.assertEqual((m0, m1), (0x0A324655, 0x9E5D5157))
            self.assertEqual(struct.unpack_from("<I", blk, 508)[0], 0x0AB16F30)
            self.assertTrue(flags & 0x2000)
            self.assertEqual(fam, 0xE48BFF56)
            self.assertEqual(addr, 0x10100000 + 0x80000 + 256 * n)
            self.assertEqual((size, no, total), (256, n, 4))
            self.assertEqual(blk[32:32 + 256], pack[256 * n:256 * (n + 1)].ljust(256, b"\0"))
        self.assertEqual(struct.unpack_from("<I", make_uf2.to_uf2(pack, 0), 12)[0], 0x10100000)

    def test_uf2_refuses_large_pack(self):
        with self.assertRaises(ValueError):
            make_uf2.to_uf2(bytes(0x80001), 0)
        with self.assertRaises(ValueError):
            make_uf2.to_uf2(b"x", 2)


class CommittedPackTest(unittest.TestCase):
    def test_es_bin_matches_tool(self):
        pack = (ROOT / "app" / "voice" / "packs" / "es.bin").read_bytes()
        info = make_pack.parse_pack(pack)
        self.assertEqual(info["lang"], "es")
        self.assertEqual(info["count"], len(make_pack.load_phrases()))
        self.assertEqual(info["total_len"], len(pack))
        self.assertTrue(info["crc_ok"])
        self.assertLessEqual(len(pack), 0x80000)
        self.assertEqual(info["grammar"], make_pack.decode_grammar(make_pack.lang_grammar("es", make_pack.load_phrases())))


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
        self.assertNotIn("ds_pack_es", text)
        self.assertIn("ds_pack_en_len", text)


try:
    import numpy as np
except ImportError:  # the pure-Python suite runs without it
    np = None


@unittest.skipIf(np is None, "needs numpy (the voice venv)")
class NotchTest(unittest.TestCase):
    """The clip pipeline takes the DTMF high group out of the speech, so no
    clip can sound like a key."""

    @staticmethod
    def _gain_db(f):
        t = np.arange(8000) / make_pack.RATE
        x = 0.3 * np.sin(2 * np.pi * f * t)
        y = make_pack.notch_dtmf(x)
        mid = slice(2000, 6000)  # away from the edges
        return 20 * np.log10(np.sqrt((y[mid] ** 2).mean()) / np.sqrt((x[mid] ** 2).mean()))

    def test_high_group_is_notched(self):
        for f in make_pack.DTMF_HIGH:
            self.assertLessEqual(self._gain_db(f), -30, f)

    def test_the_rest_passes(self):
        for f in (500, 2400):
            self.assertAlmostEqual(self._gain_db(f), 0, delta=3, msg=f)

    def test_deterministic(self):
        x = np.sin(np.arange(2000) * 0.37) * 0.2
        self.assertTrue((make_pack.notch_dtmf(x) == make_pack.notch_dtmf(x)).all())


if __name__ == "__main__":
    unittest.main()
