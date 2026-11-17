/* The ADPCM decoder against the shared vector; the pack reader, the number
 * grammar and the region scan against the real packs and a data-only
 * Japanese one (both generated into the build directory). */
#include <stdint.h>
#include <string.h>

#include "adpcm_vector.h"
#include "ds_adpcm.h"
#include "ds_core.h"
#include "ds_crc32.h"
#include "ds_pack.h"
#include "ds_prompts.h"
#include "test.h"
#include "voice_test_packs.h"

static void test_adpcm_vector(void)
{
    int16_t out[sizeof adpcm_vector_decoded / sizeof adpcm_vector_decoded[0]];
    size_t n = sizeof out / sizeof out[0];
    ds_adpcm st = {ADPCM_VECTOR_PRED, ADPCM_VECTOR_INDEX};

    CHECK_EQ_INT(sizeof adpcm_vector_bytes * 2, n);
    ds_adpcm_decode(&st, adpcm_vector_bytes, n, out);
    CHECK(memcmp(out, adpcm_vector_decoded, sizeof out) == 0);
    CHECK_EQ_INT(st.pred, out[n - 1]);

    /* An odd count stops after the low nibble of the last byte. */
    int16_t odd[3] = {0x7777, 0x7777, 0x7777};
    st.pred = ADPCM_VECTOR_PRED;
    st.index = ADPCM_VECTOR_INDEX;
    ds_adpcm_decode(&st, adpcm_vector_bytes, 3, odd);
    CHECK(memcmp(odd, adpcm_vector_decoded, sizeof odd) == 0);
}

static void test_adpcm_clamps_index(void)
{
    /* An index past the table (an untrusted start state) decodes as 88. */
    int16_t a[8], b[8];
    ds_adpcm s1 = {0, 200}, s2 = {0, 88};
    ds_adpcm_decode(&s1, adpcm_vector_bytes, 8, a);
    ds_adpcm_decode(&s2, adpcm_vector_bytes, 8, b);
    CHECK(memcmp(a, b, sizeof a) == 0);
    CHECK(s1.index <= 88);
}

static void check_pack(const uint8_t *data, uint32_t len, const char *lang)
{
    ds_pack p;
    CHECK_EQ_INT(ds_pack_open(&p, data, len), DS_OK);
    CHECK_EQ_INT(p.count, DS_P_COUNT);
    CHECK_EQ_INT(p.len, len);
    CHECK_EQ_STR(p.lang, lang);
    for (uint16_t id = 0; id < p.count; id++) {
        const uint8_t *adpcm;
        uint32_t bytes;
        ds_adpcm start;
        CHECK_EQ_INT(ds_pack_clip(&p, id, &adpcm, &bytes, &start), DS_OK);
        CHECK(adpcm >= data && adpcm + bytes <= data + len);
        CHECK(start.index <= 88);
    }
    const uint8_t *adpcm;
    uint32_t bytes;
    ds_adpcm start;
    CHECK_EQ_INT(ds_pack_clip(&p, p.count, &adpcm, &bytes, &start), DS_ERR_ARG);
}

static void test_pack_v2_open(void)
{
    check_pack(ds_pack_en, ds_pack_en_len, "en");
    check_pack(es_bin, ES_BIN_LEN, "es");
    /* A pack followed by more bytes (as in the region) opens at its own size. */
    ds_pack p;
    CHECK_EQ_INT(ds_pack_open(&p, ds_pack_en, ds_pack_en_len - 1), DS_ERR_ARG);
}

/* -- A minimal pack, edited to break one rule at a time ------------------ */

#define HDR 22u
#define TABLE (HDR + 12u * DS_P_COUNT) /* where the grammar starts */

static uint8_t mini[4096];

static void wr16(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *b, uint32_t v)
{
    wr16(b, v);
    wr16(b + 2, v >> 16);
}

static void wr64(uint8_t *b, uint64_t v)
{
    wr32(b, (uint32_t)v);
    wr32(b + 4, (uint32_t)(v >> 32));
}

/* Writes the CRC of mini[0..len) the way the tool does. */
static void seal(uint32_t len)
{
    wr32(mini + 14, 0);
    wr32(mini + 14, ds_crc32(0, mini, len));
}

