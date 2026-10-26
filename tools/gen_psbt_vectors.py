#!/usr/bin/env python3
"""Generates vectors/psbt-v0.json with embit, an implementation independent of
the C core: no expected value below is computed by our own code.

Setup:   pip install -r tools/requirements-vectors.txt
Run:     python3 tools/gen_psbt_vectors.py            (writes vectors/psbt-v0.json)
         python3 tools/gen_psbt_vectors.py --check    (fails if the file differs)

The output is deterministic: no randomness, fixed ordering, fake previous
transactions derived from fixed labels. Only public test seeds are used.

Signatures: PSBT.sign_with() is used to find which inputs are ours and with
which key. embit's PrivateKey.sign() then "grinds" the nonce (extra entropy,
retried until the DER signature is at most 70 bytes), which makes about half
of its signatures differ from plain RFC 6979 (libsecp256k1's default nonce,
which the C signer uses). The expected signature recorded here is therefore
embit's PrivateKey.sign(hash, grind=False): plain RFC 6979 with low S. The
generator asserts that every sign_with() signature is either identical to it
or a ground variant of it, and verifies each signature with embit.

The "duplicate key" and "PSBT v2" vectors cannot be produced by embit (it
never writes duplicate keys, and writing version 2 drops the unsigned
transaction). They are built by editing the serialized bytes of a valid PSBT:
the first input's witness-UTXO pair is written twice, and a global
PSBT_GLOBAL_VERSION (0xFB) = 2 pair is appended to the global map.
"""

import base64
import hashlib
import json
import sys
from pathlib import Path

from embit import bip39, script
from embit.bip32 import HDKey
from embit.ec import PublicKey
from embit.networks import NETWORKS
from embit.psbt import PSBT, DerivationPath
from embit.transaction import Transaction, TransactionInput, TransactionOutput

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "vectors" / "psbt-v0.json"
UR_TONES = ROOT / "third_party" / "ur-tones" / "vectors" / "ur-tones-v0.json"

H = 0x80000000
SECP_N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
ABANDON = bytes(16)
FOREIGN_ENTROPY = bytes([0xFF] * 16)


def ur_tones_entropy():
    for v in json.loads(UR_TONES.read_text())["vectors"]:
        if v["name"] == "NDS-Signer test seed":
            return bytes.fromhex(v["entropy"])
    raise SystemExit("NDS-Signer test seed not found in ur-tones vectors")


def root_key(entropy, passphrase="", testnet=False):
    seed = bip39.mnemonic_to_seed(bip39.mnemonic_from_bytes(entropy), passphrase)
    return HDKey.from_seed(seed, version=NETWORKS["test" if testnet else "main"]["xprv"])


class Wallet:
    def __init__(self, entropy, passphrase="", testnet=False):
        self.entropy, self.passphrase, self.testnet = entropy, passphrase, testnet
        self.net = NETWORKS["test" if testnet else "main"]
        self.root = root_key(entropy, passphrase, testnet)
        self.fp = self.root.my_fingerprint
        self.coin = 1 if testnet else 0

    def path(self, chain, i, coin=None):
        return [84 + H, (self.coin if coin is None else coin) + H, H, chain, i]

    def key(self, chain, i, coin=None):
        return self.root.derive(self.path(chain, i, coin))

    def pub(self, chain, i, coin=None):
        return self.key(chain, i, coin).get_public_key()

    def derivation(self, chain, i, coin=None):
        return DerivationPath(self.fp, self.path(chain, i, coin))


def fake_txid(label):
    return hashlib.sha256(b"dtmf-signer fake previous transaction: " + label.encode()).digest()


def prev_tx(label, vout_index, value, spk):
    """A well-formed fake previous transaction with our output at vout_index."""
    outs = [TransactionOutput(1000 + k, script.p2wpkh(FOREIGN.pub(0, 100 + k))) for k in range(vout_index)]
    outs.append(TransactionOutput(value, spk))
    outs.append(TransactionOutput(777, script.p2wpkh(FOREIGN.pub(0, 200))))
    return Transaction(version=2, vin=[TransactionInput(fake_txid(label), 0)], vout=outs)


FOREIGN = Wallet(FOREIGN_ENTROPY)


class In:
    """Input spec. who: 'own' (wallet key chain/i) or 'foreign'."""

    def __init__(self, label, value, chain=0, i=0, who="own", vout=0, nonwit=False,
                 wit=True, sighash=None, nonwit_tamper=False, wit_value=None, coin=None):
        self.__dict__.update(locals())


