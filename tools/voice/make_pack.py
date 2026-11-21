#!/usr/bin/env python3
"""Builds the spoken-prompt packs of the telephone build.

    python3 tools/voice/make_pack.py --lang en --out-c app/voice/pack_en.c
    python3 tools/voice/make_pack.py --lang es --out-bin app/voice/packs/es.bin
    python3 tools/voice/make_pack.py --header app/voice/ds_prompts.h --names app/voice/ds_prompt_names.c

English is compiled into the firmware (a C array); every other language is a
separate pack written to the voice flash region (tools/voice/make_uf2.py).

Per phrase: Piper (a subprocess) renders a WAV per part, the text being cut
after each comma and full stop; each part is resampled to 8 kHz, equalised if
the voice asks for it (a bassy voice: 200 Hz high-pass, a gentle dip around
400 Hz), notched (see notch_dtmf) and trimmed of silence at its two ends only,
and the parts are joined with real silence (150 ms after a comma, 300 ms after
a full stop, on top of the part's own tail). The trim is gentle so soft endings
(a final vowel, f, s) survive: below -55 dBFS, 20 ms kept before and 110 ms
after. Then the peak is normalised to
-3 dBFS and the clip encoded as Speex narrowband (8000 Hz, fixed point,
quality 2, 5.95 kbit/s) by spxtool, built from tools/voice/spxtool.c with
the firmware's own libspeex (see spxtool_path). The pack layout ("DSV3",
little-endian):

    "DSV3" | lang (2 ASCII) | u16 format = 3 | u16 count | u32 total_len
    | u32 crc32 (IEEE, over the whole pack with this field as 0) | u32 grammar_off
    count x { u32 offset, u32 bytes }
    grammar | data

`offset` counts from the start of the pack. A clip is its Speex frames back
to back, each a length byte (1..62) and then that many bytes; a frame is
20 ms (160 samples), the last one completed with silence, and the frames end
exactly at `offset + bytes`. The decoder starts afresh at each clip. An empty
phrase has `bytes` 0. `count` must equal the firmware's prompt count.

The grammar block (from tools/voice/grammar.json) tells the firmware how to
say a number with the pack's clips:

    u16 small[100]                 clip for 0..99, 0xFFFF = build it from the scales
    u8 nscales (<= 8)              largest first, each value >= 10
    nscales x { u64 value, u16 singular, u16 plural (0xFFFF none), u8 say_one, u8 pad[5] }
    u16 nirr (<= 255)
    nirr x { u64 value, u16 clip, u8 kind, u8 pad[5] }

Irregular kinds: 1 = an exact multiple of a scale (es "cien", "treinta"),
2 = that multiple followed by more (es "ciento", "treinta y"), 3 = the form of
a small number said right before a scale word (es 1 -> "un", 21 -> "veintiún").
say_number() below is the reference of the algorithm in app/ds_pack.c.

The building and parsing of packs is pure Python; rendering needs the
tools/voice/requirements.txt packages (see README.md).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import urllib.request
import wave
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
RATE = 8000
THRESHOLD_DBFS = -55.0
PAD_MS = 20        # kept before the first sample above the threshold
TAIL_MS = 110      # kept after the last one (soft vowels, f, s fade out)
COMMA_MS = 150     # silence after a comma, semicolon or colon
STOP_MS = 300      # silence after a full stop, question or exclamation mark
PEAK_DBFS = -3.0
ENTRY = struct.Struct("<II")
MAX_FRAME = 62     # the largest narrowband Speex frame, in bytes
REGION_SIZE = 0x100000  # the voice flash region (1 MB) holds every pack but the built-in one


# -- Pack ---------------------------------------------------------------

MAGIC = b"DSV3"
FORMAT = 3
HEADER = struct.Struct("<4s2sHHIII")
NONE = 0xFFFF
MAX_SCALES = 8
MAX_IRREGULARS = 255
MAX_DEPTH = 16  # the nesting ds_pack_number allows
SCALE = struct.Struct("<QHHB5x")
IRREGULAR = struct.Struct("<QHB5x")


def encode_grammar(g: dict) -> bytes:
    """g: {"small": 100 clip ids (NONE where absent), "scales": [(value,
    singular, plural, say_one)] largest first, "irregulars": [(value, clip,
    kind)]}, all clip ids already numbers."""
    small = list(g["small"])
    assert len(small) == 100
    scales, irr = g["scales"], g["irregulars"]
    assert len(scales) <= MAX_SCALES and len(irr) <= MAX_IRREGULARS
    values = [s[0] for s in scales]
    assert all(v >= 10 for v in values) and values == sorted(set(values), reverse=True)
    assert all(k in (1, 2, 3) for _, _, k in irr)
    out = struct.pack("<100H", *small) + struct.pack("<B", len(scales))
    out += b"".join(SCALE.pack(v, sg, pl, int(one)) for v, sg, pl, one in scales)
    out += struct.pack("<H", len(irr))
    out += b"".join(IRREGULAR.pack(v, c, k) for v, c, k in irr)
    return out


def decode_grammar(b: bytes, off: int = 0) -> dict:
    small = list(struct.unpack_from("<100H", b, off))
    off += 200
    n = b[off]
    off += 1
    scales = []
    for _ in range(n):
        v, sg, pl, one = SCALE.unpack_from(b, off)
        scales.append((v, sg, pl, bool(one)))
        off += SCALE.size
    (m,) = struct.unpack_from("<H", b, off)
    off += 2
    irr = []
    for _ in range(m):
        irr.append(IRREGULAR.unpack_from(b, off))
        off += IRREGULAR.size
    return {"small": small, "scales": scales, "irregulars": irr}


def resolve_grammar(lang_grammar: dict, ids: dict) -> dict:
    """grammar.json's entry for one language, phrase IDs -> clip numbers."""
    small = [NONE] * 100
    for k, name in lang_grammar["small"].items():
        small[int(k)] = ids[name]
    scales = [(s["value"], ids[s["singular"]],
               NONE if s["plural"] is None else ids[s["plural"]], s["say_one"])
              for s in lang_grammar["scales"]]
    irr = [(r["value"], ids[r["clip"]], r["kind"]) for r in lang_grammar["irregulars"]]
    return {"small": small, "scales": scales, "irregulars": irr}


