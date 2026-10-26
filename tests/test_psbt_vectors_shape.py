"""Checks the structure of vectors/psbt-v0.json without embit: fields, types,
base64 PSBTs with the magic prefix, and error names that exist in core/ds_core.h."""

import base64
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = json.loads((ROOT / "vectors" / "psbt-v0.json").read_text())
ERRORS = set(re.findall(r"#define\s+(DS_ERR_\w+)", (ROOT / "core" / "ds_core.h").read_text()))
HEX = re.compile(r"^(?:[0-9a-f]{2})*$")


def is_int(x):
    return isinstance(x, int) and not isinstance(x, bool)


class PsbtVectorsShapeTest(unittest.TestCase):
    def test_header(self):
        self.assertEqual(DATA["format"], "dtmf-signer-psbt")
        self.assertEqual(DATA["version"], 0)
        self.assertIsInstance(DATA["note"], str)
        self.assertTrue(DATA["vectors"])

    def test_names_are_unique(self):
        names = [v["name"] for v in DATA["vectors"]]
        self.assertEqual(len(names), len(set(names)))

    def test_every_vector(self):
        kinds = set()
        for v in DATA["vectors"]:
            with self.subTest(v["name"]):
                self.check_vector(v)
                kinds.add(v["expect"])
        self.assertEqual(kinds, {"sign", "reject"})

    def check_vector(self, v):
        self.assertIsInstance(v["name"], str)
        self.assertRegex(v["entropy"], r"^[0-9a-f]{32}$")
        self.assertIsInstance(v["passphrase"], str)
        self.assertIsInstance(v["testnet"], bool)
        raw = base64.b64decode(v["psbt"], validate=True)
        self.assertTrue(raw.startswith(bytes.fromhex("70736274ff")))
        self.assertIn(v["expect"], ("sign", "reject"))
        if v["expect"] == "reject":
            self.assertEqual(set(v), {"name", "entropy", "passphrase", "testnet", "psbt", "expect", "error"})
            self.assertIn(v["error"], ERRORS)
            return
        self.assertEqual(set(v), {"name", "entropy", "passphrase", "testnet", "psbt", "expect",
                                  "fee", "fee_warning", "outputs", "signatures"})
        self.assertTrue(is_int(v["fee"]) and v["fee"] >= 0)
        self.assertIsInstance(v["fee_warning"], bool)
        self.assertTrue(v["outputs"])
        for o in v["outputs"]:
            self.assertTrue(is_int(o["amount"]) and o["amount"] >= 0)
            self.assertIn(o["kind"], ("external", "change", "own"))
            self.assertEqual(("address" in o) + ("script_hex" in o), 1)
            self.assertEqual(set(o) - {"address", "script_hex"}, {"amount", "kind"})
            if "address" in o:
                self.assertIsInstance(o["address"], str)
                self.assertTrue(o["address"])
            else:
                self.assertRegex(o["script_hex"], HEX)
        self.assertTrue(v["signatures"])
        self.assertTrue(any(s is not None for s in v["signatures"]))
        for s in v["signatures"]:
            if s is None:
                continue
            self.assertEqual(set(s), {"pubkey", "sig"})
            self.assertRegex(s["pubkey"], r"^0[23][0-9a-f]{64}$")
            self.assertRegex(s["sig"], HEX)
            sig = bytes.fromhex(s["sig"])
            self.assertEqual(sig[0], 0x30)
            self.assertEqual(sig[1], len(sig) - 3)
            self.assertEqual(sig[-1], 0x01)


if __name__ == "__main__":
    unittest.main()
