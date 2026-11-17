#!/usr/bin/env python3
"""Wraps a voice pack in a UF2 file for the Pico's voice flash region.

    python3 tools/voice/make_uf2.py app/voice/packs/es.bin --offset 0 es.uf2

The region is 1 MB starting at flash offset 0x100000; packs sit back to back
in it, each starting on a 4 KB boundary, and --offset is where this one starts
(a multiple of 4096, relative to the region); it is written at
0x10100000 + offset (the XIP address the boot ROM expects). The next pack
goes at the next 4 KB boundary after this one's end. Dropping the file on the board in BOOTSEL mode writes the
pack without touching the firmware; the firmware finds it at boot.

UF2 blocks: 512 bytes, 256 bytes of payload each, the family ID flag set.
Pure Python.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

REGION = 0x10100000
REGION_SIZE = 0x100000  # 1 MB
ALIGN = 4096
PAYLOAD = 256
MAGIC0 = 0x0A324655
MAGIC1 = 0x9E5D5157
MAGIC_END = 0x0AB16F30
FLAG_FAMILY = 0x00002000
FAMILY = 0xE48BFF56  # the RP2040 family ID


def to_uf2(pack: bytes, offset: int) -> bytes:
    if offset < 0 or offset % ALIGN:
        raise ValueError("the offset must be a multiple of %d" % ALIGN)
    if offset + len(pack) > REGION_SIZE:
        raise ValueError("the pack (%d bytes) at offset 0x%x would cross the end of the "
                         "1 MB region" % (len(pack), offset))
    base = REGION + offset
    chunks = [pack[i:i + PAYLOAD] for i in range(0, len(pack), PAYLOAD)]
    out = bytearray()
    for n, chunk in enumerate(chunks):
        data = chunk.ljust(476, b"\0")
        out += struct.pack("<8I", MAGIC0, MAGIC1, FLAG_FAMILY, base + n * PAYLOAD,
                           PAYLOAD, n, len(chunks), FAMILY)
        out += data + struct.pack("<I", MAGIC_END)
    return bytes(out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("pack")
    ap.add_argument("--offset", type=lambda v: int(v, 0), required=True,
                    help="region-relative start, a multiple of 4096")
    ap.add_argument("out")
    a = ap.parse_args(argv)
    try:
        uf2 = to_uf2(Path(a.pack).read_bytes(), a.offset)
    except ValueError as e:
        raise SystemExit(str(e))
    Path(a.out).write_bytes(uf2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