/* DS_P_COUNT empty clips but the first (4 bytes), small[0] = clip 0, the
 * given scales (values 10^k, largest first) and no irregulars. */
static uint32_t build_mini(unsigned nscales)
{
    memset(mini, 0, sizeof mini);
    memcpy(mini, "DSV2es", 6);
    wr16(mini + 6, 2);
    wr16(mini + 8, DS_P_COUNT);
    uint8_t *g = mini + TABLE;
    for (int k = 0; k < 100; k++)
        wr16(g + 2 * k, k == 0 ? 0 : 0xffff);
    g[200] = (uint8_t)nscales;
    uint64_t value = 10;
    for (unsigned k = 0; k < nscales; k++)
        value *= 10;
    for (unsigned k = 0; k < nscales; k++) {
        uint8_t *s = g + 201 + 18 * k;
        value /= 10;
        wr64(s, value * 10);
        wr16(s + 8, 1);
        wr16(s + 10, 0xffff);
    }
    uint32_t data = TABLE + 201 + 18 * nscales + 2; /* nirr = 0 */
    for (uint32_t id = 0; id < DS_P_COUNT; id++)
        wr32(mini + HDR + 12 * id, data);
    wr32(mini + HDR + 4, 4); /* clip 0: 4 bytes */
    uint32_t total = data + 4;
    wr32(mini + 10, total);
    wr32(mini + 18, TABLE);
    seal(total);
    return total;
}

static void test_pack_rejects(void)
{
    ds_pack p;
    uint32_t len = build_mini(2);
    CHECK_EQ_INT(ds_pack_open(&p, mini, len), DS_OK);
    CHECK_EQ_INT(ds_pack_open(&p, mini, len + 100), DS_OK); /* trailing bytes */
    CHECK_EQ_INT(p.len, len);
    CHECK_EQ_INT(ds_pack_open(&p, mini, len - 1), DS_ERR_ARG); /* truncated */
    CHECK_EQ_INT(ds_pack_open(&p, mini, 21), DS_ERR_ARG);      /* header truncated */
    CHECK_EQ_INT(ds_pack_open(&p, NULL, len), DS_ERR_ARG);

    wr32(mini + 10, len + 1); /* total_len past the data given */
    seal(len + 1);
    CHECK_EQ_INT(ds_pack_open(&p, mini, len), DS_ERR_ARG);
    len = build_mini(2);

    mini[len - 1] ^= 1; /* bad CRC */
    CHECK_EQ_INT(ds_pack_open(&p, mini, len), DS_ERR_ARG);
    mini[len - 1] ^= 1;

#define BREAK(stmt, total)                                                   \
    do {                                                                     \
        uint32_t t_ = build_mini(2);                                         \
        stmt;                                                                \
        seal(total);                                                         \
        CHECK_EQ_INT(ds_pack_open(&p, mini, (total)), DS_ERR_ARG);           \
        (void)t_;                                                            \
    } while (0)

    BREAK(mini[0] = 'X', t_);                     /* magic */
    BREAK(mini[4] = 'E', t_);                     /* language must be lowercase */
    BREAK(wr16(mini + 6, 1), t_);                 /* format */
    BREAK(wr16(mini + 8, DS_P_COUNT - 1), t_);    /* another firmware's prompt list */
    BREAK(wr32(mini + 18, TABLE - 1), t_);        /* grammar inside the table */
    BREAK(mini[HDR + 10] = 89, t_);               /* start index out of range */
    BREAK(wr16(mini + TABLE + 2, DS_P_COUNT), t_); /* small[1] not a clip */
    BREAK(wr64(mini + TABLE + 201 + 18, 1000), t_); /* scales not decreasing */
    BREAK(wr64(mini + TABLE + 201 + 18, 5), t_);  /* a scale below 10 */
    BREAK(wr16(mini + TABLE + 201 + 8, 0xffff), t_); /* no singular word */
    BREAK(mini[TABLE + 201 + 12] = 2, t_);        /* say_one not a flag */

    /* offset + bytes would wrap 32 bits: offset 4 before the end and bytes
     * 0xfffffffc sum to total - 8, which a naive check would accept. */
    BREAK((wr32(mini + HDR, t_ - 4), wr32(mini + HDR + 4, 0xfffffffcu)), t_);

    /* More than 8 scales. */
    len = build_mini(9);
    CHECK_EQ_INT(ds_pack_open(&p, mini, len), DS_ERR_ARG);
    len = build_mini(8);
    CHECK_EQ_INT(ds_pack_open(&p, mini, len), DS_OK);

    /* An irregular of an unknown kind. */
    len = build_mini(0);
    {
        uint8_t *nirr = mini + TABLE + 201;
        memmove(nirr + 2 + 16, nirr + 2, 4);
        wr16(nirr, 1);
        wr64(nirr + 2, 30);
        wr16(nirr + 2 + 8, 1);
        nirr[2 + 10] = 4;
        len += 16;
        for (uint32_t id = 0; id < DS_P_COUNT; id++)
            wr32(mini + HDR + 12 * id, len - 4);
        wr32(mini + 10, len);
        seal(len);
        CHECK_EQ_INT(ds_pack_open(&p, mini, len), DS_ERR_ARG);
        nirr[2 + 10] = 1;
        seal(len);
        CHECK_EQ_INT(ds_pack_open(&p, mini, len), DS_OK);
    }
#undef BREAK
}

