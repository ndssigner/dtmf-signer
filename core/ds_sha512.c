/* SHA-512 (FIPS 180-4), HMAC (RFC 2104) and PBKDF2 (RFC 8018). */
#include "ds_core.h"
#include "ds_hash.h"

#include <string.h>

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
    0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
    0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
    0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
    0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
    0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
    0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
    0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
    0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
    0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
    0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static const uint64_t H512[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

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
        uint64_t t1 = hh + S1 + ch + K512[i] + w[i];
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
    memcpy(c->h, H512, sizeof H512);
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