def say_number(g: dict, n: int) -> list[int]:
    """The clips that say n; the reference of ds_pack_number."""
    def irregular(value, kind):
        for v, c, k in g["irregulars"]:
            if v == value and k == kind:
                return c
        return None

    def say(n, before_scale, depth):
        # A multiple of the largest scale may need that scale again ("mil
        # millones"); the firmware caps the nesting, so the reference does too.
        if depth > MAX_DEPTH:
            raise ValueError("too deep")
        out = []
        for i in range(len(g["scales"])):
            if n < 100 and g["small"][n] != NONE:
                break
            value, singular, plural, say_one = g["scales"][i]
            q, r = divmod(n, value)
            if q == 0:
                continue
            c = irregular(q * value, 1 if r == 0 else 2)
            if c is not None:
                out.append(c)
            else:
                if q > 1 or say_one:
                    out += say(q, True, depth + 1)
                out.append(plural if q > 1 and plural != NONE else singular)
            n = r
        if n > 0 or not out:
            if n >= 100 or g["small"][n] == NONE:
                raise ValueError("no way to say %d" % n)
            c = irregular(n, 3) if before_scale else None
            out.append(g["small"][n] if c is None else c)
        return out

    return say(n, False, 0)


def split_frames(blob: bytes) -> list[bytes]:
    """A clip's frames (without their length bytes); ValueError unless the
    blob is whole frames of 1..MAX_FRAME bytes, as ds_pack_open requires."""
    frames, pos = [], 0
    while pos < len(blob):
        n = blob[pos]
        if not 1 <= n <= MAX_FRAME or pos + 1 + n > len(blob):
            raise ValueError("bad frame at byte %d" % pos)
        frames.append(blob[pos + 1:pos + 1 + n])
        pos += 1 + n
    return frames


