"""Checks the Markdown documents: relative links and anchors resolve, and each
English document and its Spanish translation keep the same headings and the
same inline code (identifiers and normative values), so they cannot drift.

Usage: python3 tools/check_docs.py
"""

from __future__ import annotations

import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# (English, Spanish). A pair is skipped while either file is missing.
PAIRS: list[tuple[str, str]] = [
    ("README.md", "README.es.md"),
    ("SPEC.md", "docs/es/especificacion.md"),
    ("docs/catalog.md", "docs/es/catalogo.md"),
]

_FENCE = re.compile(r"^\s*(```|~~~)")
_HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
_LINK = re.compile(r"\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
_CODE = re.compile(r"(`+)(.+?)\1")
_EXTERNAL = ("http:", "https:", "mailto:")


def _prose_lines(text: str) -> list[str]:
    """The lines outside fenced code blocks."""
    lines, fenced = [], False
    for line in text.splitlines():
        if _FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            lines.append(line)
    return lines


def _headings(text: str) -> list[tuple[int, str]]:
    found = []
    for line in _prose_lines(text):
        m = _HEADING.match(line)
        if m:
            found.append((len(m.group(1)), m.group(2)))
    return found


def slug(heading: str) -> str:
    """The anchor GitHub gives a heading."""
    text = heading.lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


def _anchors(path: Path) -> set[str]:
    anchors, seen = set(), Counter()
    for _, title in _headings(path.read_text(encoding="utf-8")):
        base = slug(title)
        anchors.add(base if seen[base] == 0 else f"{base}-{seen[base]}")
        seen[base] += 1
    return anchors


def check_links(path: Path, root: Path) -> list[str]:
    errors = []
    name = path.relative_to(root) if path.is_relative_to(root) else path
    for line in _prose_lines(path.read_text(encoding="utf-8")):
        for target in _LINK.findall(_CODE.sub("", line)):
            if target.startswith(_EXTERNAL):
                continue
            file_part, _, anchor = target.partition("#")
            dest = (path.parent / file_part).resolve() if file_part else path
            if not dest.exists():
                errors.append(f"{name}: {target}: no such file")
                continue
            if anchor and dest.suffix == ".md" and anchor not in _anchors(dest):
                errors.append(f"{name}: {target}: no heading for #{anchor}")
    return errors


def _code_spans(text: str) -> Counter:
    spans = Counter()
    for line in _prose_lines(text):
        for _, code in _CODE.findall(line):
            spans[code.strip()] += 1
    return spans


def check_parity(en: Path, es: Path) -> list[str]:
    errors = []
    en_text, es_text = en.read_text(encoding="utf-8"), es.read_text(encoding="utf-8")
    en_levels = [level for level, _ in _headings(en_text)]
    es_levels = [level for level, _ in _headings(es_text)]
    if en_levels != es_levels:
        errors.append(f"{en.name} / {es.name}: headings differ: {en_levels} vs {es_levels}")
    en_code, es_code = _code_spans(en_text), _code_spans(es_text)
    for code, n in sorted((en_code - es_code).items()):
        errors.append(f"{en.name} / {es.name}: `{code}` only in {en.name} (x{n})")
    for code, n in sorted((es_code - en_code).items()):
        errors.append(f"{en.name} / {es.name}: `{code}` only in {es.name} (x{n})")
    return errors


def main(argv: list[str]) -> int:
    files = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "*.md"], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout.split()
    errors = []
    for name in files:
        path = ROOT / name
        if path.exists():
            errors += check_links(path, ROOT)
    for en, es in PAIRS:
        if (ROOT / en).exists() and (ROOT / es).exists():
            errors += check_parity(ROOT / en, ROOT / es)
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
