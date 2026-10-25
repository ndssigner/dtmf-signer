"""Checks vectors/keys-v0.json's seed fingerprints against ur-tones' own
JavaScript implementation, independent of the C core."""

import json
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VECTORS = json.loads((ROOT / "vectors" / "keys-v0.json").read_text())["vectors"]


@unittest.skipUnless(shutil.which("node"), "node is not installed: cannot re-check the fingerprints")
class FingerprintVectorsTest(unittest.TestCase):
    def test_seed_fingerprints_match_the_javascript_implementation(self):
        out = subprocess.run(
            ["node", str(ROOT / "tests" / "check_fingerprints.mjs")] + [v["entropy"] for v in VECTORS],
            capture_output=True, text=True, timeout=60, check=True,
        ).stdout.split()
        self.assertEqual(out, [v["seed_fingerprint"] for v in VECTORS])


if __name__ == "__main__":
    unittest.main()
