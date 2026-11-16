/* The ADPCM decoder against the shared vector, and the pack reader. */
#include <stdint.h>
#include <string.h>

#include "adpcm_vector.h"
#include "ds_adpcm.h"
#include "ds_core.h"
#include "ds_pack.h"
#include "ds_prompts.h"
#include "test.h"

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

static void test_pack_opens(const uint8_t *data, uint32_t len, const char *lang)
{
    ds_pack p;
    CHECK_EQ_INT(ds_pack_open(&p, data, len), DS_OK);
    CHECK_EQ_INT(p.count, DS_P_COUNT);
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

static void test_pack_es_opens(void)
{
    test_pack_opens(ds_pack_es, ds_pack_es_len, "es");
    test_pack_opens(ds_pack_en, ds_pack_en_len, "en");
}

static void test_pack_rejects_bad(void)
{
    uint8_t buf[64];
    ds_pack p;

    /* One clip of 4 bytes right after the 8 + 12 byte table. */
    memset(buf, 0, sizeof buf);
    memcpy(buf, "DSV1es", 6);
    buf[6] = 1;
    buf[8] = 20; /* offset */
    buf[12] = 4; /* bytes */
    CHECK_EQ_INT(ds_pack_open(&p, buf, 24), DS_OK);
    CHECK_EQ_INT(ds_pack_open(&p, buf, 23), DS_ERR_ARG); /* clip past the end */
    CHECK_EQ_INT(ds_pack_open(&p, buf, 19), DS_ERR_ARG); /* table truncated */
    CHECK_EQ_INT(ds_pack_open(&p, buf, 7), DS_ERR_ARG);  /* header truncated */
    CHECK_EQ_INT(ds_pack_open(&p, NULL, 24), DS_ERR_ARG);

    buf[0] = 'X'; /* bad magic */
    CHECK_EQ_INT(ds_pack_open(&p, buf, 24), DS_ERR_ARG);
    buf[0] = 'D';

    buf[4] = 'E'; /* language must be lowercase letters */
    CHECK_EQ_INT(ds_pack_open(&p, buf, 24), DS_ERR_ARG);
    buf[4] = 'e';

    buf[18] = 89; /* index out of range */
    CHECK_EQ_INT(ds_pack_open(&p, buf, 24), DS_ERR_ARG);
    buf[18] = 0;

    buf[11] = 0xff; /* offset + bytes would overflow 32 bits */
    buf[10] = 0xff;
    buf[9] = 0xff;
    buf[8] = 0xff;
    buf[12] = 8;
    CHECK_EQ_INT(ds_pack_open(&p, buf, 24), DS_ERR_ARG);
}

TEST_MAIN(test_adpcm_vector, test_pack_es_opens, test_pack_rejects_bad)
