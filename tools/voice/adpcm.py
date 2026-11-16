"""IMA ADPCM (4 bits per sample), the standard step and index tables.

Two samples per byte, the first sample in the low nibble. The decoder state
is (predictor, index); both start where the caller says (the pack stores
them per clip). Pure Python, no dependencies.
"""

from __future__ import annotations

import math

STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767,
]
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8]


def _clamp(x: int, lo: int, hi: int) -> int:
    return lo if x < lo else hi if x > hi else x


def _step(pred: int, index: int, code: int) -> tuple[int, int]:
    """Advance the decoder state by one code; return (pred, index)."""
    step = STEP_TABLE[index]
    vpdiff = step >> 3
    if code & 4:
        vpdiff += step
    if code & 2:
        vpdiff += step >> 1
    if code & 1:
        vpdiff += step >> 2
    pred = _clamp(pred - vpdiff if code & 8 else pred + vpdiff, -32768, 32767)
    index = _clamp(index + INDEX_TABLE[code & 7], 0, 88)
    return pred, index


def encode(pcm, pred: int = 0, index: int = 0) -> bytes:
    """Encode int16 samples; an odd count is padded by repeating the last."""
    samples = list(pcm)
    if len(samples) % 2:
        samples.append(samples[-1])
    out = bytearray()
    nib = []
    for s in samples:
        diff = s - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        step = STEP_TABLE[index]
        if diff >= step:
            code |= 4
            diff -= step
        step >>= 1
        if diff >= step:
            code |= 2
            diff -= step
        step >>= 1
        if diff >= step:
            code |= 1
        pred, index = _step(pred, index, code)
        nib.append(code)
        if len(nib) == 2:
            out.append(nib[0] | (nib[1] << 4))
            nib = []
    return bytes(out)


def decode(data: bytes, pred: int = 0, index: int = 0) -> list[int]:
    out = []
    for b in data:
        for code in (b & 15, b >> 4):
            pred, index = _step(pred, index, code)
            out.append(pred)
    return out


def sample_pcm() -> list[int]:
    """The fixed 200-sample input of vectors/adpcm-v0.json: 440 Hz plus
    1000 Hz at 8 kHz, peak amplitude 8000."""
    return [
        round(4000 * (math.sin(2 * math.pi * 440 * n / 8000)
                      + math.sin(2 * math.pi * 1000 * n / 8000)))
        for n in range(200)
    ]


def sample_vector() -> dict:
    pcm = sample_pcm()
    data = encode(pcm, 0, 0)
    return {
        "pcm": pcm,
        "pred": 0,
        "index": 0,
        "adpcm_hex": data.hex(),
        "decoded": decode(data, 0, 0),
    }
