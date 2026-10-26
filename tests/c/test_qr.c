#include <stdint.h>
#include <string.h>

#include "ds_psbt.h"
#include "ds_qr.h"
#include "ds_rx.h"
#include "psbt_vectors.h"
#include "test.h"

/* A ds_qr holds a fountain encoder (about 3 KB) and the decoder work area is
 * large: everything is static. The encoder keeps a pointer to the message. */
static ds_qr q;
static ut_fountain_decoder dec;
static uint8_t work[UT_FOUNTAIN_WORK(DS_APP_PSBT_MAX)];
static uint8_t msg[2048];
static uint8_t big[DS_APP_PSBT_MAX + 4096];
static uint8_t fb[8][128];

static size_t vector_cbor(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof psbt_vectors / sizeof psbt_vectors[0]; i++) {
        if (strcmp(psbt_vectors[i].name, name) == 0) {
            int32_t n = ut_psbt_to_cbor(psbt_vectors[i].psbt, psbt_vectors[i].psbt_len,
                                        msg, sizeof msg);
            CHECK(n > 0);
            return n > 0 ? (size_t)n : 0;
        }
    }
    CHECK(0);
    return 0;
}

/* A message of 300 bytes that reaches scale 2: the shortest fragment
 * ur-tones makes is 10 bytes, and version 3 holds 77 characters, which the
 * highest part sent (W = 70) of 10-byte fragments just fills (part 1 is 74).
 * 300 bytes is 30 parts of 10. */
static uint8_t synth[300];

static size_t synthetic(void)
{
    size_t i;
    for (i = 0; i < sizeof synth; i++) {
        synth[i] = (uint8_t)(i * 31 + 7);
    }
    return sizeof synth;
}

static void lower(char *s)
{
    for (; *s; s++) {
        if (*s >= 'A' && *s <= 'Z') {
            *s = (char)(*s - 'A' + 'a');
        }
    }
}

/* Starts at the given scale, sends seq_len + 3 parts through a decoder and
 * checks the message and the version of every part. */
static void roundtrip(const uint8_t *m, size_t len, int scale, int effective,
                      int max_version)
{
    size_t i;
    const char *type = NULL;
    const uint8_t *out = NULL;
    size_t olen = 0;
    int32_t r = 0, n;

    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", m, len, scale), effective);
    CHECK(ut_fountain_decoder_init(&dec, work, sizeof work) >= 0);
    for (i = 0; i < q.e.seq_len + 3u; i++) {
        n = ds_qr_next(&q);
        CHECK(n >= 21 && n <= 17 + 4 * max_version);
        CHECK(((n - 17) / 4) <= max_version);
        CHECK(strchr(q.ur, 'a') == NULL);
        lower(q.ur);
        r = ut_fountain_receive(&dec, q.ur);
        CHECK(r >= 0);
    }
    CHECK_EQ_INT(r, 1);
    /* Keep going well past the highest part number W and through the wrap:
     * every part must still be a QR code of the allowed version. */
    for (i = 0; i < 400; i++) {
        n = ds_qr_next(&q);
        CHECK(n >= 21 && (n - 17) / 4 <= max_version);
        CHECK(q.e.seq_num <= 2 * q.e.seq_len + 10);
    }
    CHECK(ut_fountain_result(&dec, &type, &out, &olen) >= 0);
    CHECK_EQ_INT(olen, len);
    CHECK(olen == len && memcmp(out, m, len) == 0);
}

static void test_qr_roundtrip_scale2(void)
{
    roundtrip(synth, synthetic(), 2, 2, 3);
}

static void test_qr_scale1_fits_v7(void)
{
    size_t len = vector_cbor("two inputs with non_witness_utxo");
    roundtrip(msg, len, 1, 1, 7);
    len = vector_cbor("one input with change");
    roundtrip(msg, len, 1, 1, 7);
}

/* Messages whose parts grow with the sequence number: 1500 bytes (fragment
 * 79, 19 parts, W 48), 2400 (80, 30, W 70) and 6000 (82, 74, W 158). */
static void test_qr_long_run(void)
{
    static const size_t lens[] = {1500, 2400, 6000};
    size_t i, k;
    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        for (k = 0; k < sizeof synth; k++) {
            big[k] = (uint8_t)(k * 13 + i);
        }
        for (; k < lens[i]; k++) {
            big[k] = (uint8_t)(k * 7 + (k >> 8));
        }
        roundtrip(big, lens[i], 1, 1, 7);
    }
}

