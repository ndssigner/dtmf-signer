"""Tests of `dtmf-signer-host tones`: WAV recordings of a seed or a PSBT as
ur-tones tones, read back by `keys` and `sign`."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "third_party" / "ur-tones" / "reference" / "python"))

import ur_tones  # noqa: E402

HOST = ROOT / "build" / "ports" / "host" / "dtmf-signer-host"
ZERO16 = "00" * 16
ZERO_FP = "73c5da0a"
VECTOR = next(v for v in json.loads((ROOT / "vectors" / "psbt-v0.json").read_text())["vectors"]
              if v["name"] == "one input with change")


def run(args, stdin="yes\n"):
    p = subprocess.run([str(HOST), *args], input=stdin, capture_output=True,
                       text=True, timeout=120)
    return p.returncode, p.stdout, p.stderr


class HostTonesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def psbt_file(self):
        p = self.dir / "in.psbt"
        p.write_text(VECTOR["psbt"])
        return str(p)

    def fingerprint(self, wav, *extra):
        code, out, err = run(["keys", "--wav", wav, *extra])
        self.assertEqual(code, 0, err)
        return dict(l.split(" ", 1) for l in out.splitlines())["seed_fingerprint"]

    def test_seed_wav_roundtrip(self):
        wav = str(self.dir / "seed.wav")
        code, _, err = run(["tones", "seed", ZERO16, "--pin", "TEST", "--out", wav])
        self.assertEqual(code, 0, err)
        self.assertTrue(Path(wav).read_bytes().startswith(b"RIFF"))
        self.assertEqual(self.fingerprint(wav, "--pin", "TEST"), ZERO_FP)

    def test_seed_wav_keypad_and_options(self):
        for extra in (["--keypad"], ["--rate", "44100"], ["--timing", "air"],
                      ["--keypad", "--pin", "TEST", "--rate", "44100", "--timing", "air"]):
            with self.subTest(extra=extra):
                wav = str(self.dir / "s.wav")
                code, _, err = run(["tones", "seed", ZERO16, *extra, "--out", wav])
                self.assertEqual(code, 0, err)
                pin = ["--pin", "TEST"] if "--pin" in extra else []
                self.assertEqual(self.fingerprint(wav, *pin), ZERO_FP)

    def test_keypad_air_timing(self):
        # Keypad tones always go at 80/80/600 ms: the same WAV as --timing air.
        a, b = str(self.dir / "k1.wav"), str(self.dir / "k2.wav")
        code, _, err = run(["tones", "seed", ZERO16, "--keypad", "--out", a])
        self.assertEqual(code, 0, err)
        code, _, err = run(["tones", "seed", ZERO16, "--keypad", "--timing", "air", "--out", b])
        self.assertEqual(code, 0, err)
        self.assertEqual(Path(a).read_bytes(), Path(b).read_bytes())
        cable = str(self.dir / "c.wav")
        code, _, err = run(["tones", "seed", ZERO16, "--timing", "cable", "--out", cable])
        self.assertEqual(code, 0, err)
        # 16 kHz, 16-bit mono: at least 80 + 80 ms for every keypad tone.
        tones = len(ur_tones.seed_to_keypad(bytes(16)))
        frames = (len(Path(a).read_bytes()) - 44) // 2
        self.assertGreaterEqual(frames, tones * (80 + 80) * 16)

    def test_psbt_wav_roundtrip(self):
        wav = str(self.dir / "psbt.wav")
        code, _, err = run(["tones", "psbt", self.psbt_file(), "--out", wav])
        self.assertEqual(code, 0, err)
        seed = self.seed_args()
        c1, o1, e1 = run(["sign", *seed, "--psbt", self.psbt_file()])
        c2, o2, e2 = run(["sign", *seed, "--psbt", wav])
        self.assertEqual(c1, 0, e1)
        self.assertEqual(c2, 0, e2)
        self.assertEqual(o1, o2)

    def seed_args(self):
        return ["--ur", ur_tones.seed_to_ur(bytes.fromhex(VECTOR["entropy"]))]

    def test_psbt_wav_options(self):
        seed = self.seed_args()
        _, expected, _ = run(["sign", *seed, "--psbt", self.psbt_file()])
        for extra in (["--rate", "44100"], ["--timing", "air"]):
            with self.subTest(extra=extra):
                wav = str(self.dir / "p.wav")
                code, _, err = run(["tones", "psbt", self.psbt_file(), *extra, "--out", wav])
                self.assertEqual(code, 0, err)
                code, out, err = run(["sign", *seed, "--psbt", wav])
                self.assertEqual(code, 0, err)
                self.assertEqual(out, expected)

    def test_sign_bad_wav(self):
        bad = self.dir / "bad.wav"
        bad.write_bytes(b"RIFF\x00\x00\x00\x00WAVEjunk")
        code, out, err = run(["sign", *self.seed_args(), "--psbt", str(bad)])
        self.assertEqual(code, 1)
        self.assertEqual(out, "")

    def test_sign_wav_of_a_seed_is_not_a_psbt(self):
        wav = str(self.dir / "seed.wav")
        run(["tones", "seed", ZERO16, "--out", wav])
        code, out, _ = run(["sign", *self.seed_args(), "--psbt", wav])
        self.assertEqual(code, 1)
        self.assertEqual(out, "")

    def test_tones_usage(self):
        wav = str(self.dir / "x.wav")
        cases = [
            ["tones", "seed", ZERO16],                                  # no --out
            ["tones", "psbt", self.psbt_file()],
            ["tones"],
            ["tones", "other", ZERO16, "--out", wav],
            ["tones", "seed", ZERO16, "--rate", "8000", "--out", wav],
            ["tones", "seed", ZERO16, "--timing", "fast", "--out", wav],
            ["tones", "seed", ZERO16, "--out", wav, "--out", wav],
            ["tones", "psbt", self.psbt_file(), "--keypad", "--out", wav],
            ["tones", "seed", ZERO16, "--keypad", "--timing", "cable", "--out", wav],
            ["tones", "psbt", self.psbt_file(), "--pin", "TEST", "--out", wav],
        ]
        for args in cases:
            with self.subTest(args=args):
                code, _, _ = run(args)
                self.assertEqual(code, 2)
        self.assertFalse(Path(wav).exists())

    def test_tones_bad_input(self):
        wav = str(self.dir / "x.wav")
        for args in (["tones", "seed", "zz", "--out", wav],
                     ["tones", "seed", "00" * 15, "--out", wav],
                     ["tones", "seed", ZERO16, "--pin", "no pin!", "--out", wav],
                     ["tones", "psbt", str(self.dir / "missing"), "--out", wav]):
            with self.subTest(args=args):
                code, _, err = run(args)
                self.assertEqual(code, 1, err)
        self.assertFalse(Path(wav).exists())


if __name__ == "__main__":
    unittest.main()
