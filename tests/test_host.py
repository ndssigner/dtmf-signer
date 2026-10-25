"""Tests of the computer build (ports/host): seeds from URs, keypad tones and
WAV recordings to fingerprints and the BIP-84 xpub."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "build" / "ports" / "host" / "dtmf-signer-host"
UT = ROOT / "third_party" / "ur-tones"
sys.path.insert(0, str(UT / "reference" / "python"))

import ur_tones  # noqa: E402

KEYS = json.loads((ROOT / "vectors" / "keys-v0.json").read_text())["vectors"]
UT_VECTORS = json.loads((UT / "vectors" / "ur-tones-v0.json").read_text())["vectors"]
SEEDS = [v for v in UT_VECTORS if v.get("entropy")]
BIP84 = KEYS[0]


def run(*args):
    """Runs the tool; returns (exit code, {field: value}, stderr)."""
    p = subprocess.run([str(HOST), *args], capture_output=True, text=True, timeout=60)
    fields = dict(line.split(" ", 1) for line in p.stdout.splitlines())
    return p.returncode, fields, p.stderr


def expected_fingerprint(seed):
    """The seed fingerprint vectors/keys-v0.json holds for this ur-tones seed."""
    return next(k["seed_fingerprint"] for k in KEYS if k["entropy"] == seed["entropy"])


def keys_args(seed, source, value):
    args = ["keys", source, value]
    if seed["pin"]:
        args += ["--pin", seed["pin"]]
    return args


class HostTest(unittest.TestCase):
    def test_host_vectors(self):
        self.assertEqual(len(SEEDS), 10)
        for seed in SEEDS:
            with self.subTest(seed["name"], pin=seed["pin"]):
                code, out, err = run(*keys_args(seed, "--ur", seed["ur"]))
                self.assertEqual(code, 0, err)
                self.assertEqual(out["seed_fingerprint"], expected_fingerprint(seed))
                self.assertEqual(out["wallet_fingerprint"], expected_fingerprint(seed))
                self.assertTrue(out["xpub"].startswith("xpub"))
                self.assertTrue(out["descriptor"].startswith("wpkh([%s/84'/0'/0']xpub" % out["wallet_fingerprint"]))
        # keypad tones of the BIP-84 test seed (all-zero entropy)
        keypad = ur_tones.seed_to_keypad(bytes.fromhex(BIP84["entropy"]))
        code, out, err = run("keys", "--keypad", keypad)
        self.assertEqual(code, 0, err)
        self.assertEqual(out["seed_fingerprint"], BIP84["seed_fingerprint"])
        self.assertEqual(out["xpub"], BIP84["xpub"])
        self.assertIn("#", out["descriptor"])

    def test_host_keypad_vectors(self):
        for seed in SEEDS:
            with self.subTest(seed["name"], pin=seed["pin"]):
                code, out, err = run(*keys_args(seed, "--keypad", seed["keypad"]))
                self.assertEqual(code, 0, err)
                self.assertEqual(out["seed_fingerprint"], expected_fingerprint(seed))

    def test_host_frame_tones(self):
        for seed in SEEDS:
            with self.subTest(seed["name"], pin=seed["pin"]):
                frame = ur_tones.ur_to_frame(seed["ur"])
                code, out, err = run(*keys_args(seed, "--frame", frame))
                self.assertEqual(code, 0, err)
                self.assertEqual(out["seed_fingerprint"], expected_fingerprint(seed))

    def test_host_wrong_pin_changes_fingerprint(self):
        seed = next(s for s in SEEDS if s["pin"] == "1234")
        code, out, err = run("keys", "--ur", seed["ur"], "--pin", "1235")
        self.assertEqual(code, 0, err)
        self.assertNotEqual(out["seed_fingerprint"], expected_fingerprint(seed))

    def test_host_passphrase_and_testnet(self):
        zero = ur_tones.seed_to_ur(bytes(16))
        code, plain, err = run("keys", "--ur", zero)
        self.assertEqual(code, 0, err)
        code, out, err = run("keys", "--ur", zero, "--passphrase", "TREZOR")
        self.assertEqual(code, 0, err)
        self.assertEqual(out["seed_fingerprint"], plain["seed_fingerprint"])
        self.assertNotEqual(out["wallet_fingerprint"], plain["wallet_fingerprint"])
        code, out, err = run("keys", "--ur", zero, "--testnet")
        self.assertEqual(code, 0, err)
        self.assertTrue(out["xpub"].startswith("tpub"))
        self.assertIn("/84'/1'/0'", out["descriptor"])

    def test_host_wav(self):
        seed = next(s for s in SEEDS if not s["pin"] and len(s["entropy"]) == 32)
        frame = ur_tones.ur_to_frame(seed["ur"])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "seed.wav"
            ur_tones.write_wav(path, ur_tones.render([frame]), 8000)
            code, out, err = run("keys", "--wav", str(path))
            self.assertEqual(code, 0, err)
            self.assertEqual(out["seed_fingerprint"], expected_fingerprint(seed))
            # keypad mode in a recording
            keypad = ur_tones.seed_to_keypad(bytes.fromhex(seed["entropy"]))
            ur_tones.write_wav(path, ur_tones.render([keypad]), 8000)
            code, out, err = run("keys", "--wav", str(path))
            self.assertEqual(code, 0, err)
            self.assertEqual(out["seed_fingerprint"], expected_fingerprint(seed))
            # silence holds no tones
            ur_tones.write_wav(path, [0.0] * 8000, 8000)
            code, out, err = run("keys", "--wav", str(path))
            self.assertEqual(code, 1)
            self.assertEqual(out, {})
            self.assertTrue(err)

    def test_host_usage(self):
        self.assertEqual(run()[0], 2)
        self.assertEqual(run("keys")[0], 2)
        self.assertEqual(run("keys", "--ur")[0], 2)
        self.assertEqual(run("keys", "--ur", "x", "--keypad", "*1#")[0], 2)
        self.assertEqual(run("keys", "--bogus", "x")[0], 2)
        self.assertEqual(run("sign")[0], 2)

    def test_host_errors(self):
        code, out, err = run("keys", "--ur", "ur:crypto-seed/notaseed")
        self.assertEqual(code, 1)
        self.assertEqual(out, {})
        self.assertTrue(err)
        code, out, err = run("keys", "--wav", "/nonexistent/file.wav")
        self.assertEqual(code, 1)
        self.assertTrue(err)
        seed = SEEDS[0]
        code, out, err = run("keys", "--ur", seed["ur"], "--passphrase", "café")
        self.assertEqual(code, 1)
        self.assertEqual(out, {})

    def test_host_24_words(self):
        seeds = [s for s in SEEDS if s["name"] == "SeedSigner SeedQR test vector 1"]
        self.assertEqual(len(seeds), 2)
        for seed in seeds:
            with self.subTest(pin=seed["pin"]):
                self.assertEqual(len(seed["entropy"]), 64)
                code, out, err = run(*keys_args(seed, "--ur", seed["ur"]))
                self.assertEqual(code, 0, err)
                self.assertEqual(out["seed_fingerprint"], expected_fingerprint(seed))


if __name__ == "__main__":
    unittest.main()