/* -- The region ---------------------------------------------------------- */

static uint8_t region[1u << 20];

static void test_scan_region(void)
{
    uint32_t x = 12345;
    for (size_t k = 0; k < sizeof region; k++) {
        x = x * 1103515245u + 12345u;
        region[k] = (uint8_t)(x >> 24);
    }
    /* A corrupt copy at 0x1000, then es.bin on the next boundaries it does
     * not reach into, then (back to back) a second valid pack. */
    memcpy(region + 0x1000, es_bin, 0x3000);
    region[0x1000 + 100] ^= 0x40; /* the corrupt copy */
    memcpy(region + 0x4000, es_bin, ES_BIN_LEN);

    ds_pack en;
    ds_packs ps;
    CHECK_EQ_INT(ds_pack_open(&en, ds_pack_en, ds_pack_en_len), DS_OK);
    CHECK_EQ_INT(ds_packs_scan(&ps, &en, region, sizeof region), DS_OK);
    CHECK_EQ_INT(ps.n, 2);
    CHECK_EQ_STR(ps.pack[0].lang, "en");
    CHECK_EQ_STR(ps.pack[1].lang, "es");
    CHECK(ps.pack[1].data == region + 0x4000);
    CHECK(memcmp(ps.fp[1], es_bin_fp, 8) == 0);

    /* Two valid packs back to back: the second starts on the next 4 KB
     * boundary after the first's end, however large the first is. */
    uint32_t second = 0x4000 + (ES_BIN_LEN + DS_PACK_ALIGN - 1) / DS_PACK_ALIGN * DS_PACK_ALIGN;
    uint32_t mlen0 = build_mini(2);
    CHECK(second + mlen0 <= sizeof region);
    memcpy(region + second, mini, mlen0);
    CHECK_EQ_INT(ds_packs_scan(&ps, &en, region, sizeof region), DS_OK);
    CHECK_EQ_INT(ps.n, 3);
    CHECK(ps.pack[1].data == region + 0x4000);
    CHECK(ps.pack[2].data == region + second);

    /* No region: only the built-in pack. */
    CHECK_EQ_INT(ds_packs_scan(&ps, &en, NULL, 0), DS_OK);
    CHECK_EQ_INT(ps.n, 1);

    /* At most four in all, then the scan stops. */
    memset(region, 0xff, sizeof region);
    uint32_t mlen = build_mini(2);
    for (uint32_t k = 0; k < 5; k++)
        memcpy(region + k * DS_PACK_ALIGN, mini, mlen);
    CHECK_EQ_INT(ds_packs_scan(&ps, &en, region, sizeof region), DS_OK);
    CHECK_EQ_INT(ps.n, DS_PACKS_MAX);
}

/* -- Numbers ------------------------------------------------------------- */

static void check_number(const ds_pack *p, uint64_t n, const uint16_t *want, int count,
                         int line)
{
    uint16_t got[32];
    int32_t rc = ds_pack_number(p, n, got, sizeof got / sizeof got[0]);
    int same = rc == count;
    for (int k = 0; same && k < count; k++)
        same = got[k] == want[k];
    if (!same) {
        printf("%s:%d: number %llu: got %d clips:", __FILE__, line, (unsigned long long)n,
               (int)rc);
        for (int k = 0; k < rc; k++)
            printf(" %u", got[k]);
        printf("\n");
        ds_failures++;
    }
}

