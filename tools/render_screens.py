#!/usr/bin/env python3
"""Turns the output of the screens program (build/tests/c/screens) into the
PNG images of docs/screens.md: each 128x64 screen drawn at 3x, lit pixels
light on a dark panel, a faint gap between pixels as on the OLED.

Usage: tools/render_screens.py build/tests/c/screens docs/images/screens

Standard library only; the files come out byte for byte the same each run.
"""
import struct
import subprocess
import sys
import zlib
from pathlib import Path

SCALE = 3
BORDER = 6  # dark margin around the panel, in output pixels
LIT, LIT_EDGE = (236, 242, 246), (176, 186, 194)
DARK, DARK_EDGE = (16, 19, 23), (24, 28, 33)


def png(rows: list[str]) -> bytes:
    w, h = 128 * SCALE + 2 * BORDER, 64 * SCALE + 2 * BORDER
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        py = y - BORDER
        for x in range(w):
            px = x - BORDER
            if not (0 <= px < 128 * SCALE and 0 <= py < 64 * SCALE):
                raw += bytes(DARK)
                continue
            lit = rows[py // SCALE][px // SCALE] == "#"
            edge = px % SCALE == SCALE - 1 or py % SCALE == SCALE - 1
            raw += bytes((LIT_EDGE if edge else LIT) if lit else (DARK_EDGE if edge else DARK))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    head = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    out = subprocess.run([sys.argv[1]], capture_output=True, text=True, check=True).stdout
    dest = Path(sys.argv[2])
    dest.mkdir(parents=True, exist_ok=True)
    name, rows = None, []
    for line in out.splitlines() + ["== end"]:
        if line.startswith("== "):
            if name is not None:
                assert len(rows) == 64 and all(len(r) == 128 for r in rows), name
                (dest / f"{name}.png").write_bytes(png(rows))
            name, rows = line[3:], []
        else:
            rows.append(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