def build(wallet, ins, outs, locktime=0):
    """outs: list of dicts {value, spk, deriv=(chain,i)|None}.
    Returns (psbt, [input value ...])."""
    prevs, vins = [], []
    for n, s in enumerate(ins):
        if s.who == "own":
            spk = script.p2wpkh(wallet.pub(s.chain, s.i, s.coin))
        else:
            spk = script.p2wpkh(FOREIGN.pub(0, s.i))
        p = prev_tx(s.label, s.vout, s.value, spk)
        prevs.append((p, spk))
        vins.append(TransactionInput(p.txid(), s.vout, sequence=0xFFFFFFFD))
    tx = Transaction(version=2, vin=vins,
                     vout=[TransactionOutput(o["value"], o["spk"]) for o in outs],
                     locktime=locktime)
    psbt = PSBT(tx)
    for n, s in enumerate(ins):
        p, spk = prevs[n]
        inp = psbt.inputs[n]
        if s.nonwit:
            q = p
            if s.nonwit_tamper:  # same shape, different content: its txid no longer matches
                q = Transaction(version=2, vin=p.vin, vout=list(p.vout), locktime=1)
            inp.non_witness_utxo = q
        if s.wit:
            wv = s.value if s.wit_value is None else s.wit_value
            inp.witness_utxo = TransactionOutput(wv, spk)
        if s.sighash is not None:
            inp.sighash_type = s.sighash
        if s.who == "own":
            inp.bip32_derivations[wallet.pub(s.chain, s.i, s.coin)] = wallet.derivation(s.chain, s.i, s.coin)
        else:
            inp.bip32_derivations[FOREIGN.pub(0, s.i)] = DerivationPath(FOREIGN.fp, FOREIGN.path(0, s.i))
    for n, o in enumerate(outs):
        if o.get("deriv"):
            c, i = o["deriv"]
            psbt.outputs[n].bip32_derivations[wallet.pub(c, i)] = wallet.derivation(c, i)
    return psbt


def out_ext(value, kind):
    """External outputs of several script types, all paying FOREIGN keys."""
    k = FOREIGN.pub(0, 1)
    spk = {
        "p2wpkh": script.p2wpkh(k),
        "p2wsh": script.p2wsh(script.multisig(1, [k])),
        "p2tr": script.p2tr(k),
        "p2sh": script.p2sh(script.p2wpkh(k)),
        "p2pkh": script.p2pkh(k),
        "op_return": script.Script(b"\x6a\x0bdtmf-signer"),
    }[kind]
    return {"value": value, "spk": spk, "kind": "external"}


def out_own(wallet, chain, i, value):
    return {"value": value, "spk": script.p2wpkh(wallet.pub(chain, i)), "deriv": (chain, i),
            "kind": "change" if chain == 1 else "own"}


def summary_outputs(wallet, outs):
    res = []
    for o in outs:
        d = {"amount": o["value"]}
        if o["spk"].script_type() is not None:
            d["address"] = o["spk"].address(wallet.net)
        else:
            d["script_hex"] = o["spk"].data.hex()
        d["kind"] = o["kind"]
        res.append(d)
    return res


def is_low_s(der):
    # DER: 30 len 02 rlen r 02 slen s
    rlen = der[3]
    s = int.from_bytes(der[6 + rlen:], "big")
    return s <= SECP_N // 2


def sign_expected(wallet, psbt):
    """Per-input expected partial signatures, from embit, verified."""
    signed = PSBT.parse(psbt.serialize())
    n = signed.sign_with(wallet.root)
    sigs, found = [], 0
    for idx, inp in enumerate(signed.inputs):
        if not inp.partial_sigs:
            sigs.append(None)
            continue
        assert len(inp.partial_sigs) == 1
        (pub, got), = inp.partial_sigs.items()
        h = signed.sighash(idx)
        # find the derivation to get the private key
        der = psbt.inputs[idx].bip32_derivations[pub]
        assert der.fingerprint == wallet.fp
        priv = wallet.root.derive(der.derivation).key
        assert priv.get_public_key() == pub
        sig = priv.sign(h, grind=False)
        der_sig = sig.serialize()
        assert is_low_s(der_sig), "high S"
        assert priv.sign(h, grind=False).serialize() == der_sig, "not deterministic"
        assert pub.verify(sig, h), "signature does not verify"
        assert got[-1] == 1 and pub.verify(type(sig).parse(got[:-1]), h)
        # embit only grinds when the plain signature is longer than 70 bytes
        assert got == der_sig + b"\x01" or len(der_sig) > 70
        sigs.append({"pubkey": pub.sec().hex(), "sig": (der_sig + b"\x01").hex()})
        found += 1
    assert found == n
    return sigs


def b64(raw):
    return base64.b64encode(raw).decode()