#define SAYS(p, n, ...)                                                      \
    do {                                                                     \
        static const uint16_t w_[] = {__VA_ARGS__};                          \
        check_number(p, n, w_, (int)(sizeof w_ / sizeof w_[0]), __LINE__);  \
    } while (0)

static void test_numbers_es(void)
{
    ds_pack p;
    CHECK_EQ_INT(ds_pack_open(&p, es_bin, ES_BIN_LEN), DS_OK);
    SAYS(&p, 60000, DS_P_N_SESENTA, DS_P_N_MIL);
    SAYS(&p, 39000, DS_P_N_TREINTA_Y, DS_P_N_NUEVE, DS_P_N_MIL);
    SAYS(&p, 1000, DS_P_N_MIL);
    SAYS(&p, 1001, DS_P_N_MIL, DS_P_N_UNO);
    SAYS(&p, 100, DS_P_N_CIEN);
    SAYS(&p, 101, DS_P_N_CIENTO, DS_P_N_UNO);
    SAYS(&p, 1000000, DS_P_N_UN, DS_P_N_MILLON);
    SAYS(&p, 2500000, DS_P_N_DOS, DS_P_N_MILLONES, DS_P_N_QUINIENTOS, DS_P_N_MIL);
    SAYS(&p, 21000, DS_P_N_VEINTIUN, DS_P_N_MIL);
    SAYS(&p, 31000, DS_P_N_TREINTA_Y, DS_P_N_UN, DS_P_N_MIL);
    SAYS(&p, 0, DS_P_N_CERO);
    SAYS(&p, 101000, DS_P_N_CIENTO, DS_P_N_UN, DS_P_N_MIL);
    SAYS(&p, 1000000000, DS_P_N_MIL, DS_P_N_MILLONES);

    /* Too little room. */
    uint16_t out[2];
    CHECK_EQ_INT(ds_pack_number(&p, 2500000, out, 2), DS_ERR_SPACE);
    /* The largest amount fits in a 32-clip buffer. */
    uint16_t big[32];
    CHECK(ds_pack_number(&p, UINT64_MAX, big, 32) > 0);
}

static void test_numbers_en(void)
{
    ds_pack p;
    CHECK_EQ_INT(ds_pack_open(&p, ds_pack_en, ds_pack_en_len), DS_OK);
    SAYS(&p, 60000, DS_P_N_SIXTY, DS_P_N_THOUSAND);
    SAYS(&p, 1000001, DS_P_N_ONE, DS_P_N_MILLION, DS_P_N_ONE);
    SAYS(&p, 1234, DS_P_N_ONE, DS_P_N_THOUSAND, DS_P_N_TWO, DS_P_N_HUNDRED, DS_P_N_THIRTY,
         DS_P_N_FOUR);
    SAYS(&p, 0, DS_P_N_ZERO);
    SAYS(&p, 15, DS_P_N_FIFTEEN);
}

static void test_numbers_ja_grouping(void)
{
    ds_pack p;
    CHECK_EQ_INT(ds_pack_open(&p, ja_pack, JA_PACK_LEN), DS_OK);
    CHECK_EQ_STR(p.lang, "ja");
    SAYS(&p, 120000, JA_JUU, 2, JA_MAN);
    SAYS(&p, 100000000, 1, JA_OKU);
    SAYS(&p, 1000, JA_SEN);
    SAYS(&p, 300, JA_SANBYAKU);
    SAYS(&p, 3000, JA_SANZEN);
    SAYS(&p, 12345678, JA_SEN, 2, JA_HYAKU, 3, JA_JUU, 4, JA_MAN, 5, JA_SEN, JA_ROPPYAKU, 7,
         JA_JUU, 8);
    /* The test pack has no word for zero. */
    uint16_t out[4];
    CHECK_EQ_INT(ds_pack_number(&p, 0, out, 4), DS_ERR_ARG);
}

TEST_MAIN(test_adpcm_vector, test_adpcm_clamps_index, test_pack_v2_open, test_pack_rejects,
          test_scan_region, test_numbers_es, test_numbers_en, test_numbers_ja_grouping)
