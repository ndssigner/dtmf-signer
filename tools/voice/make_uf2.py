#!/usr/bin/env python3
"""Wraps a voice pack in a UF2 file for the Pico's voice flash region.

    python3 tools/voice/make_uf2.py app/voice/packs/es.bin --offset 0 es.uf2

The region is 1 MB starting at flash offset 0x100000; packs sit back to back
in it, each starting on a 4 KB boundary, and --offset is where this one starts
(a multiple of 4096, relative to the region); it is written at
0x10100000 + offset (the XIP address the boot ROM expects). The next pack
goes at the next 4 KB boundary after this one's end. Dropping the file on the board in BOOTSEL mode writes the
pack without touching the firmware; the firmware finds it at boot.

With --firmware, the output is the firmware's UF2 and the pack in one file,
renumbered as one: a single copy onto the board writes both. The boot ROM
reboots the board as soon as one file is complete, so two files dropped at
once do not both arrive; one file does.

    python3 tools/voice/make_uf2.py app/voice/packs/es.bin --offset 0 \\
        --firmware dtmf-signer-pex.uf2 dtmf-signer-pex-es.uf2

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


def blocks(uf2: bytes) -> list[tuple[int, int, bytes]]:
    """(target address, payload size, the whole block) of each block of a UF2,
    checked: its magics and the RP2040 family."""
    if len(uf2) % 512 or not uf2:
        raise ValueError("not a UF2 file: its size is not a whole number of 512-byte blocks")
    out = []
    for i in range(0, len(uf2), 512):
        b = uf2[i:i + 512]
        m0, m1, flags, addr, size, _, _, family = struct.unpack_from("<8I", b)
        if (m0, m1, struct.unpack_from("<I", b, 508)[0]) != (MAGIC0, MAGIC1, MAGIC_END):
            raise ValueError("not a UF2 file: block %d has bad magic numbers" % (i // 512))
        if not flags & FLAG_FAMILY or family != FAMILY:
            raise ValueError("block %d is not for the RP2040" % (i // 512))
        out.append((addr, size, b))
    return out


def join(*uf2s: bytes) -> bytes:
    """Several UF2 files as one: their blocks in order, renumbered over the
    whole, refused if any two write the same flash."""
    all_blocks = [blk for u in uf2s for blk in blocks(u)]
    spans = sorted((a, a + s) for a, s, _ in all_blocks)
    for (_, end), (start, _) in zip(spans, spans[1:]):
        if start < end:
            raise ValueError("two of the files write the same flash at 0x%x" % start)
    out = bytearray()
    for n, (_, _, b) in enumerate(all_blocks):
        out += b[:20] + struct.pack("<2I", n, len(all_blocks)) + b[28:]
    return bytes(out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("pack")
    ap.add_argument("--offset", type=lambda v: int(v, 0), required=True,
                    help="region-relative start, a multiple of 4096")
    ap.add_argument("--firmware", help="a firmware UF2 to put in the same file, before the pack")
    ap.add_argument("out")
    a = ap.parse_args(argv)
    try:
        uf2 = to_uf2(Path(a.pack).read_bytes(), a.offset)
        if a.firmware:
            uf2 = join(Path(a.firmware).read_bytes(), uf2)
    except ValueError as e:
        raise SystemExit(str(e))
    Path(a.out).write_bytes(uf2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
