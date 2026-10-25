/* SHA-512 (FIPS 180-4), HMAC (RFC 2104) and PBKDF2 (RFC 8018). */
#include "ds_core.h"
#include "ds_hash.h"

#include <string.h>

#include "ur_tones.h"

/* 64-bit constants stored as (high, low) 32-bit pairs: ur-tones has no
 * 64-bit ROM read, so rom64() rebuilds them from two UT_ROM_U32 reads. */
static const uint32_t K512[160] UT_ROM = {
    0x428a2f98u, 0xd728ae22u, 0x71374491u, 0x23ef65cdu,
    0xb5c0fbcfu, 0xec4d3b2fu, 0xe9b5dba5u, 0x8189dbbcu,
    0x3956c25bu, 0xf348b538u, 0x59f111f1u, 0xb605d019u,
    0x923f82a4u, 0xaf194f9bu, 0xab1c5ed5u, 0xda6d8118u,
    0xd807aa98u, 0xa3030242u, 0x12835b01u, 0x45706fbeu,
    0x243185beu, 0x4ee4b28cu, 0x550c7dc3u, 0xd5ffb4e2u,
    0x72be5d74u, 0xf27b896fu, 0x80deb1feu, 0x3b1696b1u,
    0x9bdc06a7u, 0x25c71235u, 0xc19bf174u, 0xcf692694u,
    0xe49b69c1u, 0x9ef14ad2u, 0xefbe4786u, 0x384f25e3u,
    0x0fc19dc6u, 0x8b8cd5b5u, 0x240ca1ccu, 0x77ac9c65u,
    0x2de92c6fu, 0x592b0275u, 0x4a7484aau, 0x6ea6e483u,
    0x5cb0a9dcu, 0xbd41fbd4u, 0x76f988dau, 0x831153b5u,
    0x983e5152u, 0xee66dfabu, 0xa831c66du, 0x2db43210u,
    0xb00327c8u, 0x98fb213fu, 0xbf597fc7u, 0xbeef0ee4u,
    0xc6e00bf3u, 0x3da88fc2u, 0xd5a79147u, 0x930aa725u,
    0x06ca6351u, 0xe003826fu, 0x14292967u, 0x0a0e6e70u,
    0x27b70a85u, 0x46d22ffcu, 0x2e1b2138u, 0x5c26c926u,
    0x4d2c6dfcu, 0x5ac42aedu, 0x53380d13u, 0x9d95b3dfu,
    0x650a7354u, 0x8baf63deu, 0x766a0abbu, 0x3c77b2a8u,
    0x81c2c92eu, 0x47edaee6u, 0x92722c85u, 0x1482353bu,
    0xa2bfe8a1u, 0x4cf10364u, 0xa81a664bu, 0xbc423001u,
    0xc24b8b70u, 0xd0f89791u, 0xc76c51a3u, 0x0654be30u,
    0xd192e819u, 0xd6ef5218u, 0xd6990624u, 0x5565a910u,
    0xf40e3585u, 0x5771202au, 0x106aa070u, 0x32bbd1b8u,
    0x19a4c116u, 0xb8d2d0c8u, 0x1e376c08u, 0x5141ab53u,
    0x2748774cu, 0xdf8eeb99u, 0x34b0bcb5u, 0xe19b48a8u,
    0x391c0cb3u, 0xc5c95a63u, 0x4ed8aa4au, 0xe3418acbu,
    0x5b9cca4fu, 0x7763e373u, 0x682e6ff3u, 0xd6b2b8a3u,
    0x748f82eeu, 0x5defb2fcu, 0x78a5636fu, 0x43172f60u,
    0x84c87814u, 0xa1f0ab72u, 0x8cc70208u, 0x1a6439ecu,
    0x90befffau, 0x23631e28u, 0xa4506cebu, 0xde82bde9u,
    0xbef9a3f7u, 0xb2c67915u, 0xc67178f2u, 0xe372532bu,
    0xca273eceu, 0xea26619cu, 0xd186b8c7u, 0x21c0c207u,
    0xeada7dd6u, 0xcde0eb1eu, 0xf57d4f7fu, 0xee6ed178u,
    0x06f067aau, 0x72176fbau, 0x0a637dc5u, 0xa2c898a6u,
    0x113f9804u, 0xbef90daeu, 0x1b710b35u, 0x131c471bu,
    0x28db77f5u, 0x23047d84u, 0x32caab7bu, 0x40c72493u,
    0x3c9ebe0au, 0x15c9bebcu, 0x431d67c4u, 0x9c100d4cu,
    0x4cc5d4beu, 0xcb3e42b6u, 0x597f299cu, 0xfc657e2au,
    0x5fcb6fabu, 0x3ad6faecu, 0x6c44198cu, 0x4a475817u,
};

