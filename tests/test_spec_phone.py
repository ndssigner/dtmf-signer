"""The phone-exchange build ("Centralita") is described in the spec and the catalog."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

CODES = "`TONES-SW`, `IN-PHONELINE`, `OUT-PHONELINE`, `VERIFY-VOICE`, `BTC-INPUT-AMOUNTS`."
CAPABILITIES = {"docs/catalog.md": "Capabilities", "docs/es/catalogo.md": "Capacidades"}


def section(path, number):
    """The text of the '## N.' or '### N.M' section, up to the next heading of the same or higher level."""
    text = (ROOT / path).read_text(encoding="utf-8")
    level = "#" * (number.count(".") + 2 if "." in number else 2)
    pattern = rf"^{level} {re.escape(number)}[ .].*?(?=^#{{1,{len(level)}}} |\Z)"
    match = re.search(pattern, text, re.S | re.M)
    assert match, f"{path}: no section {number}"
    return match.group(0)


class PhoneDocsTest(unittest.TestCase):
    def test_phone_profile_in_catalog(self):
        for path, label in CAPABILITIES.items():
            text = (ROOT / path).read_text(encoding="utf-8")
            start = text.index("### `PROFILE-PHONE`")
            self.assertIn(f"- **{label}:** {CODES}", text[start:], path)
            for code in ("IN-PHONELINE", "OUT-PHONELINE", "VERIFY-VOICE"):
                self.assertIn(f"### `{code}`", text, path)

    def test_spec_phone_exception(self):
        self.assertIn("telephone line", section("SPEC.md", "4.1"))
        self.assertIn("hook", section("SPEC.md", "7"))
        self.assertIn("línea telefónica", section("docs/es/especificacion.md", "4.1"))
        self.assertIn("gancho", section("docs/es/especificacion.md", "7"))

    def test_minimum_hardware_names_telephone_line(self):
        for path, word in (("SPEC.md", "telephone line"), ("docs/es/especificacion.md", "línea telefónica")):
            s7 = section(path, "7")
            bullets = [b for b in s7.split("\n- ") if b.startswith(("**A `CABLE`", "**An output", "**Una entrada `CABLE`", "**Una salida para"))]
            self.assertEqual(len(bullets), 2, path)
            for b in bullets:
                self.assertIn(word, b.replace("\n  ", " "), path)

    def test_inv2_telephone_carve_out(self):
        for path, word in (("SPEC.md", "telephone"), ("docs/es/especificacion.md", "teléfono")):
            text = (ROOT / path).read_text(encoding="utf-8")
            start = text.index("`INV-2`")
            self.assertIn(word, text[start : text.index("`INV-3`")], path)

    def test_spec_lists_new_capabilities(self):
        for path in ("SPEC.md", "docs/es/especificacion.md"):
            text = (ROOT / path).read_text(encoding="utf-8")
            for code in ("IN-PHONELINE", "OUT-PHONELINE", "VERIFY-VOICE", "PROFILE-PHONE"):
                self.assertIn(f"`{code}`", text, path)


if __name__ == "__main__":
    unittest.main()
