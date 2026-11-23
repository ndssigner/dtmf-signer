#!/usr/bin/env python3
"""Installs the Phone Exchange (PEX) on a Raspberry Pi Pico: its firmware and
the language packs you choose.

  1. Put in one folder this file, dtmf-signer-pex.uf2 and the packs you want
     (es.bin, ...), all from the same version's downloads.
  2. In that folder, run:   python3 install_pex.py
  3. When it asks, hold the Pico's BOOTSEL button while you plug it in. It
     asks twice: once for the firmware, once for the languages.

It checks the files first and says which languages go in. English is
always there; each pack adds a language (three at most), offered when the
handset is lifted. Packs from an earlier install are wiped.

  python3 install_pex.py FIRMWARE.uf2 es.bin ...   name the files yourself
  python3 install_pex.py --out FOLDER              write the two UF2s, copy nothing

Needs Python 3.8 or later and nothing else. Never connect the PEX to a
socket of the real telephone network.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import string
import struct
import sys
import time
import zlib
from pathlib import Path

FIRMWARE = "dtmf-signer-pex.uf2"
FLASH = 0x10000000
REGION = 0x10100000            # the voice packs' 1 MB, after the firmware's
REGION_SIZE = 0x100000
ALIGN = 4096                   # packs start on 4 KB boundaries
MAX_PACKS = 3                  # besides the built-in English
PAYLOAD = 256
MAGIC0, MAGIC1, MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FLAG_FAMILY, FAMILY = 0x00002000, 0xE48BFF56   # the RP2040
PACK_HEADER = struct.Struct("<4s2sHHIII")       # "DSV3", lang, format, count, total, crc, grammar
PACK_FORMAT = 3


class Refused(Exception):
    """A file or a choice that would not give a working PEX."""


# -- Files -------------------------------------------------------------------

def uf2_blocks(data: bytes, name: str) -> list[tuple[int, bytes]]:
    """(address, payload) of each block of an RP2040 UF2."""
    if not data or len(data) % 512:
        raise Refused(f"{name} is not a UF2 file")
    out = []
    for i in range(0, len(data), 512):
        b = data[i:i + 512]
        m0, m1, flags, addr, size, _, _, family = struct.unpack_from("<8I", b)
        if (m0, m1, struct.unpack_from("<I", b, 508)[0]) != (MAGIC0, MAGIC1, MAGIC_END):
            raise Refused(f"{name} is not a UF2 file")
        if not flags & FLAG_FAMILY or family != FAMILY or size > 476:
            raise Refused(f"{name} is not for the Raspberry Pi Pico (RP2040)")
        out.append((addr, b[32:32 + size]))
    return out


def pack_info(data: bytes, name: str) -> dict:
    """A language pack's language and prompt count, after checking it whole."""
    if len(data) < PACK_HEADER.size:
        raise Refused(f"{name} is not a language pack")
    magic, lang, fmt, count, total, crc, _ = PACK_HEADER.unpack_from(data)
    if magic != b"DSV3" or fmt != PACK_FORMAT:
        raise Refused(f"{name} is not a language pack")
    if total != len(data) or zlib.crc32(data[:14] + b"\0\0\0\0" + data[18:]) != crc:
        raise Refused(f"{name} is damaged or incomplete: download it again")
    return {"lang": lang.decode("ascii", "replace"), "count": count, "size": len(data)}


def builtin_count(blocks: list[tuple[int, bytes]]) -> int:
    """The prompt count of the English pack built into the firmware: every
    pack must have the same, or the firmware ignores it."""
    fw = [(addr, p) for addr, p in blocks if FLASH <= addr < REGION]
    image = bytearray(b"\xff" * (max((a + len(p) for a, p in fw), default=FLASH) - FLASH))
    for addr, payload in fw:
        image[addr - FLASH:addr - FLASH + len(payload)] = payload
    start = 0
    while (i := image.find(b"DSV3en", start)) >= 0:
        try:
            return pack_info(bytes(image[i:i + PACK_HEADER.unpack_from(image, i)[4]]), "")["count"]
        except (Refused, struct.error):
            start = i + 1
    raise Refused("this firmware is not the PEX's (no built-in English pack found)")


