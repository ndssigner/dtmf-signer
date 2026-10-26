"""Tests of `dtmf-signer-host sign`: a PSBT in (base64, binary or UR), the
review summary, "yes" on standard input, the signed PSBT out."""

import base64
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "build" / "ports" / "host" / "dtmf-signer-host"
UT = ROOT / "third_party" / "ur-tones"
sys.path.insert(0, str(UT / "reference" / "python"))

import ur_tones  # noqa: E402

VECTORS = json.loads((ROOT / "vectors" / "psbt-v0.json").read_text())["vectors"]
MAGIC = bytes.fromhex("70736274ff")


def read_varint(b, i):
    n = b[i]
    if n < 0xFD:
        return n, i + 1
    size = {0xFD: 2, 0xFE: 4, 0xFF: 8}[n]
    return int.from_bytes(b[i + 1:i + 1 + size], "little"), i + 1 + size


def read_map(b, i):
    """({key: value}, next index) of the key-value map at b[i]."""
    pairs = {}
    while True:
        n, i = read_varint(b, i)
        if n == 0:
            return pairs, i
        key = b[i:i + n]
        i += n
        n, i = read_varint(b, i)
        pairs[key] = b[i:i + n]
        i += n


def input_signatures(psbt):
    """{public key: signature} of the partial signatures (key 0x02) in the
    input maps of the binary PSBT."""
    assert psbt.startswith(MAGIC)
    glob, i = read_map(psbt, len(MAGIC))
    tx = glob[b"\x00"]
    n_in, j = read_varint(tx, 4)
    n_out = None
    for _ in range(n_in):  # skip the inputs: outpoint, script, sequence
        n, j = read_varint(tx, j + 36)
        j += n + 4
    n_out, _ = read_varint(tx, j)
    sigs = {}
    for _ in range(n_in):
        pairs, i = read_map(psbt, i)
        for key, value in pairs.items():
            if key[0] == 0x02:
                assert len(key) == 34
                assert key[1:] not in sigs
                sigs[key[1:]] = value
    for _ in range(n_out):
        _, i = read_map(psbt, i)
    assert i == len(psbt)
    return sigs


def run(args, stdin="yes\n"):
    """Runs the tool; returns (exit code, stdout, stderr)."""
    p = subprocess.run([str(HOST), *args], input=stdin, capture_output=True,
                       text=True, timeout=60)
    return p.returncode, p.stdout, p.stderr


def seed_args(v):
    args = ["--ur", ur_tones.seed_to_ur(bytes.fromhex(v["entropy"]))]
    if v["passphrase"]:
        args += ["--passphrase", v["passphrase"]]
    if v["testnet"]:
        args.append("--testnet")
    return args


# ---- a multi-part UR, from the pure fragments of UR's fountain code --------
# (the first seq_len parts of a message each carry one fragment, unmixed; this
# is BCR-2020-005 written out again here, independent of the C encoder)

def cbor_head(major, n):
    if n < 24:
        return bytes([major << 5 | n])
    for info, size in ((24, 1), (25, 2), (26, 4)):
        if n < 1 << (8 * size):
            return bytes([major << 5 | info]) + n.to_bytes(size, "big")
    raise ValueError(n)