static const uint32_t H512[16] UT_ROM = {
    0x6a09e667u, 0xf3bcc908u, 0xbb67ae85u, 0x84caa73bu,
    0x3c6ef372u, 0xfe94f82bu, 0xa54ff53au, 0x5f1d36f1u,
    0x510e527fu, 0xade682d1u, 0x9b05688cu, 0x2b3e6c1fu,
    0x1f83d9abu, 0xfb41bd6bu, 0x5be0cd19u, 0x137e2179u,
};

static uint64_t rom64(const uint32_t *p)
{
    return ((uint64_t)UT_ROM_U32(p) << 32) | UT_ROM_U32(p + 1);
}

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static void compress(uint64_t h[8], const uint8_t blk[128])
{
    uint64_t w[80];
    uint64_t a, b, c, d, e, f, g, hh;
    int i;

    for (i = 0; i < 16; i++) {
        int j = 8 * i;
        w[i] = ((uint64_t)blk[j] << 56) | ((uint64_t)blk[j + 1] << 48) |
               ((uint64_t)blk[j + 2] << 40) | ((uint64_t)blk[j + 3] << 32) |
               ((uint64_t)blk[j + 4] << 24) | ((uint64_t)blk[j + 5] << 16) |
               ((uint64_t)blk[j + 6] << 8) | (uint64_t)blk[j + 7];
    }
    for (i = 16; i < 80; i++) {
        uint64_t s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 80; i++) {
        uint64_t S1 = ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = hh + S1 + ch + rom64(&K512[2 * i]) + w[i];
        uint64_t S0 = ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39);
        uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    ds_wipe(w, sizeof w);
}

void ds_sha512_init(ds_sha512_ctx *c)
{
    int i;
    for (i = 0; i < 8; i++) {
        c->h[i] = rom64(&H512[2 * i]);
    }
    c->len = 0;
    c->fill = 0;
    memset(c->buf, 0, sizeof c->buf);
}

void ds_sha512_update(ds_sha512_ctx *c, const uint8_t *d, size_t n)
{
    c->len += n;
    while (n > 0) {
        size_t take = 128 - c->fill;
        if (take > n) {
            take = n;
        }
        memcpy(c->buf + c->fill, d, take);
        c->fill += (uint32_t)take;
        d += take;
        n -= take;
        if (c->fill == 128) {
            compress(c->h, c->buf);
            c->fill = 0;
        }
    }
}

void ds_sha512_final(ds_sha512_ctx *c, uint8_t out[64])
{
    uint64_t bits_hi = c->len >> 61, bits_lo = c->len << 3;
    int i;

    c->buf[c->fill++] = 0x80;
    if (c->fill > 112) {
        memset(c->buf + c->fill, 0, 128 - c->fill);
        compress(c->h, c->buf);
        c->fill = 0;
    }
    memset(c->buf + c->fill, 0, 112 - c->fill);
    for (i = 0; i < 8; i++) {
        c->buf[112 + i] = (uint8_t)(bits_hi >> (56 - 8 * i));
        c->buf[120 + i] = (uint8_t)(bits_lo >> (56 - 8 * i));
    }
    compress(c->h, c->buf);
    for (i = 0; i < 64; i++) {
        out[i] = (uint8_t)(c->h[i / 8] >> (56 - 8 * (i % 8)));
    }
    ds_wipe(c, sizeof *c);
}

