"""The website's donation addresses (site/config.json) are the README's."""
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class SiteDonate(unittest.TestCase):
    def test_same_addresses_as_the_readmes(self):
        donate = json.loads((ROOT / "site" / "config.json").read_text(encoding="utf-8"))["donate"]
        for readme in ("README.md", "README.es.md"):
            text = (ROOT / readme).read_text(encoding="utf-8")
            for kind in ("bitcoin", "lightning"):
                self.assertIn(donate[kind], text, f"{readme}: {kind}")


if __name__ == "__main__":
    unittest.main()
