/* RIPEMD-160 (Dobbertin, Bosselaers, Preneel) and HASH160. */
#include "ds_core.h"
#include "ds_hash.h"

#include <string.h>

#include "ur_tones.h"

#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static const uint8_t RL[80] UT_ROM = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    7, 4, 13, 1, 10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8,
    3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0, 6, 13, 11, 5, 12,
    1, 9, 11, 10, 0, 8, 12, 4, 13, 3, 7, 15, 14, 5, 6, 2,
    4, 0, 5, 9, 7, 12, 2, 10, 14, 1, 3, 8, 11, 6, 15, 13};
static const uint8_t RR[80] UT_ROM = {
    5, 14, 7, 0, 9, 2, 11, 4, 13, 6, 15, 8, 1, 10, 3, 12,
    6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8, 12, 4, 9, 1, 2,
    15, 5, 1, 3, 7, 14, 6, 9, 11, 8, 12, 2, 10, 0, 4, 13,
    8, 6, 4, 1, 3, 11, 15, 0, 5, 12, 2, 13, 9, 7, 10, 14,
    12, 15, 10, 4, 1, 5, 8, 7, 6, 2, 13, 14, 0, 3, 9, 11};
static const uint8_t SL[80] UT_ROM = {
    11, 14, 15, 12, 5, 8, 7, 9, 11, 13, 14, 15, 6, 7, 9, 8,
    7, 6, 8, 13, 11, 9, 7, 15, 7, 12, 15, 9, 11, 7, 13, 12,
    11, 13, 6, 7, 14, 9, 13, 15, 14, 8, 13, 6, 5, 12, 7, 5,
    11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6, 8, 6, 5, 12,
    9, 15, 5, 11, 6, 8, 13, 12, 5, 12, 13, 14, 11, 8, 5, 6};
static const uint8_t SR[80] UT_ROM = {
    8, 9, 9, 11, 13, 15, 15, 5, 7, 7, 8, 11, 14, 14, 12, 6,
    9, 13, 15, 7, 12, 8, 9, 11, 7, 7, 12, 7, 6, 15, 13, 11,
    9, 7, 15, 11, 8, 6, 6, 14, 12, 13, 5, 14, 13, 13, 7, 5,
    15, 5, 8, 11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5, 15, 8,
    8, 5, 12, 9, 12, 5, 14, 6, 8, 13, 6, 5, 15, 13, 11, 11};
static const uint32_t KL[5] UT_ROM = {0x00000000u, 0x5a827999u, 0x6ed9eba1u,
                               0x8f1bbcdcu, 0xa953fd4eu};
static const uint32_t KR[5] UT_ROM = {0x50a28be6u, 0x5c4dd124u, 0x6d703ef3u,
                               0x7a6d76e9u, 0x00000000u};

static uint32_t f(int round, uint32_t x, uint32_t y, uint32_t z)
{
    switch (round) {
    case 0: return x ^ y ^ z;
    case 1: return (x & y) | (~x & z);
    case 2: return (x | ~y) ^ z;
    case 3: return (x & z) | (y & ~z);
    default: return x ^ (y | ~z);
    }
}

static void compress(uint32_t h[5], const uint8_t blk[64])
{
    uint32_t x[16];
    uint32_t al = h[0], bl = h[1], cl = h[2], dl = h[3], el = h[4];
    uint32_t ar = al, br = bl, cr = cl, dr = dl, er = el;
    uint32_t t;
    int j;

    for (j = 0; j < 16; j++) {
        x[j] = (uint32_t)blk[4 * j] | ((uint32_t)blk[4 * j + 1] << 8) |
               ((uint32_t)blk[4 * j + 2] << 16) | ((uint32_t)blk[4 * j + 3] << 24);
    }
    for (j = 0; j < 80; j++) {
        int r = j / 16;
        t = al + f(r, bl, cl, dl) + x[UT_ROM_U8(&RL[j])] + UT_ROM_U32(&KL[r]);
        t = ROL32(t, UT_ROM_U8(&SL[j])) + el;
        al = el; el = dl; dl = ROL32(cl, 10); cl = bl; bl = t;
        t = ar + f(4 - r, br, cr, dr) + x[UT_ROM_U8(&RR[j])] + UT_ROM_U32(&KR[r]);
        t = ROL32(t, UT_ROM_U8(&SR[j])) + er;
        ar = er; er = dr; dr = ROL32(cr, 10); cr = br; br = t;
    }
    t = h[1] + cl + dr;
    h[1] = h[2] + dl + er;
    h[2] = h[3] + el + ar;
    h[3] = h[4] + al + br;
    h[4] = h[0] + bl + cr;
    h[0] = t;
    ds_wipe(x, sizeof x);
}

void ds_ripemd160(const uint8_t *d, size_t n, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u,
                     0xc3d2e1f0u};
    uint8_t blk[64];
    uint64_t bits = (uint64_t)n << 3;
    size_t rem = n;
    int i;

    while (rem >= 64) {
        compress(h, d);
        d += 64;
        rem -= 64;
    }
    memset(blk, 0, sizeof blk);
    memcpy(blk, d, rem);
    blk[rem] = 0x80;
    if (rem >= 56) {
        compress(h, blk);
        memset(blk, 0, sizeof blk);
    }
    for (i = 0; i < 8; i++) {
        blk[56 + i] = (uint8_t)(bits >> (8 * i));
    }
    compress(h, blk);
    for (i = 0; i < 20; i++) {
        out[i] = (uint8_t)(h[i / 4] >> (8 * (i % 4)));
    }
    ds_wipe(blk, sizeof blk);
    ds_wipe(h, sizeof h);
}

void ds_hash160(const uint8_t *d, size_t n, uint8_t out[20])
{
    uint8_t sha[32];
    ut_sha256(d, n, sha);
    ds_ripemd160(sha, sizeof sha, out);
    ds_wipe(sha, sizeof sha);
}