# ---- raw PSBT editing (only for the two cases embit cannot write) ----

def read_compact(b, o):
    f = b[o]
    if f < 0xFD:
        return f, o + 1
    if f == 0xFD:
        return int.from_bytes(b[o + 1:o + 3], "little"), o + 3
    raise ValueError("unsupported length")


def split_maps(raw, n_maps):
    """Returns (magic, [map, ...]) where each map is a list of (key, value)."""
    assert raw[:5] == b"psbt\xff"
    o, maps = 5, []
    for _ in range(n_maps):
        m = []
        while True:
            kl, o = read_compact(raw, o)
            if kl == 0:
                break
            k = raw[o:o + kl]; o += kl
            vl, o = read_compact(raw, o)
            m.append((k, raw[o:o + vl])); o += vl
        maps.append(m)
    assert o == len(raw)
    return maps


def ser_compact(n):
    assert n < 0xFD
    return bytes([n])


def join_maps(maps):
    out = b"psbt\xff"
    for m in maps:
        for k, v in m:
            out += ser_compact(len(k)) + k + ser_compact(len(v)) + v
        out += b"\x00"
    return out


def edited(psbt, fn):
    maps = split_maps(psbt.serialize(), 1 + len(psbt.inputs) + len(psbt.outputs))
    fn(maps)
    return join_maps(maps)


def duplicate_key(maps):
    pair = next(p for p in maps[1] if p[0] == b"\x01")  # witness-UTXO of input 0
    maps[1].insert(maps[1].index(pair) + 1, pair)


def make_v2(maps):
    maps[0].append((b"\xfb", (2).to_bytes(4, "little")))


# ---- vectors ----

def fee_warning(fee, outs):
    ext = sum(o["value"] for o in outs if o["kind"] == "external")
    if ext == 0:
        return fee > 1_000_000
    return fee * 10 > ext or fee > 1_000_000


def sign_vector(name, wallet, ins, outs):
    psbt = build(wallet, ins, outs)
    total_in = sum(s.value for s in ins)
    fee = total_in - sum(o["value"] for o in outs)
    assert fee >= 0
    return {
        "name": name,
        "entropy": wallet.entropy.hex(),
        "passphrase": wallet.passphrase,
        "testnet": wallet.testnet,
        "psbt": b64(psbt.serialize()),
        "expect": "sign",
        "fee": fee,
        "fee_warning": fee_warning(fee, outs),
        "outputs": summary_outputs(wallet, outs),
        "signatures": sign_expected(wallet, psbt),
    }


def reject_vector(name, wallet, raw_psbt, error):
    return {
        "name": name,
        "entropy": wallet.entropy.hex(),
        "passphrase": wallet.passphrase,
        "testnet": wallet.testnet,
        "psbt": b64(raw_psbt),
        "expect": "reject",
        "error": error,
    }