def ur_parts(type_, payload, max_fragment):
    message = cbor_head(2, len(payload)) + payload
    seq_len = -(-len(message) // max_fragment)
    size = -(-len(message) // seq_len)
    padded = message.ljust(size * seq_len, b"\0")
    checksum = ur_tones.crc32(message)
    parts = []
    for seq in range(1, seq_len + 1):
        fragment = padded[(seq - 1) * size:seq * size]
        body = (b"\x85" + cbor_head(0, seq) + cbor_head(0, seq_len)
                + cbor_head(0, len(message)) + cbor_head(0, checksum)
                + cbor_head(2, size) + fragment)
        parts.append(ur_tones.ur_text(type_, ur_tones.KIND_PART, body))
    return parts


def single_ur(type_, payload):
    return ur_tones.ur_text(type_, ur_tones.KIND_SINGLE, cbor_head(2, len(payload)) + payload)


class HostSignTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def path(self, name, data):
        p = Path(self.tmp.name) / name
        p.write_bytes(data if isinstance(data, bytes) else data.encode())
        return str(p)

    def sign(self, v, psbt_file, stdin="yes\n", extra=()):
        return run(["sign", *seed_args(v), "--psbt", psbt_file, *extra], stdin)

    def check_signed(self, v, code, out, err):
        self.assertEqual(code, 0, err)
        review = out + err
        self.assertIn("FEE: %.8f BTC" % (v["fee"] / 1e8), review)
        for o in v["outputs"]:
            if "address" in o:
                self.assertIn(o["address"], review)
            else:
                self.assertIn("script, %d bytes" % (len(o["script_hex"]) // 2), review)
        self.assertEqual(("WARNING: high fee" in review), v["fee_warning"])
        signed = base64.b64decode(out.strip().splitlines()[-1], validate=True)
        got = input_signatures(signed)
        expected = [s for s in v["signatures"] if s is not None]  # None: an input not ours
        self.assertEqual(len(got), len(expected))
        self.assertEqual("NOT SIGNED" in review, None in v["signatures"])
        for s in expected:
            self.assertEqual(got.get(bytes.fromhex(s["pubkey"])), bytes.fromhex(s["sig"]), s["pubkey"])
        return signed

    def test_host_sign_vectors(self):
        sign = [v for v in VECTORS if v["expect"] == "sign"]
        reject = [v for v in VECTORS if v["expect"] == "reject"]
        self.assertEqual((len(sign), len(reject)), (13, 10))
        for v in sign:
            with self.subTest(v["name"]):
                f = self.path("in.psbt", v["psbt"])
                code, out, err = self.sign(v, f)
                self.check_signed(v, code, out, err)
        for v in reject:
            with self.subTest(v["name"]):
                f = self.path("in.psbt", v["psbt"])
                code, out, err = self.sign(v, f)
                self.assertEqual(code, 1, err)
                self.assertEqual(out, "")
                self.assertIn(v["error"], err)

    def test_host_sign_needs_yes(self):
        v = VECTORS[0]
        f = self.path("in.psbt", v["psbt"])
        for stdin in ("", "no\n", "Yes\n", "yes please\n", " yes\n", "\n"):
            with self.subTest(stdin=stdin):
                code, out, err = self.sign(v, f, stdin)
                self.assertEqual(code, 3, err)
                self.assertEqual(out, "")
                self.assertIn("FEE:", err + out)  # the review came first
        # the answer is never taken from the review: nothing is written
        outfile = Path(self.tmp.name) / "signed.psbt"
        code, out, err = self.sign(v, f, "no\n", ["--out", str(outfile)])
        self.assertEqual(code, 3)
        self.assertFalse(outfile.exists())

    def test_host_sign_nothing_to_sign(self):
        # a PSBT none of whose inputs is ours (the wallet has another
        # passphrase): not a signature, so no question, no output, exit 1
        v = dict(VECTORS[0], passphrase="another passphrase")
        f = self.path("in.psbt", v["psbt"])
        outfile = Path(self.tmp.name) / "signed.psbt"
        code, out, err = self.sign(v, f, "yes\n", ["--out", str(outfile)])
        self.assertEqual(code, 1, err)
        self.assertEqual(out, "")
        self.assertFalse(outfile.exists())
        self.assertIn("FEE:", err)  # the review is still shown
        self.assertIn("NOT SIGNED", err)
        self.assertIn("nothing to sign: no input of this wallet", err)
        self.assertNotIn("Type yes", err)  # nothing was asked

    def test_host_sign_summary_before_question(self):
        v = VECTORS[0]
        f = self.path("in.psbt", v["psbt"])
        code, out, err = self.sign(v, f)
        self.assertEqual(code, 0, err)
        # the summary is on stderr (stdout carries only the PSBT), and the
        # question comes after it
        self.assertLess(err.index("FEE:"), err.index("yes"))
        self.assertEqual(len(out.strip().splitlines()), 1)

    def test_host_sign_formats_and_out_file(self):
        v = VECTORS[0]
        raw = base64.b64decode(v["psbt"])
        _, want, _ = self.sign(v, self.path("a.psbt", v["psbt"]))
        for name, data in (("binary", raw), ("base64 with newline", v["psbt"] + "\n"),
                           ("single UR", single_ur("crypto-psbt", raw) + "\n"),
                           ("single UR psbt", single_ur("psbt", raw))):
            with self.subTest(name):
                code, out, err = self.sign(v, self.path("b.psbt", data))
                self.assertEqual(code, 0, err)
                self.assertEqual(out, want)
        outfile = Path(self.tmp.name) / "signed.psbt"
        code, out, err = self.sign(v, self.path("c.psbt", v["psbt"]), extra=["--out", str(outfile)])
        self.assertEqual(code, 0, err)
        self.assertEqual(out, "")
        self.assertEqual(outfile.read_text().strip(), want.strip())

    def test_host_sign_ur_parts(self):
        for v in [x for x in VECTORS if x["expect"] == "sign"][:3]:
            raw = base64.b64decode(v["psbt"])
            for type_ in ("crypto-psbt", "psbt"):
                with self.subTest(v["name"], type=type_):
                    parts = ur_parts(type_, raw, 60)
                    self.assertGreater(len(parts), 3)
                    # in reverse order, with blank lines and a repeated part
                    text = "\n".join(reversed(parts)) + "\n\n" + parts[0] + "\n"
                    code, out, err = self.sign(v, self.path("p.txt", text))
                    self.check_signed(v, code, out, err)

    def test_host_sign_bad_input(self):
        v = VECTORS[0]
        raw = base64.b64decode(v["psbt"])
        parts = ur_parts("crypto-psbt", raw, 60)
        for name, data in (("empty", ""), ("garbage", "hello world\n"),
                           ("truncated binary", raw[:-3]),
                           ("missing part", "\n".join(parts[:-1]) + "\n"),
                           ("bad base64", v["psbt"][:-4] + "!!!!")):
            with self.subTest(name):
                code, out, err = self.sign(v, self.path("x.psbt", data))
                self.assertEqual(code, 1, err)
                self.assertEqual(out, "")
                self.assertTrue(err)
        code, out, err = self.sign(v, str(Path(self.tmp.name) / "missing"))
        self.assertEqual((code, out), (1, ""))

    def test_host_sign_usage(self):
        v = VECTORS[0]
        f = self.path("in.psbt", v["psbt"])
        ur = ur_tones.seed_to_ur(bytes(16))
        for args in (["sign"], ["sign", "--psbt", f], ["sign", "--ur", ur],
                     ["sign", "--ur", ur, "--psbt"], ["sign", "--ur", ur, "--psbt", f, "--out"],
                     ["sign", "--ur", ur, "--psbt", f, "--psbt", f],
                     ["keys", "--ur", ur, "--psbt", f]):
            with self.subTest(args=args):
                self.assertEqual(run(args)[0], 2)


if __name__ == "__main__":
    unittest.main()
