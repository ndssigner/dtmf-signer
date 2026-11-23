"""tools/install_pex.py: the PEX's firmware and its language packs in one UF2
for the Pico, refused when the result would not work."""
import hashlib
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import install_pex as ip  # noqa: E402


def pack(lang: str, count: int, body: bytes = b"") -> bytes:
    """A minimal DSV3 pack with a valid CRC."""
    total = ip.PACK_HEADER.size + len(body)
    p = bytearray(ip.PACK_HEADER.pack(b"DSV3", lang.encode(), ip.PACK_FORMAT, count, total, 0, 0)) + body
    struct.pack_into("<I", p, 14, zlib.crc32(p))
    return bytes(p)


def firmware(count: int = 7, n: int = 4) -> bytes:
    """A stand-in for the PEX's firmware UF2: n blocks from the start of flash,
    the built-in English pack in the middle of them."""
    image = bytearray(b"\0" * (256 * n))
    en = pack("en", count, b"x" * 40)
    image[300:300 + len(en)] = en
    out = bytearray()
    for i in range(n):
        out += struct.pack("<8I", ip.MAGIC0, ip.MAGIC1, ip.FLAG_FAMILY, ip.FLASH + 256 * i, 256, i, n, ip.FAMILY)
        out += image[256 * i:256 * (i + 1)].ljust(476, b"\0") + struct.pack("<I", ip.MAGIC_END)
    return bytes(out)


class InstallPexTest(unittest.TestCase):
    def run_in(self, files: dict, args=()):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            for name, data in files.items():
                (d / name).write_bytes(data)
            cwd = Path.cwd()
            import os
            os.chdir(d)
            try:
                rc = ip.main(["--out", "out.uf2", *args])
                out = (d / "out.uf2").read_bytes() if (d / "out.uf2").exists() else None
            finally:
                os.chdir(cwd)
            return rc, out

    def test_firmware_and_packs_in_one_file(self):
        es, pt = pack("es", 7, b"e" * 5000), pack("pt", 7, b"p" * 300)
        rc, out = self.run_in({ip.FIRMWARE: firmware(), "es.bin": es, "pt.bin": pt})
        self.assertEqual(rc, 0)
        n = len(out) // 512
        self.assertEqual(n, 4 + ip.REGION_SIZE // 256)        # firmware + the whole voice region
        region = bytearray()
        for i in range(n):
            blk = out[i * 512:(i + 1) * 512]
            self.assertEqual(struct.unpack_from("<2I", blk, 20), (i, n))   # numbered as one file
            addr, size = struct.unpack_from("<2I", blk, 12)
            if addr >= ip.REGION:
                region += blk[32:32 + size]
        self.assertEqual(region[:len(es)], es)                 # sorted by name: es first
        self.assertEqual(region[8192:8192 + len(pt)], pt)      # the next 4 KB boundary
        self.assertEqual(set(region[8192 + len(pt):]), {0xFF})  # the rest erased

    def test_no_pack_erases_the_voice_region(self):
        rc, out = self.run_in({ip.FIRMWARE: firmware()})
        self.assertEqual(rc, 0)
        self.assertEqual(len(out) // 512, 4 + ip.REGION_SIZE // 256)

    def test_refusals(self):
        fw = firmware(count=7)
        for files in ({},                                                   # no firmware
                      {ip.FIRMWARE: b"not a uf2"},
                      {ip.FIRMWARE: fw, "es.bin": pack("es", 8)},            # another version
                      {ip.FIRMWARE: fw, "es.bin": pack("es", 7)[:-1] + b"?"},  # damaged
                      {ip.FIRMWARE: fw, "es.bin": b"hello"},                 # not a pack
                      {ip.FIRMWARE: fw, "a.bin": pack("es", 7), "b.bin": pack("es", 7)},
                      {ip.FIRMWARE: fw, "en.bin": pack("en", 7)},
                      {ip.FIRMWARE: fw, **{f"{c}.bin": pack(c, 7) for c in ("de", "es", "fr", "pt")}},
                      {ip.FIRMWARE: fw, "SHA256SUMS": b"00" * 32 + b"  " + ip.FIRMWARE.encode()},
                      {ip.FIRMWARE: fw.replace(b"DSV3en", b"XXXXen")}):        # not the PEX's
            rc, out = self.run_in(files)
            self.assertEqual((rc, out), (1, None), sorted(files))

    def test_sums_checked_when_present(self):
        fw = firmware()
        sums = hashlib.sha256(fw).hexdigest().encode() + b"  " + ip.FIRMWARE.encode() + b"\n"
        rc, out = self.run_in({ip.FIRMWARE: fw, "SHA256SUMS": sums})
        self.assertEqual(rc, 0)

    def test_the_committed_pack_is_accepted(self):
        info = ip.pack_info((ROOT / "app" / "voice" / "packs" / "es.bin").read_bytes(), "es.bin")
        self.assertEqual(info["lang"], "es")


if __name__ == "__main__":
    unittest.main()
