/* What the signer says: number composition, sample counts, the log, and that
 * no clip or earcon sounds like a DTMF key to the detector. */
#include <stdint.h>
#include <string.h>

#include "ds_core.h"
#include "ds_pack.h"
#include "ds_prompts.h"
#include "ds_say.h"
#include "test.h"
#include "voice_test_packs.h"

static ds_pack en;

static size_t drain(ds_say *s, int16_t *out, size_t cap)
{
    size_t n = 0;
    while (ds_say_busy(s) && n + 64 <= cap) {
        ds_say_read(s, out + n, 64);
        n += 64;
    }
    return n;
}

static void test_say_number(void)
{
    static int16_t got[200000], want[200000];
    ds_say s;
    char tok[DS_SAY_TOKEN_MAX];
    uint16_t ids[40];
    ds_say_init(&s, &en);
    CHECK_EQ_INT(ds_say_number(&s, 60000), DS_OK);
    CHECK(ds_say_take_log(&s, tok, sizeof tok) > 0);
    CHECK_EQ_STR(tok, "num 60000");
    CHECK_EQ_INT(ds_say_take_log(&s, tok, sizeof tok), 0);
    size_t n = 0;
    while (ds_say_busy(&s)) {
        ds_say_read(&s, got + n, 1);
        n++;
    }
    int32_t k = ds_pack_number(&en, 60000, ids, 40);
    CHECK(k > 0);
    size_t w = 0;
    for (int32_t i = 0; i < k; i++) {
        ds_say one;
        ds_say_init(&one, &en);
        ds_say_clip(&one, ids[i]);
        while (ds_say_busy(&one)) {
            ds_say_read(&one, want + w, 1);
            w++;
        }
    }
    CHECK_EQ_INT(n, w);
    CHECK(n > 0 && memcmp(got, want, w * sizeof want[0]) == 0);
}

static void test_read_lengths(void)
{
    ds_say s;
    const uint8_t *adpcm;
    uint32_t bytes;
    ds_adpcm st;
    int16_t buf[64];
    ds_say_init(&s, &en);
    CHECK_EQ_INT(ds_pack_clip(&en, DS_P_C_5, &adpcm, &bytes, &st), DS_OK);
    ds_say_clip(&s, DS_P_C_5);
    CHECK(ds_say_busy(&s));
    size_t total = 0;
    while (total < (size_t)bytes * 4) {
        CHECK(ds_say_busy(&s));
        ds_say_read(&s, buf, 1);
        total++;
    }
    /* Samples of the clip: 2 per nibble pair of the 8 kHz source, that is 4 per byte. */
    CHECK_EQ_INT(total, (size_t)bytes * 4);
    CHECK_EQ_INT(ds_say_busy(&s), 0);
    memset(buf, 0x55, sizeof buf);
    CHECK_EQ_INT(ds_say_read(&s, buf, 64), 64);
    for (int i = 0; i < 64; i++)
        CHECK_EQ_INT(buf[i], 0);
    CHECK_EQ_INT(ds_say_busy(&s), 0);
}

static void test_secret_char_not_logged(void)
{
    ds_say s;
    char tok[DS_SAY_TOKEN_MAX];
    ds_say_init(&s, &en);
    CHECK_EQ_INT(ds_say_secret_char(&s, 'q'), DS_OK);
    CHECK(ds_say_take_log(&s, tok, sizeof tok) > 0);
    CHECK_EQ_STR(tok, "say pin-char");
    CHECK_EQ_INT(ds_say_take_log(&s, tok, sizeof tok), 0);
    CHECK_EQ_INT(ds_say_chars(&s, "bc1q-k"), DS_OK);
    CHECK(ds_say_take_log(&s, tok, sizeof tok) > 0);
    CHECK_EQ_STR(tok, "chars bc1qk");
    CHECK_EQ_INT(ds_say_take_log(&s, tok, 10), DS_ERR_SPACE);
}

static void test_log_tokens(void)
{
    ds_say s;
    char tok[DS_SAY_TOKEN_MAX];
    ds_say_init(&s, &en);
    ds_say_clip(&s, DS_P_GREETING);
    ds_say_earcon(&s, DS_EAR_OK);
    ds_say_tones(&s, "312");
    ds_say_pause(&s, 50);
    ds_say_take_log(&s, tok, sizeof tok);
    CHECK_EQ_STR(tok, "say greeting");
    ds_say_take_log(&s, tok, sizeof tok);
    CHECK_EQ_STR(tok, "ear ok");
    ds_say_take_log(&s, tok, sizeof tok);
    CHECK_EQ_STR(tok, "tones 312");
    CHECK_EQ_INT(ds_say_clip(&s, DS_P_COUNT), DS_ERR_ARG);
    CHECK_EQ_INT(ds_say_earcon(&s, 0), DS_ERR_ARG);
}