def build_pack(lang: str, clips, grammar: bytes) -> bytes:
    """clips: one encoded clip (bytes, see encode_clip) per phrase, in
    phrases.json order; b"" for an empty phrase."""
    assert len(lang) == 2 and lang.isascii() and lang.islower()
    grammar_off = HEADER.size + len(clips) * ENTRY.size
    head = grammar_off + len(grammar)
    table, data = bytearray(), bytearray()
    for blob in clips:
        blob = bytes(blob)
        split_frames(blob)
        table += ENTRY.pack(head + len(data), len(blob))
        data += blob
    total = head + len(data)
    pack = bytearray(HEADER.pack(MAGIC, lang.encode("ascii"), FORMAT, len(clips), total, 0, grammar_off))
    pack += table + grammar + data
    struct.pack_into("<I", pack, 14, zlib.crc32(pack))
    return bytes(pack)


def pack_crc(pack: bytes) -> int:
    """The CRC-32 of the pack with its crc field as 0."""
    return zlib.crc32(pack[:14] + b"\0\0\0\0" + pack[18:])


def parse_pack(pack: bytes) -> dict:
    magic, lang, fmt, count, total, crc, goff = HEADER.unpack_from(pack, 0)
    if magic != MAGIC or fmt != FORMAT:
        raise ValueError("not a DSV3 pack")
    entries = []
    for i in range(count):
        offset, nbytes = ENTRY.unpack_from(pack, HEADER.size + i * ENTRY.size)
        if offset + nbytes > total:
            raise ValueError("clip %d past the end" % i)
        entries.append({"offset": offset, "bytes": nbytes,
                        "frames": len(split_frames(pack[offset:offset + nbytes]))})
    return {"lang": lang.decode("ascii"), "format": fmt, "count": count, "total_len": total,
            "crc32": crc, "crc_ok": crc == pack_crc(pack[:total]), "grammar_off": goff,
            "entries": entries, "grammar": decode_grammar(pack, goff)}


# -- Phrases and generated C --------------------------------------------

def load_phrases() -> dict:
    return json.loads((HERE / "phrases.json").read_text(encoding="utf-8"))


def lang_grammar(lang: str, phrases: dict) -> bytes:
    g = json.loads((HERE / "grammar.json").read_text(encoding="utf-8"))[lang]
    return encode_grammar(resolve_grammar(g, {k: i for i, k in enumerate(phrases)}))