/* These two PSBT vectors (282 and 693 bytes) cannot split into 10-byte
 * fragments: their shortest is 11 bytes, whose part W is 79 or 80 characters,
 * over version 3 (77), so a request for scale 2 gives scale 1. */
static void test_qr_psbt_needs_scale1(void)
{
    size_t len = vector_cbor("two inputs with non_witness_utxo");
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", msg, len, 2), 1);
    len = vector_cbor("one input with change");
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", msg, len, 2), 1);
}

static void test_qr_args(void)
{
    size_t len = vector_cbor("two inputs with non_witness_utxo");
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", msg, len, 3), DS_ERR_ARG);
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", msg, len, 0), DS_ERR_ARG);
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", msg, 0, 2), DS_ERR_ARG);
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", NULL, len, 2), DS_ERR_ARG);
    CHECK_EQ_INT(ds_qr_start(&q, NULL, msg, len, 2), DS_ERR_ARG);
    CHECK_EQ_INT(ds_qr_start(NULL, "crypto-psbt", msg, len, 2), DS_ERR_ARG);
}

/* Sets the last 4 bytes of m so that its CRC-32 is 0 (its CBOR in each part
 * is then 1 byte, not 5): the CRC is affine in those 32 bits, so the bits
 * are solved over GF(2). */
static void force_crc_zero(uint8_t *m, size_t len)
{
    uint8_t *tail = m + len - 4;
    uint32_t col[32], comb[32], base, x = 0, t;
    int i, j;

    memset(tail, 0, 4);
    base = ut_crc32(m, len);
    for (i = 0; i < 32; i++) {
        tail[i / 8] = (uint8_t)(1u << (i % 8));
        col[i] = ut_crc32(m, len) ^ base;
        comb[i] = 1u << i;
        tail[i / 8] = 0;
    }
    /* Gauss-Jordan: col[i] becomes bit i, comb[i] the tail bits giving it. */
    for (i = 0; i < 32; i++) {
        for (j = i; j < 32 && !((col[j] >> i) & 1u); j++) {
        }
        CHECK(j < 32);
        if (j == 32) {
            return;
        }
        t = col[i], col[i] = col[j], col[j] = t;
        t = comb[i], comb[i] = comb[j], comb[j] = t;
        for (j = 0; j < 32; j++) {
            if (j != i && ((col[j] >> i) & 1u)) {
                col[j] ^= col[i];
                comb[j] ^= comb[i];
            }
        }
    }
    for (i = 0; i < 32; i++) {
        if ((base >> i) & 1u) {
            x ^= comb[i];
        }
    }
    for (i = 0; i < 4; i++) {
        tail[i] = (uint8_t)(x >> (8 * i));
    }
}

/* What fits depends on the message's length only: the parts are measured
 * with the message CRC-32 at its longest (5 bytes in the CBOR), so a message
 * whose CRC takes 1 byte gets the same fragment and scale as any other of
 * its length, and ds_app_fits_qr may probe with zeros. */
static void test_qr_crc_size_ignored(void)
{
    static const size_t longer[] = {1500, 2400, 6000, DS_APP_PSBT_MAX};
    size_t len, k, i;
    int scale;

    for (k = 0; k < 400 + sizeof longer / sizeof longer[0]; k++) {
        len = k < 400 ? 20 + k : longer[k - 400];
        for (scale = 2; scale >= 1; scale--) {
            int32_t r1, r2;
            uint32_t frag, parts;

            for (i = 0; i < len; i++) {
                big[i] = (uint8_t)(i * 7 + 3);
            }
            CHECK(ut_crc32(big, len) >= 0x10000u); /* 5 bytes in the CBOR */
            r1 = ds_qr_start(&q, "crypto-psbt", big, len, scale);
            frag = q.e.fragment;
            parts = q.e.seq_len;
            force_crc_zero(big, len);
            CHECK_EQ_INT(ut_crc32(big, len), 0);
            r2 = ds_qr_start(&q, "crypto-psbt", big, len, scale);
            CHECK_EQ_INT(r2, r1);
            CHECK_EQ_INT(q.e.fragment, frag);
            CHECK_EQ_INT(q.e.seq_len, parts);
            if (len < 400 && r2 > 0) {
                /* And every part of it still fits, through W. */
                for (i = 0; i < 2 * q.e.seq_len + 12; i++) {
                    CHECK(ds_qr_next(&q) > 0);
                }
            }
        }
    }
}

