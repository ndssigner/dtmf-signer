"""The breadboard's parts list (hardware/breadboard/bom.csv) is the one
source for the site and the docs: its resistors and capacitors must be the
ones the layout uses (VALUES in test_check_diy.py, read from the .diy)."""
import csv
import re
import unittest
from collections import Counter
from pathlib import Path

from test_check_diy import PEX_VALUES, VALUES

ROOT = Path(__file__).resolve().parent.parent
BOM = ROOT / "hardware" / "breadboard" / "bom.csv"
HEADER = ["part", "qty", "value", "notes", "price_usd"]


def short(value: str) -> str:
    """DIYLC's "22.0K", "1.0uF", "10.0nF" -> the list's "22k", "1µF", "10nF"."""
    m = re.fullmatch(r"(\d+(?:\.\d+)?)(K|uF|nF)", value)
    number = m.group(1).removesuffix(".0")
    return number + {"K": "k", "uF": "µF", "nF": "nF"}[m.group(2)]


def rows():
    with open(BOM, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        return reader.fieldnames, list(reader)


class Bom(unittest.TestCase):
    def test_header(self):
        self.assertEqual(rows()[0], HEADER)

    def test_passives_match_the_layout(self):
        listed = Counter()
        for r in rows()[1]:
            if re.fullmatch(r"\d+(\.\d+)?(k|µF|nF)", r["value"]):
                listed[r["value"]] += int(r["qty"])
        self.assertEqual(listed, Counter(short(v) for v in VALUES.values()))

    def test_quantities(self):
        for r in rows()[1]:
            self.assertRegex(r["qty"], r"^[1-9]\d*$", r["part"])

    def test_price_ranges(self):
        for r in rows()[1]:
            if r["price_usd"]:
                m = re.fullmatch(r"(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)", r["price_usd"])
                self.assertIsNotNone(m, r["part"])
                self.assertLessEqual(float(m.group(1)), float(m.group(2)), r["part"])


class PexBom(unittest.TestCase):
    """The PEX's list (hardware/phone/bom.csv) against its layout (PEX_VALUES)."""

    def test_passives_match_the_layout(self):
        with open(ROOT / "hardware" / "phone" / "bom.csv", newline="", encoding="utf-8") as f:
            listed = Counter()
            for r in csv.DictReader(f):
                if re.fullmatch(r"\d+(\.\d+)?(Ω|k|µF|nF)", r["value"]):
                    listed[r["value"]] += int(r["qty"])

        def pex_short(value):
            return value.removesuffix(".0R") + "Ω" if value.endswith("R") else short(value)
        self.assertEqual(listed, Counter(pex_short(v) for v in PEX_VALUES.values()))


if __name__ == "__main__":
    unittest.main()