def header_text(phrases: dict) -> str:
    lines = [
        "/* Generated by tools/voice/make_pack.py from tools/voice/phrases.json. */",
        "#ifndef DS_PROMPTS_H",
        "#define DS_PROMPTS_H",
        "",
        "#include <stdint.h>",
        "",
        "/* One prompt per entry, in the order of the packs. */",
        "enum ds_prompt {",
    ]
    lines += ["    DS_P_%s," % k.upper() for k in phrases]
    lines += [
        "    DS_P_COUNT",
        "};",
        "",
        "/* Each prompt's id as in phrases.json (lowercase), for the log of what the",
        " * device says; defined in the generated ds_prompt_names.c. */",
        "extern const char *const ds_prompt_name[DS_P_COUNT];",
        "",
        "/* The built-in English pack (\"DSV3\", see tools/voice/make_pack.py);",
        " * other languages are separate packs in the voice flash region. */",
        "extern const uint8_t ds_pack_en[];",
        "extern const uint32_t ds_pack_en_len;",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def names_c_text(phrases: dict) -> str:
    rows = ['    "%s",' % k for k in phrases]
    return (
        "/* Generated by tools/voice/make_pack.py from tools/voice/phrases.json (do not edit). */\n"
        "#include \"ds_prompts.h\"\n\n"
        "const char *const ds_prompt_name[DS_P_COUNT] = {\n%s\n};\n"
    ) % "\n".join(rows)


def pack_c_text(lang: str, pack: bytes, voice_name: str) -> str:
    rows = []
    for i in range(0, len(pack), 20):
        rows.append("    " + ", ".join("0x%02x" % b for b in pack[i:i + 20]) + ",")
    return (
        "/* Generated by tools/voice/make_pack.py (do not edit).\n"
        " * Voice: %s (Piper), prompts from tools/voice/phrases.json;\n"
        " * licences in app/voice/VOICES.md. Speex narrowband, 8000 Hz. */\n"
        "#include \"ds_prompts.h\"\n\n"
        "const uint8_t ds_pack_%s[] = {\n%s\n};\n\n"
        "const uint32_t ds_pack_%s_len = %d;\n"
    ) % (voice_name, lang, "\n".join(rows), lang, len(pack))


# -- Voices and rendering (the part that needs the venv) -----------------

def cache_dir() -> Path:
    return Path(os.environ.get("DTMF_VOICE_CACHE", Path.home() / ".cache" / "dtmf-signer" / "voices"))


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch_voice(lang: str) -> tuple[Path, dict]:
    voice = json.loads((HERE / "voices.json").read_text())["voices"][lang]
    cache = cache_dir()
    cache.mkdir(parents=True, exist_ok=True)
    for name, info in voice["files"].items():
        path = cache / name
        if not path.exists():
            print("downloading", name, file=sys.stderr)
            urllib.request.urlretrieve(info["url"], path)
        if sha256(path) != info["sha256"]:
            raise SystemExit("SHA-256 mismatch for %s; delete it from the cache" % path)
    return cache / (voice["name"] + ".onnx"), voice


def piper_wav(model: Path, text: str, out: Path, speaker=None) -> None:
    cmd = [sys.executable, "-m", "piper", "-m", str(model), "-f", str(out),
           "--noise-scale", "0", "--noise-w-scale", "0", "--sentence-silence", "0"]
    if speaker is not None:
        cmd += ["--speaker", str(speaker)]
    subprocess.run(
        cmd,
        input=text.encode("utf-8"), check=True, capture_output=True,
    )


def read_wav(path: Path):
    import numpy as np
    with wave.open(str(path), "rb") as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 1
        rate = w.getframerate()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
    return x, rate


def resample(x, rate_in: int, rate_out: int = RATE):
    """Windowed-sinc (Kaiser) resampler, low-passed below the new Nyquist."""
    import numpy as np
    if len(x) == 0:
        return x
    ratio = rate_in / rate_out
    cutoff = 0.45 * rate_out / rate_in          # fraction of the input rate
    half = int(np.ceil(8 / (2 * cutoff)))       # about 8 sinc lobes each side
    n_out = int(len(x) / ratio)
    pos = np.arange(n_out) * ratio
    base = np.floor(pos).astype(np.int64)
    taps = np.arange(-half + 1, half + 1)
    idx = base[:, None] + taps[None, :]
    t = idx - pos[:, None]
    win = np.i0(8.0 * np.sqrt(np.clip(1 - (t / half) ** 2, 0, 1))) / np.i0(8.0)
    h = 2 * cutoff * np.sinc(2 * cutoff * t) * win
    xp = np.concatenate([np.zeros(half), x, np.zeros(half + 1)])
    return (xp[np.clip(idx + half, 0, len(xp) - 1)] * h).sum(axis=1)


# The DTMF high group. A key is a low-group and a high-group tone together, so
# a clip with no energy at the four high-group frequencies can never sound
# like a key to a detector (speech otherwise does now and then). The notches
# are wide enough (Q 2.5) that Speex at 5.95 kbit/s, which models the spectrum
# coarsely, does not put a tone back there: at Q 4 a clip ("unit_sats", en)
# came out of the codec with a 20 ms '4', enough for the signer to take a key
# pressed right after it for data.
DTMF_HIGH = (1209.0, 1336.0, 1477.0, 1633.0)
NOTCH_Q = 2.5


def _notch(x, f0: float, q: float):
    """One biquad notch (RBJ), run forward and backward (zero phase, so the
    depth doubles); plain float arithmetic, so the same input gives the same
    output."""
    import math
    import numpy as np
    w0 = 2 * math.pi * f0 / RATE
    alpha = math.sin(w0) / (2 * q)
    a0 = 1 + alpha
    b = (1 / a0, -2 * math.cos(w0) / a0, 1 / a0)
    a1, a2 = -2 * math.cos(w0) / a0, (1 - alpha) / a0

    def run(v):
        out = np.empty_like(v)
        x1 = x2 = y1 = y2 = 0.0
        for i, xi in enumerate(v.tolist()):
            yi = b[0] * xi + b[1] * x1 + b[2] * x2 - a1 * y1 - a2 * y2
            out[i] = yi
            x2, x1, y2, y1 = x1, xi, y1, yi
        return out

    return run(run(x)[::-1])[::-1]


def notch_dtmf(x, q: float = NOTCH_Q):
    """Notches at the four high-group frequencies, on 8 kHz samples."""
    import numpy as np
    x = np.asarray(x, dtype=np.float64)
    for f0 in DTMF_HIGH:
        x = _notch(x, f0, q)
    return x


def trim_ends(x):
    """Cut the silence at the two ends only (never inside): below
    THRESHOLD_DBFS, keeping PAD_MS before and TAIL_MS after the speech (real
    samples where there are any, else zeros). None if it is all silence."""
    import numpy as np
    x = np.asarray(x, dtype=np.float64)
    keep = np.nonzero(np.abs(x) >= 10 ** (THRESHOLD_DBFS / 20))[0]
    if len(keep) == 0:
        return None
    lead, tail = RATE * PAD_MS // 1000, RATE * TAIL_MS // 1000
    a, b = max(0, keep[0] - lead), min(len(x), keep[-1] + 1 + tail)
    return np.concatenate([np.zeros(lead - (keep[0] - a)), x[a:b],
                           np.zeros(tail - (b - keep[-1] - 1))])


def _biquad(x, b, a):
    import numpy as np
    out = np.empty(len(x))
    x1 = x2 = y1 = y2 = 0.0
    for i, xi in enumerate(np.asarray(x, dtype=np.float64).tolist()):
        yi = b[0] * xi + b[1] * x1 + b[2] * x2 - a[0] * y1 - a[1] * y2
        out[i] = yi
        x2, x1, y2, y1 = x1, xi, y1, yi
    return out


def equalise(x):
    """For a bassy voice: RBJ 2nd-order high-pass at 200 Hz, then a peaking
    cut of 3 dB at 400 Hz (Q 0.7). Plain float arithmetic, deterministic."""
    import math
    w = 2 * math.pi * 200 / RATE
    al, c = math.sin(w) / (2 * 0.7071), math.cos(w)
    a0 = 1 + al
    x = _biquad(x, ((1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0),
                (-2 * c / a0, (1 - al) / a0))
    amp = 10 ** (-3 / 40)
    w = 2 * math.pi * 400 / RATE
    al, c = math.sin(w) / (2 * 0.7), math.cos(w)
    a0 = 1 + al / amp
    return _biquad(x, ((1 + al * amp) / a0, -2 * c / a0, (1 - al * amp) / a0),
                   (-2 * c / a0, (1 - al / amp) / a0))


def condition(x):
    """trim_ends, then peak to -3 dBFS, as int16 samples."""
    import numpy as np
    y = trim_ends(x)
    if y is None:
        return []
    y = y * (10 ** (PEAK_DBFS / 20) / np.abs(y).max())
    return [int(v) for v in np.clip(np.round(y * 32767), -32768, 32767)]


def split_text(text: str):
    """[(part, silence_ms after it)]: cut after , ; : . ? ! that end a word
    (not inside "1.5" or "ur-tones"); the last part gets no silence."""
    parts = [(p.strip(), STOP_MS if p.rstrip()[-1] in ".?!" else COMMA_MS)
             for p in re.split(r"(?<=[,;:.?!])\s+", text.strip()) if p.strip()]
    if parts:
        parts[-1] = (parts[-1][0], 0)
    return parts


def render_text(model, text: str, wav: Path, speaker=None, eq=False):
    import numpy as np
    out = []
    for part, gap in split_text(text):
        piper_wav(model, part, wav, speaker)
        x, rate = read_wav(wav)
        x = resample(x, rate)
        y = trim_ends(notch_dtmf(equalise(x) if eq else x))
        if y is not None:
            out.append(y)
            out.append(np.zeros(RATE * gap // 1000))
    return np.concatenate(out) if out else np.zeros(0)


def spxtool_path() -> Path:
    """Builds spxtool (CMake target, host compiler, Release) in
    build/voice-tools and returns its path; the same command every time, so
    the encoding is the firmware's own fixed-point libspeex, never a system
    speexenc."""
    out = ROOT / "build" / "voice-tools"
    subprocess.run(["cmake", "-S", str(ROOT), "-B", str(out), "-DCMAKE_BUILD_TYPE=Release"],
                   check=True, capture_output=True)
    subprocess.run(["cmake", "--build", str(out), "--target", "spxtool"],
                   check=True, capture_output=True)
    return out / "spxtool"


def encode_clip(pcm, spxtool: Path, tmp: Path) -> bytes:
    """int16 samples at 8 kHz -> the clip's Speex frames (spxtool encode)."""
    if not len(pcm):
        return b""
    wav, spx = tmp / "c.wav", tmp / "c.spx"
    with wave.open(str(wav), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack("<%dh" % len(pcm), *pcm))
    subprocess.run([str(spxtool), "encode", str(wav), str(spx)], check=True)
    blob = spx.read_bytes()
    split_frames(blob)
    return blob


def render_clips(lang: str, phrases: dict, spxtool: Path):
    """The encoded clips and the voice's name; also the seconds of speech."""
    model, voice = fetch_voice(lang)
    clips, samples = [], 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        wav = tmp / "p.wav"
        for key, text in ((k, v[lang]) for k, v in phrases.items()):
            if not text:
                clips.append(b"")
                continue
            pcm = condition(render_text(model, text, wav, voice.get("speaker"), voice.get("eq", False)))
            samples += len(pcm)
            clips.append(encode_clip(pcm, spxtool, tmp))
    return clips, voice["name"], samples / RATE


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--lang", choices=["es", "en"])
    ap.add_argument("--out-c", help="write the pack as a C file (with --lang)")
    ap.add_argument("--out-bin", help="write the raw pack (with --lang)")
    ap.add_argument("--header", help="write ds_prompts.h")
    ap.add_argument("--names", help="write ds_prompt_names.c")
    ap.add_argument("--spxtool", help="the spxtool to encode with (default: build it, see spxtool_path)")
    a = ap.parse_args()
    phrases = load_phrases()
    if a.header:
        Path(a.header).write_text(header_text(phrases), encoding="utf-8")
    if a.names:
        Path(a.names).write_text(names_c_text(phrases), encoding="utf-8")
    if a.out_c or a.out_bin:
        if not a.lang:
            ap.error("--out-c/--out-bin need --lang")
        spxtool = Path(a.spxtool) if a.spxtool else spxtool_path()
        clips, name, secs = render_clips(a.lang, phrases, spxtool)
        pack = build_pack(a.lang, clips, lang_grammar(a.lang, phrases))
        print("%s: %d bytes, %.1f s of speech, %d phrases" % (a.lang, len(pack), secs, len(clips)))
        if len(pack) > REGION_SIZE:
            raise SystemExit("%s: the pack is larger than the voice region (%d bytes)" % (a.lang, REGION_SIZE))
        if a.out_c:
            Path(a.out_c).write_text(pack_c_text(a.lang, pack, name), encoding="utf-8")
        if a.out_bin:
            Path(a.out_bin).write_bytes(pack)
    return 0


if __name__ == "__main__":
    sys.exit(main())