static void test_qr_fallback(void)
{
    size_t i;
    int32_t r;
    for (i = 0; i < sizeof big; i++) {
        big[i] = (uint8_t)(i * 7 + 3);
    }
    /* 6000 bytes: at scale 2 (version 3) the fragment is at least 10 bytes,
     * so it needs 600 parts > UT_MAX_PARTS (256); scale 1 (version 7) takes
     * fragments of 82 bytes, 74 parts. */
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", big, 6000, 2), 1);
    CHECK(q.e.seq_len <= UT_MAX_PARTS);
    CHECK(ds_qr_next(&q) <= 17 + 4 * 7);

    /* DS_APP_PSBT_MAX (16384 bytes): fragment 82 and 200 parts fit at 1 px. */
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", big, DS_APP_PSBT_MAX, 2), 1);

    /* DS_APP_PSBT_MAX + 4096 = 20480 bytes: the fragment is 82 bytes and it
     * is 250 parts (under 256), but the highest part number is W = 510, whose
     * 3-digit number and 3-byte CBOR number no longer fit version 7. Not even
     * at 1 px. */
    r = ds_qr_start(&q, "crypto-psbt", big, sizeof big, 2);
    CHECK_EQ_INT(r, DS_ERR_LIMIT);
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", big, sizeof big, 1), DS_ERR_LIMIT);
}

static void test_qr_too_big(void)
{
    static uint8_t huge[40000];
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", huge, sizeof huge, 2), DS_ERR_LIMIT);
    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", huge, sizeof huge, 1), DS_ERR_LIMIT);
}

static void test_qr_next_before_start(void)
{
    static ds_qr z;
    CHECK_EQ_INT(ds_qr_next(&z), DS_ERR_STATE);
    CHECK_EQ_INT(ds_qr_next(NULL), DS_ERR_ARG);
}

static int pixel(int x, int y)
{
    return (fb[y / 8][x] >> (y % 8)) & 1;
}

static void check_box(int scale)
{
    size_t len = scale == 2 ? synthetic() : vector_cbor("two inputs with non_witness_utxo");
    const uint8_t *m = scale == 2 ? synth : msg;
    int n, size, box, off, x, y, lit_outside = 0;

    CHECK_EQ_INT(ds_qr_start(&q, "crypto-psbt", m, len, scale), scale);
    size = ds_qr_next(&q);
    CHECK(size > 0);
    memset(fb, 0xFF, sizeof fb); /* draw must clear the zone and leave the rest */
    for (y = 0; y < 64; y++) { /* the right half is not ours */
        fb[y / 8][100] = 0xAA;
    }
    ds_qr_draw(&q, fb);
    box = (size + 2) * scale;
    off = (64 - box) / 2;
    CHECK(box <= 64);
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            int mx = (x - off) / scale - 1, my = (y - off) / scale - 1;
            int inside = x >= off && x < off + box && y >= off && y < off + box;
            int expect = 0;
            if (inside) {
                expect = (mx < 0 || my < 0 || mx >= size || my >= size)
                             ? 1
                             : !qrcodegen_getModule(q.qr, mx, my);
            }
            if (pixel(x, y) != expect) {
                lit_outside++;
            }
        }
    }
    CHECK_EQ_INT(lit_outside, 0);
    for (x = 64; x < 128; x++) {
        for (n = 0; n < 8; n++) {
            CHECK_EQ_INT(fb[n][x], x == 100 ? 0xAA : 0xFF);
        }
    }
}

static void test_qr_draw_box(void)
{
    check_box(2);
    check_box(1);
}

TEST_MAIN(test_qr_roundtrip_scale2, test_qr_scale1_fits_v7, test_qr_psbt_needs_scale1,
          test_qr_args,
          test_qr_fallback, test_qr_long_run, test_qr_too_big, test_qr_next_before_start,
          test_qr_draw_box, test_qr_crc_size_ignored)