def check_sums(firmware: Path) -> str:
    """The firmware against SHA256SUMS, when that file is beside it."""
    sums = firmware.parent / "SHA256SUMS"
    if not sums.exists():
        return "not checked (no SHA256SUMS beside it)"
    digest = hashlib.sha256(firmware.read_bytes()).hexdigest()
    for line in sums.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].lstrip("*") == firmware.name:
            if parts[0].lower() != digest:
                raise Refused(f"{firmware.name} does not match SHA256SUMS: download it again")
            return "matches SHA256SUMS"
    return "not checked (not listed in SHA256SUMS)"


# -- The UF2 -----------------------------------------------------------------

def region_blocks(packs: list[bytes]) -> list[tuple[int, bytes]]:
    """The voice region's blocks: the packs back to back on 4 KB boundaries,
    then one erased (0xFF) block at each later 4 KB boundary. The firmware
    looks for a pack only at those boundaries, so that is enough to leave no
    pack of an earlier install behind, and it keeps the file small."""
    out = []
    off = 0
    for p in packs:
        if off + len(p) > REGION_SIZE:
            raise Refused("these packs do not fit together in the Pico's 1 MB for voices")
        out += [(REGION + off + i, p[i:i + PAYLOAD].ljust(PAYLOAD, b"\xff")) for i in range(0, len(p), PAYLOAD)]
        off = -(-(off + len(p)) // ALIGN) * ALIGN
    out += [(REGION + b, b"\xff" * PAYLOAD) for b in range(off, REGION_SIZE, ALIGN)]
    return out


def to_uf2(blocks: list[tuple[int, bytes]]) -> bytes:
    """Blocks as one UF2 file, numbered over the whole."""
    out = bytearray()
    for n, (addr, payload) in enumerate(blocks):
        out += struct.pack("<8I", MAGIC0, MAGIC1, FLAG_FAMILY, addr, len(payload), n, len(blocks), FAMILY)
        out += payload.ljust(476, b"\0") + struct.pack("<I", MAGIC_END)
    return bytes(out)


# -- The Pico ------------------------------------------------------------------

def pico_drives() -> list[Path]:
    """Mounted drives of a Pico in BOOTSEL mode (they hold INFO_UF2.TXT)."""
    roots: list[Path] = []
    if sys.platform == "win32":
        roots = [Path(f"{c}:\\") for c in string.ascii_uppercase[3:]]
    else:
        user = os.environ.get("USER", "")
        for base in ("/Volumes", f"/media/{user}", f"/run/media/{user}", "/media", "/mnt"):
            try:
                roots += [p for p in Path(base).iterdir() if p.is_dir()]
            except OSError:
                pass
    found = []
    for r in roots:
        try:
            info = (r / "INFO_UF2.TXT").read_text(errors="replace")
        except OSError:
            continue
        if "RP2" in info:
            found.append(r)
    return found


def wait_for(present: bool, seconds: float) -> list[Path]:
    """Waits until a Pico in BOOTSEL is there (present) or gone; its drives."""
    end = time.monotonic() + seconds
    while True:
        drives = pico_drives()
        if bool(drives) == present or time.monotonic() > end:
            return drives
        time.sleep(0.5)


def copy_onto(drive: Path, name: str, uf2: bytes) -> None:
    """Copies a UF2 onto the Pico's drive; the Pico restarts once it is in."""
    try:
        with open(drive / name, "wb") as f:
            f.write(uf2)
            f.flush()
            os.fsync(f.fileno())
    except OSError:
        pass  # the drive may vanish as the Pico restarts: checked below
    if wait_for(False, 20):
        raise Refused("the Pico did not take the file (its drive is still there): run this again")


# -- Main ----------------------------------------------------------------------

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog="\n".join(__doc__.splitlines()[2:]))
    ap.add_argument("files", nargs="*", help=f"the firmware ({FIRMWARE}) and the packs (.bin)")
    ap.add_argument("--out", metavar="FOLDER",
                    help="write the two UF2s to this folder instead of copying them onto the Pico")
    a = ap.parse_args(argv)

    here = Path.cwd()
    files = [Path(f) for f in a.files]
    fw = [f for f in files if f.suffix.lower() == ".uf2"]
    packs = [f for f in files if f.suffix.lower() == ".bin"]
    if not files:
        fw = [here / FIRMWARE] if (here / FIRMWARE).exists() else []
        packs = sorted(here.glob("*.bin"))
    try:
        if len(fw) != 1:
            raise Refused(f"put {FIRMWARE} in this folder ({here}), or name the firmware's file")
        if [f for f in files if f.suffix.lower() not in (".uf2", ".bin")]:
            raise Refused("only the firmware (.uf2) and language packs (.bin) can be named")
        if len(packs) > MAX_PACKS:
            raise Refused(f"{len(packs)} packs: the PEX takes {MAX_PACKS} at most, besides English")

        fw_uf2 = fw[0].read_bytes()
        firmware = uf2_blocks(fw_uf2, fw[0].name)
        if any(addr + len(p) > REGION for addr, p in firmware):
            raise Refused(f"{fw[0].name} reaches into the voice region: it is not the PEX's")
        sums = check_sums(fw[0])
        count = builtin_count(firmware)
        datas, langs = [], []
        for p in packs:
            data = p.read_bytes()
            info = pack_info(data, p.name)
            if info["count"] != count:
                raise Refused(f"{p.name} is for another version of the firmware: take both "
                              "from the same version's downloads")
            if info["lang"] in langs or info["lang"] == "en":
                raise Refused(f"{p.name}: that language is already there")
            datas.append(data)
            langs.append(info["lang"])
        voices_uf2 = to_uf2(region_blocks(datas))
    except (Refused, OSError) as e:
        print(f"Nothing was installed: {e}", file=sys.stderr)
        return 1

    print(f"Firmware: {fw[0].name}, {sums}")
    print("Languages: " + ", ".join(["en (built in)"] + langs))
    # Two copies: the Pico does not take one file that writes both the
    # firmware and the voice region, but it takes each on its own.
    if a.out:
        out = Path(a.out)
        out.mkdir(parents=True, exist_ok=True)
        (out / "1-firmware.uf2").write_bytes(fw_uf2)
        (out / "2-languages.uf2").write_bytes(voices_uf2)
        print(f"Written in {out}: copy 1-firmware.uf2 onto the Pico in BOOTSEL, then, with it "
              "in BOOTSEL again, 2-languages.uf2.")
        return 0
    try:
        drives = pico_drives()
        if len(drives) > 1:
            raise Refused("more than one Pico is in BOOTSEL: leave only one plugged in")
        if not drives:
            print("Hold the Pico's BOOTSEL button while you plug it in...")
            drives = wait_for(True, 120)
            if not drives:
                raise Refused("no Pico in BOOTSEL mode appeared")
        print(f"1 of 2: the firmware, onto the Pico ({drives[0]})...")
        copy_onto(drives[0], "1-firmware.uf2", fw_uf2)
        print("The Pico restarted. Unplug it, hold BOOTSEL and plug it in again...")
        drives = wait_for(True, 300)
        if not drives:
            raise Refused("the Pico did not come back in BOOTSEL: the firmware is in; run this "
                          "again to add the languages")
        print(f"2 of 2: the languages, onto the Pico ({drives[0]})...")
        copy_onto(drives[0], "2-languages.uf2", voices_uf2)
    except Refused as e:
        print(f"Stopped: {e}", file=sys.stderr)
        return 2
    print("Done. The Pico restarts by itself; lift the handset and it greets you.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