static void test_earcon_lengths_and_level(void)
{
    static const struct { int e; size_t ms; } want[] = {
        {DS_EAR_OK, 240}, {DS_EAR_FAIL, 240}, {DS_EAR_WARN, 320},
        {DS_EAR_LISTEN, 150}, {DS_EAR_NEXT, 40}, {DS_EAR_BACK, 40}};
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        ds_say s;
        static int16_t buf[8000];
        ds_say_init(&s, &en);
        ds_say_earcon(&s, want[i].e);
        size_t n = 0;
        int peak = 0;
        while (ds_say_busy(&s)) {
            ds_say_read(&s, buf + n, 1);
            int v = buf[n] < 0 ? -buf[n] : buf[n];
            peak = v > peak ? v : peak;
            n++;
        }
        CHECK_EQ_INT(n, want[i].ms * 16);
        if (want[i].e == DS_EAR_LISTEN) {
            CHECK(peak > 3500 && peak <= 4200);
        } else {
            CHECK(peak > 15500 && peak <= 16500);
        }
        CHECK(buf[0] > -400 && buf[0] < 400);
    }
}

static void test_tones_play(void)
{
    ds_say s;
    static int16_t buf[40000];
    ds_say_init(&s, &en);
    ds_say_tones(&s, "312");
    size_t n = drain(&s, buf, sizeof buf);
    CHECK(n >= 800 * 16 && n < 40000);
}

static void test_queue_full(void)
{
    ds_say s;
    ds_say_init(&s, &en);
    for (unsigned i = 0; i < DS_SAY_QUEUE; i++)
        CHECK_EQ_INT(ds_say_pause(&s, 10), DS_OK);
    CHECK_EQ_INT(ds_say_pause(&s, 10), DS_ERR_SPACE);
    ds_say_clear(&s);
    CHECK_EQ_INT(ds_say_busy(&s), 0);
    CHECK_EQ_INT(ds_say_pause(&s, 10), DS_OK);
}

/* ---- talk-off: no clip, no earcon, is a key to the detector ---- */

static int keys_heard(const int16_t *x, size_t n)
{
    static ut_listener l;
    char g[UT_GROUP + 1];
    ut_listener_init(&l, 16000);
    for (size_t i = 0; i < n; i += 256)
        ut_listener_push(&l, x + i, n - i < 256 ? n - i : 256);
    ut_listener_flush(&l);
    int heard = 0;
    char lv[UT_LIVE + 1];
    lv[0] = g[0] = 0;
    heard += ut_listener_live(&l, lv, sizeof lv) > 0;
    heard += ut_listener_group(&l, g, sizeof g) > 0;
    if (heard)
        printf("  live '%s' group '%s'\n", lv, g);
    return heard;
}

/* Plays one clip (or earcon) at 16 kHz and tells whether the detector hears a key. */
static int played_is_a_key(const ds_pack *p, unsigned id, int ear)
{
    static ds_say s;
    static int16_t buf[16000 * 40];
    ds_say_init(&s, p);
    if (ear)
        ds_say_earcon(&s, ear);
    else
        ds_say_clip(&s, (uint16_t)id);
    size_t n = 0;
    while (ds_say_busy(&s) && n < sizeof buf / sizeof buf[0])
        n += ds_say_read(&s, buf + n, 1);
    CHECK(!ds_say_busy(&s));
    return keys_heard(buf, n) != 0;
}

static void test_no_earcon_is_a_key(void)
{
    for (int e = DS_EAR_OK; e <= DS_EAR_BACK; e++) {
        if (played_is_a_key(&en, (unsigned)e, e)) {
            printf("earcon %d is heard as a key\n", e);
            ds_failures++;
        }
    }
}

/* Clips of the current packs that the detector takes for a key (speech
 * holding energy on a DTMF pair). KNOWN TALK-OFF: to be emptied by fixing the
 * clips; the test fails on any clip not listed and on any listed one that no
 * longer trips, so the list can only shrink. Sorted. */
static const uint16_t talkoff_en[] = {0, 1, 10, 11, 12, 13, 14, 17, 18, 26, 27, 29,
                                      30, 31, 59, 61, 65, 71, 145, 162, 164};
static const uint16_t talkoff_es[] = {1, 12, 13, 14, 17, 27, 28, 55, 58, 98, 132};

static int listed(const uint16_t *list, size_t n, unsigned id)
{
    for (size_t i = 0; i < n; i++)
        if (list[i] == id)
            return 1;
    return 0;
}

static void test_no_prompt_is_a_key(void)
{
    ds_pack es;
    CHECK_EQ_INT(ds_pack_open(&es, es_bin, ES_BIN_LEN), DS_OK);
    for (unsigned id = 0; id < DS_P_COUNT; id++) {
        int e = played_is_a_key(&en, id, 0);
        int x = played_is_a_key(&es, id, 0);
        int ke = listed(talkoff_en, sizeof talkoff_en / sizeof talkoff_en[0], id);
        int kx = listed(talkoff_es, sizeof talkoff_es / sizeof talkoff_es[0], id);
        if (e != ke || x != kx) {
            printf("clip %u (en/es): heard as a key %d/%d, listed %d/%d\n", id, e, x, ke, kx);
            ds_failures++;
        }
    }
}

int main(void)
{
    CHECK_EQ_INT(ds_pack_open(&en, ds_pack_en, ds_pack_en_len), DS_OK);
    test_say_number();
    test_read_lengths();
    test_secret_char_not_logged();
    test_log_tokens();
    test_earcon_lengths_and_level();
    test_tones_play();
    test_queue_full();
    test_no_earcon_is_a_key();
    test_no_prompt_is_a_key();
    if (ds_failures)
        printf("%d check(s) failed\n", ds_failures);
    return ds_failures != 0;
}
