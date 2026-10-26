/* SHA-256 (FIPS 180-4), incremental, and the compact-size integer. */
#include "ds_bitcoin.h"

#include <string.h>

#include "ur_tones.h"

static const uint32_t K256[64] UT_ROM = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static const uint32_t H256[8] UT_ROM = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void compress(uint32_t h[8], const uint8_t blk[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, hh;
    int i;

    for (i = 0; i < 16; i++) {
        int j = 4 * i;
        w[i] = (uint32_t)blk[j] << 24 | (uint32_t)blk[j + 1] << 16 |
               (uint32_t)blk[j + 2] << 8 | (uint32_t)blk[j + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++) {
        uint32_t S1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + UT_ROM_U32(&K256[i]) + w[i];
        uint32_t S0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    ds_wipe(w, sizeof w);
}

void ds_sha256_init(ds_sha256_ctx *c)
{
    int i;
    for (i = 0; i < 8; i++) {
        c->h[i] = UT_ROM_U32(&H256[i]);
    }
    c->len = 0;
    c->fill = 0;
    memset(c->buf, 0, sizeof c->buf);
}

void ds_sha256_update(ds_sha256_ctx *c, const uint8_t *d, size_t n)
{
    c->len += n;
    while (n > 0) {
        size_t take = 64 - c->fill;
        if (take > n) {
            take = n;
        }
        memcpy(c->buf + c->fill, d, take);
        c->fill += (uint32_t)take;
        d += take;
        n -= take;
        if (c->fill == 64) {
            compress(c->h, c->buf);
            c->fill = 0;
        }
    }
}

void ds_sha256_final(ds_sha256_ctx *c, uint8_t out[32])
{
    uint64_t bits = c->len << 3;
    int i;

    c->buf[c->fill++] = 0x80;
    if (c->fill > 56) {
        memset(c->buf + c->fill, 0, 64 - c->fill);
        compress(c->h, c->buf);
        c->fill = 0;
    }
    memset(c->buf + c->fill, 0, 56 - c->fill);
    for (i = 0; i < 8; i++) {
        c->buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    compress(c->h, c->buf);
    for (i = 0; i < 32; i++) {
        out[i] = (uint8_t)(c->h[i / 4] >> (24 - 8 * (i % 4)));
    }
    ds_wipe(c, sizeof *c);
}

void ds_sha256d(const uint8_t *d, size_t n, uint8_t out[32])
{
    ds_sha256_ctx c;
    uint8_t t[32];

    ds_sha256_init(&c);
    ds_sha256_update(&c, d, n);
    ds_sha256_final(&c, t);
    ds_sha256_init(&c);
    ds_sha256_update(&c, t, 32);
    ds_sha256_final(&c, out);
    ds_wipe(t, sizeof t);
}

int32_t ds_compact_decode(const uint8_t *p, size_t n, uint64_t *value)
{
    size_t len, i;
    uint64_t v = 0;

    if (n < 1) {
        return DS_ERR_PSBT;
    }
    if (p[0] < 0xfd) {
        *value = p[0];
        return 1;
    }
    len = p[0] == 0xfd ? 2 : p[0] == 0xfe ? 4 : 8;
    if (n < 1 + len) {
        return DS_ERR_PSBT;
    }
    for (i = 0; i < len; i++) {
        v |= (uint64_t)p[1 + i] << (8 * i);
    }
    *value = v;
    return (int32_t)(1 + len);
}

size_t ds_compact_encode(uint64_t v, uint8_t out[9])
{
    size_t len, i;

    if (v < 0xfd) {
        out[0] = (uint8_t)v;
        return 1;
    }
    len = v <= 0xffffu ? 2 : v <= 0xffffffffu ? 4 : 8;
    out[0] = len == 2 ? 0xfd : len == 4 ? 0xfe : 0xff;
    for (i = 0; i < len; i++) {
        out[1 + i] = (uint8_t)(v >> (8 * i));
    }
    return 1 + len;
}