void ds_sha512(const uint8_t *d, size_t n, uint8_t out[64])
{
    ds_sha512_ctx c;
    ds_sha512_init(&c);
    ds_sha512_update(&c, d, n);
    ds_sha512_final(&c, out);
}

void ds_hmac_sha512(const uint8_t *key, size_t klen, const uint8_t *msg,
                    size_t mlen, uint8_t out[64])
{
    uint8_t pad[128];
    uint8_t kh[64];
    uint8_t inner[64];
    ds_sha512_ctx c;
    size_t i;

    if (klen > 128) {
        ds_sha512(key, klen, kh);
        key = kh;
        klen = 64;
    }
    memset(pad, 0x36, sizeof pad);
    for (i = 0; i < klen; i++) {
        pad[i] ^= key[i];
    }
    ds_sha512_init(&c);
    ds_sha512_update(&c, pad, sizeof pad);
    ds_sha512_update(&c, msg, mlen);
    ds_sha512_final(&c, inner);

    memset(pad, 0x5c, sizeof pad);
    for (i = 0; i < klen; i++) {
        pad[i] ^= key[i];
    }
    ds_sha512_init(&c);
    ds_sha512_update(&c, pad, sizeof pad);
    ds_sha512_update(&c, inner, sizeof inner);
    ds_sha512_final(&c, out);

    ds_wipe(pad, sizeof pad);
    ds_wipe(kh, sizeof kh);
    ds_wipe(inner, sizeof inner);
}

void ds_pbkdf2_hmac_sha512(const uint8_t *pw, size_t pwlen,
                           const uint8_t *salt, size_t saltlen,
                           uint32_t iterations, uint8_t *out, size_t outlen)
{
    uint8_t u[64], t[64];
    ds_sha512_ctx c;
    uint8_t ipad[128], opad[128], kh[64];
    uint32_t block = 1;
    size_t i;

    /* HMAC pads are built once; the key is the password. */
    if (pwlen > 128) {
        ds_sha512(pw, pwlen, kh);
        pw = kh;
        pwlen = 64;
    }
    memset(ipad, 0x36, sizeof ipad);
    memset(opad, 0x5c, sizeof opad);
    for (i = 0; i < pwlen; i++) {
        ipad[i] ^= pw[i];
        opad[i] ^= pw[i];
    }
    ds_wipe(kh, sizeof kh);

    while (outlen > 0) {
        uint8_t ctr[4];
        uint32_t it;
        size_t take, k;

        ctr[0] = (uint8_t)(block >> 24);
        ctr[1] = (uint8_t)(block >> 16);
        ctr[2] = (uint8_t)(block >> 8);
        ctr[3] = (uint8_t)block;

        ds_sha512_init(&c);
        ds_sha512_update(&c, ipad, sizeof ipad);
        ds_sha512_update(&c, salt, saltlen);
        ds_sha512_update(&c, ctr, 4);
        ds_sha512_final(&c, u);
        ds_sha512_init(&c);
        ds_sha512_update(&c, opad, sizeof opad);
        ds_sha512_update(&c, u, 64);
        ds_sha512_final(&c, u);
        memcpy(t, u, 64);

        for (it = 1; it < iterations; it++) {
            ds_sha512_init(&c);
            ds_sha512_update(&c, ipad, sizeof ipad);
            ds_sha512_update(&c, u, 64);
            ds_sha512_final(&c, u);
            ds_sha512_init(&c);
            ds_sha512_update(&c, opad, sizeof opad);
            ds_sha512_update(&c, u, 64);
            ds_sha512_final(&c, u);
            for (k = 0; k < 64; k++) {
                t[k] ^= u[k];
            }
        }
        take = outlen < 64 ? outlen : 64;
        memcpy(out, t, take);
        out += take;
        outlen -= take;
        block++;
    }
    ds_wipe(u, sizeof u);
    ds_wipe(t, sizeof t);
    ds_wipe(ipad, sizeof ipad);
    ds_wipe(opad, sizeof opad);
    ds_wipe(&c, sizeof c);
}