def generate():
    A = Wallet(ABANDON)
    N = Wallet(ur_tones_entropy())
    T = Wallet(ur_tones_entropy(), testnet=True)
    P = Wallet(ABANDON, passphrase="test passphrase")
    vs = []

    # --- sign ---
    vs.append(sign_vector("one input with change", A,
        [In("a1", 100_000)],
        [out_ext(60_000, "p2wpkh"), out_own(A, 1, 0, 39_000)]))
    vs.append(sign_vector("two inputs with non_witness_utxo", A,
        [In("b1", 80_000, 0, 1, vout=1, nonwit=True), In("b2", 50_000, 1, 0, nonwit=True)],
        [out_ext(100_000, "p2wsh"), out_own(A, 1, 1, 29_000)]))
    vs.append(sign_vector("testnet account m/84'/1'/0'", T,
        [In("t1", 250_000, 0, 3)],
        [out_ext(100_000, "p2wpkh"), out_own(T, 1, 2, 149_000)]))
    vs.append(sign_vector("BIP-39 passphrase", P,
        [In("p1", 90_000, 0, 5)],
        [out_ext(50_000, "p2pkh"), out_own(P, 1, 0, 39_500)]))
    vs.append(sign_vector("second seed, high address index", N,
        [In("n1", 70_000, 1, 17)],
        [out_ext(30_000, "p2wpkh"), out_own(N, 1, 18, 39_000)]))
    vs.append(sign_vector("foreign input is not signed", A,
        [In("f1", 60_000, 0, 2, nonwit=True), In("f2", 40_000, 0, 7, who="foreign", nonwit=True)],
        [out_ext(90_000, "p2wpkh"), out_own(A, 1, 3, 9_000)]))
    vs.append(sign_vector("outputs to P2TR, P2SH and OP_RETURN", A,
        [In("o1", 200_000, 0, 4)],
        [out_ext(50_000, "p2tr"), out_ext(40_000, "p2sh"), out_ext(0, "op_return"),
         out_own(A, 1, 4, 109_000)]))
    vs.append(sign_vector("receive-address output counts as own", A,
        [In("r1", 100_000, 0, 6)],
        [out_ext(40_000, "p2wpkh"), out_own(A, 0, 9, 30_000), out_own(A, 1, 5, 29_000)]))
    vs.append(sign_vector("fee above 10% of external outputs", A,
        [In("w1", 500_000, 0, 8)],
        [out_ext(100_000, "p2wpkh"), out_own(A, 1, 6, 300_000)]))
    vs.append(sign_vector("fee above 1000000 sat", A,
        [In("w2", 52_000_000, 0, 10)],
        [out_ext(50_000_000, "p2wpkh"), out_own(A, 1, 7, 800_000)]))
    vs.append(sign_vector("no external outputs, fee under the limit", A,
        [In("w3", 100_000, 0, 11)],
        [out_own(A, 1, 8, 90_000)]))
    vs.append(sign_vector("no external outputs, fee above 1000000 sat", A,
        [In("w4", 2_000_000, 0, 12)],
        [out_own(A, 1, 9, 400_000)]))

    # --- reject ---
    ok_in = lambda lbl: [In(lbl, 100_000)]
    ext = out_ext(60_000, "p2wpkh")

    # fake change: our fingerprint and path, somebody else's key in the script
    fake = {"value": 39_000, "spk": script.p2wpkh(FOREIGN.pub(1, 0)), "deriv": (1, 0), "kind": "change"}
    vs.append(reject_vector("fake change: our path, foreign key", A,
        build(A, ok_in("x1"), [ext, fake]).serialize(), "DS_ERR_CHANGE"))
    nested = {"value": 39_000, "spk": script.p2sh(script.p2wpkh(A.pub(1, 0))), "deriv": (1, 0), "kind": "change"}
    vs.append(reject_vector("change to P2SH-P2WPKH of our derivation", A,
        build(A, ok_in("x2"), [ext, nested]).serialize(), "DS_ERR_CHANGE"))
    vs.append(reject_vector("testnet path on a mainnet wallet", A,
        build(A, [In("x3", 100_000, 0, 0, coin=1)], [ext, out_ext(39_000, "p2wpkh")]).serialize(),
        "DS_ERR_NETWORK"))
    vs.append(reject_vector("SIGHASH_NONE", A,
        build(A, [In("x4", 100_000, sighash=2)], [ext, out_own(A, 1, 0, 39_000)]).serialize(),
        "DS_ERR_SIGHASH"))
    two = [In("x5a", 60_000, 0, 0), In("x5b", 50_000, 0, 1)]
    vs.append(reject_vector("two inputs without non_witness_utxo", A,
        build(A, two, [ext, out_own(A, 1, 0, 49_000)]).serialize(), "DS_ERR_AMOUNTS"))
    two = [In("x6a", 60_000, 0, 0, nonwit=True), In("x6b", 50_000, 0, 1, nonwit=True, nonwit_tamper=True)]
    vs.append(reject_vector("non_witness_utxo with a different txid", A,
        build(A, two, [ext, out_own(A, 1, 0, 49_000)]).serialize(), "DS_ERR_AMOUNTS"))
    two = [In("x7a", 60_000, 0, 0, nonwit=True), In("x7b", 50_000, 0, 1, nonwit=True, wit_value=49_000)]
    vs.append(reject_vector("witness_utxo amount differs from non_witness_utxo", A,
        build(A, two, [ext, out_own(A, 1, 0, 49_000)]).serialize(), "DS_ERR_AMOUNTS"))
    base = build(A, ok_in("x8"), [ext, out_own(A, 1, 0, 39_000)])
    vs.append(reject_vector("duplicate key in an input map", A,
        edited(base, duplicate_key), "DS_ERR_PSBT"))
    vs.append(reject_vector("PSBT version 2", A,
        edited(base, make_v2), "DS_ERR_UNSUPPORTED"))

    return {
        "format": "dtmf-signer-psbt",
        "version": 0,
        "note": "Public test seeds and testnet-style data only.",
        "vectors": vs,
    }


def render():
    return json.dumps(generate(), indent=1) + "\n"


if __name__ == "__main__":
    text = render()
    if "--check" in sys.argv:
        if OUT.read_text() != text:
            raise SystemExit("vectors/psbt-v0.json is out of date")
    else:
        OUT.write_text(text)
        print("wrote", OUT.relative_to(ROOT), len(json.loads(text)["vectors"]), "vectors")
