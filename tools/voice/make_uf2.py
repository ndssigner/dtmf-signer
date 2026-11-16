#!/usr/bin/env python3
"""Wraps a voice pack in a UF2 file for the Pico's voice flash region.

    python3 tools/voice/make_uf2.py app/voice/packs/es.bin --slot 0 es.uf2

The region starts at flash offset 0x100000 and holds two slots of 0x80000
bytes; slot N is written at 0x10100000 + N * 0x80000 (the XIP address the
boot ROM expects). Dropping the file on the board in BOOTSEL mode writes the
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
SLOT_SIZE = 0x80000
SLOTS = 2
PAYLOAD = 256
MAGIC0 = 0x0A324655
MAGIC1 = 0x9E5D5157
MAGIC_END = 0x0AB16F30
FLAG_FAMILY = 0x00002000
FAMILY = 0xE48BFF56  # the RP2040 family ID


def to_uf2(pack: bytes, slot: int) -> bytes:
    if not 0 <= slot < SLOTS:
        raise ValueError("slot must be 0..%d" % (SLOTS - 1))
    if len(pack) > SLOT_SIZE:
        raise ValueError("the pack (%d bytes) is larger than a slot (%d)" % (len(pack), SLOT_SIZE))
    base = REGION + slot * SLOT_SIZE
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
    ap.add_argument("--slot", type=int, choices=range(SLOTS), required=True)
    ap.add_argument("out")
    a = ap.parse_args(argv)
    try:
        uf2 = to_uf2(Path(a.pack).read_bytes(), a.slot)
    except ValueError as e:
        raise SystemExit(str(e))
    Path(a.out).write_bytes(uf2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
