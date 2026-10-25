"""Tests for tools/check_docs.py: links, anchors and English/Spanish parity."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import check_docs  # noqa: E402


class DocsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path


class SlugTest(unittest.TestCase):
    def test_slug_spanish(self):
        self.assertEqual(check_docs.slug("8. Política de canales"), "8-política-de-canales")
        self.assertEqual(check_docs.slug("Qué protegemos y de quién"), "qué-protegemos-y-de-quién")


class LinksTest(DocsTest):
    def test_links_ok(self):
        a = self.write("a.md", "See [x](b.md#título-dos).\n")
        self.write("b.md", "# B\n\n## Título dos\n")
        self.assertEqual(check_docs.check_links(a, self.root), [])

    def test_links_resolve_from_the_file_directory(self):
        a = self.write("docs/es/a.md", "[English](../../b.md)\n")
        self.write("b.md", "# B\n")
        self.assertEqual(check_docs.check_links(a, self.root), [])

    def test_links_detect_missing_file(self):
        a = self.write("a.md", "See [x](c.md).\n")
        errors = check_docs.check_links(a, self.root)
        self.assertEqual(len(errors), 1)
        self.assertIn("c.md", errors[0])

    def test_links_detect_missing_anchor(self):
        a = self.write("a.md", "See [x](b.md#otro).\n")
        self.write("b.md", "# B\n\n## Título dos\n")
        errors = check_docs.check_links(a, self.root)
        self.assertEqual(len(errors), 1)
        self.assertIn("#otro", errors[0])

    def test_links_detect_missing_local_anchor(self):
        a = self.write("a.md", "# A\n\nSee [x](#nada).\n")
        errors = check_docs.check_links(a, self.root)
        self.assertEqual(len(errors), 1)
        self.assertIn("#nada", errors[0])

    def test_links_ignore_external(self):
        a = self.write("a.md", "[w](https://example.org/x.md) [m](mailto:a@b.c) [h](http://e.org)\n")
        self.assertEqual(check_docs.check_links(a, self.root), [])


class ParityTest(DocsTest):
    def test_parity_ok(self):
        en = self.write("en.md", "# T\n\n## One\n\nRule `INV-1` at `m/84'/0'/0'`.\n\n## Two\n")
        es = self.write("es.md", "# T\n\n## Uno\n\nRegla `INV-1` en `m/84'/0'/0'`.\n\n## Dos\n")
        self.assertEqual(check_docs.check_parity(en, es), [])

    def test_parity_detects_heading_mismatch(self):
        en = self.write("en.md", "# T\n\n## One\n")
        es = self.write("es.md", "# T\n\n### Uno\n")
        self.assertEqual(len(check_docs.check_parity(en, es)), 1)

    def test_parity_detects_code_span_mismatch(self):
        en = self.write("en.md", "# T\n\nAt least `12` characters.\n")
        es = self.write("es.md", "# T\n\nAl menos `10` caracteres.\n")
        errors = check_docs.check_parity(en, es)
        self.assertTrue(errors)
        joined = "\n".join(errors)
        self.assertIn("12", joined)
        self.assertIn("10", joined)

    def test_parity_ignores_fenced_blocks(self):
        en = self.write("en.md", "# T\n\n```\n## not a heading `a`\n```\n")
        es = self.write("es.md", "# T\n\n```\n### tampoco `b`\n```\n")
        self.assertEqual(check_docs.check_parity(en, es), [])


if __name__ == "__main__":
    unittest.main()
