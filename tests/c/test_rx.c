#include <stdint.h>
#include <string.h>

#include "ds_psbt.h"
#include "ds_rx.h"
#include "psbt_vectors.h"
#include "test.h"
#include "tones.h"

/* A ds_rx is about 40 KB and the audio of a frame is large: all static. */
static ds_rx rx;
static int16_t audio[TONES_RATE * 60];
static char tones[UT_GROUP + 1];
static uint8_t cbor[2048];

static const uint8_t zero16[16];

/* Sends tones through the receiver as audio and takes the one group heard. */
static int32_t hear(ds_rx *r, const char *t)
{
    size_t n = tones_render(t, audio, sizeof audio / sizeof audio[0]), i;
    for (i = 0; i < n; i += 1000) {
        ds_rx_push(r, audio + i, n - i < 1000 ? n - i : 1000);
    }
    return ds_rx_poll(r);
}

static int all_zero(const char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void test_rx_seed_ur_pin(void)
{
    uint8_t e[32];
    CHECK(tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones) > 0);
    ds_rx_init(&rx, TONES_RATE, DS_RX_SEED);
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_GOT_SEED);
    CHECK_EQ_INT(rx.has_group, 1);
    CHECK_EQ_INT(ds_rx_seed(&rx, "TEST", e), 16);
    CHECK(memcmp(e, zero16, 16) == 0);
    CHECK_EQ_INT(rx.has_group, 0);
    CHECK(all_zero(rx.group, sizeof rx.group));
    CHECK_EQ_INT(ds_rx_poll(&rx), DS_RX_NONE);
    CHECK_EQ_INT(ds_rx_seed(&rx, "TEST", e), DS_ERR_STATE);
    ds_rx_wipe(&rx);
}

static void test_rx_seed_wrong_pin_wipes(void)
{
    uint8_t e[32];
    CHECK(tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones) > 0);
    ds_rx_init(&rx, TONES_RATE, DS_RX_SEED);
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_GOT_SEED);
    CHECK(ds_rx_seed(&rx, "bad pin!", e) < 0);
    CHECK_EQ_INT(rx.has_group, 0);
    CHECK(all_zero(rx.group, sizeof rx.group));
    ds_rx_wipe(&rx);
}

static void test_rx_seed_keypad(void)
{
    uint8_t e[32];
    CHECK(tones_seed_keypad(zero16, 16, NULL, tones, sizeof tones) > 0);
    ds_rx_init(&rx, TONES_RATE, DS_RX_SEED);
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_GOT_SEED);
    CHECK_EQ_INT(ds_rx_seed(&rx, NULL, e), 16);
    CHECK(memcmp(e, zero16, 16) == 0);
    CHECK_EQ_INT(rx.has_group, 0);
    CHECK(all_zero(rx.group, sizeof rx.group));
    ds_rx_wipe(&rx);
}

static void test_rx_seed_replaced(void)
{
    uint8_t e[32], other[16];
    memset(other, 0x55, sizeof other);
    ds_rx_init(&rx, TONES_RATE, DS_RX_SEED);
    CHECK(tones_seed_ur(zero16, 16, NULL, tones, sizeof tones) > 0);
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_GOT_SEED);
    CHECK(tones_seed_ur(other, 16, NULL, tones, sizeof tones) > 0);
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_GOT_SEED);
    CHECK_EQ_INT(ds_rx_seed(&rx, NULL, e), 16);
    CHECK(memcmp(e, other, 16) == 0);
    ds_rx_wipe(&rx);
}

/* The PSBT of vector v as a fountain encoder over its CBOR, fragment 100. */
static void encoder_for(const psbt_vector *v, ut_fountain_encoder *e)
{
    int32_t n = ut_psbt_to_cbor(v->psbt, v->psbt_len, cbor, sizeof cbor);
    CHECK(n > 0);
    CHECK(ut_fountain_encoder_init(e, cbor, (size_t)n, 100) >= 0);
}

static void test_rx_psbt_parts(void)
{
    static ut_fountain_encoder e;
    const psbt_vector *v = &psbt_vectors[0];
    const uint8_t *p = NULL;
    size_t len = 0, i;
    int32_t r = DS_RX_NONE;
    uint32_t have = 0, need = 0;

    encoder_for(v, &e);
    ds_rx_init(&rx, TONES_RATE, DS_RX_PSBT);
    CHECK_EQ_INT(ds_rx_psbt(&rx, &p, &len), DS_ERR_STATE);
    for (i = 0; i < 20u * e.seq_len && r != DS_RX_DONE; i++) {
        CHECK(tones_next_part(&e, tones, sizeof tones) > 0);
        r = hear(&rx, tones);
        CHECK(r == DS_RX_PART || r == DS_RX_DONE);
        if (r == DS_RX_PART) {
            ds_rx_progress(&rx, &have, &need);
            CHECK_EQ_INT(need, e.seq_len);
            CHECK(have < need);
        }
    }
    CHECK_EQ_INT(r, DS_RX_DONE);
    CHECK_EQ_INT(ds_rx_psbt(&rx, &p, &len), DS_OK);
    CHECK_EQ_INT(len, v->psbt_len);
    CHECK(len == v->psbt_len && memcmp(p, v->psbt, len) == 0);
    CHECK(p >= rx.work && p < rx.work + sizeof rx.work);
    CHECK_EQ_INT(ds_rx_poll(&rx), DS_RX_NONE);
    ds_rx_wipe(&rx);
}

static void test_rx_psbt_lost_frame(void)
{
    static ut_fountain_encoder e;
    const psbt_vector *v = &psbt_vectors[0];
    const uint8_t *p = NULL;
    size_t len = 0;
    uint32_t i, total;
    int32_t r = DS_RX_NONE;

    encoder_for(v, &e);
    total = e.seq_len + 2;
    ds_rx_init(&rx, TONES_RATE, DS_RX_PSBT);
    for (i = 0; i < total + 1 && r != DS_RX_DONE; i++) {
        CHECK(tones_next_part(&e, tones, sizeof tones) > 0);
        if (i == 1) {
            continue; /* the second part is lost */
        }
        r = hear(&rx, tones);
    }
    CHECK_EQ_INT(r, DS_RX_DONE);
    CHECK_EQ_INT(ds_rx_psbt(&rx, &p, &len), DS_OK);
    CHECK(len == v->psbt_len && memcmp(p, v->psbt, len) == 0);
    ds_rx_wipe(&rx);
}

static void test_rx_wrong_kind(void)
{
    static ut_fountain_encoder a, b;
    const uint8_t *p = NULL;
    size_t len = 0, i;
    int32_t r = DS_RX_NONE;

    /* A seed while waiting for a PSBT. */
    CHECK(tones_seed_ur(zero16, 16, NULL, tones, sizeof tones) > 0);
    ds_rx_init(&rx, TONES_RATE, DS_RX_PSBT);
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_OTHER);
    CHECK_EQ_INT(rx.has_group, 0);
    CHECK(all_zero(rx.group, sizeof rx.group));
    CHECK_EQ_INT(ds_rx_psbt(&rx, &p, &len), DS_ERR_STATE);
    CHECK(tones_seed_keypad(zero16, 16, NULL, tones, sizeof tones) > 0);
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_OTHER);
    ds_rx_wipe(&rx);

    /* A PSBT while waiting for a seed. */
    encoder_for(&psbt_vectors[0], &a);
    CHECK(tones_next_part(&a, tones, sizeof tones) > 0);
    ds_rx_init(&rx, TONES_RATE, DS_RX_SEED);
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_OTHER);
    CHECK_EQ_INT(rx.has_group, 0);
    CHECK_EQ_INT(ds_rx_poll(&rx), DS_RX_NONE);
    ds_rx_wipe(&rx);

    /* Parts of two different PSBTs mixed: OTHER, then the second alone. */
    encoder_for(&psbt_vectors[0], &a);
    ds_rx_init(&rx, TONES_RATE, DS_RX_PSBT);
    CHECK(tones_next_part(&a, tones, sizeof tones) > 0);
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_PART);
    {
        static ut_fountain_encoder b2;
        int32_t n = ut_psbt_to_cbor(psbt_vectors[1].psbt, psbt_vectors[1].psbt_len,
                                    cbor, sizeof cbor);
        static uint8_t cbor_b[2048];
        CHECK(n > 0);
        memcpy(cbor_b, cbor, (size_t)n);
        CHECK(ut_fountain_encoder_init(&b2, cbor_b, (size_t)n, 100) >= 0);
        CHECK(tones_next_part(&b2, tones, sizeof tones) > 0);
        CHECK_EQ_INT(hear(&rx, tones), DS_RX_OTHER);
        b = b2;
    }
    r = DS_RX_NONE;
    for (i = 0; i < 20u * b.seq_len && r != DS_RX_DONE; i++) {
        /* the first part of b was already consumed by the mismatch: go on */
        CHECK(tones_next_part(&b, tones, sizeof tones) > 0);
        r = hear(&rx, tones);
    }
    CHECK_EQ_INT(r, DS_RX_DONE);
    CHECK_EQ_INT(ds_rx_psbt(&rx, &p, &len), DS_OK);
    CHECK(len == psbt_vectors[1].psbt_len &&
          memcmp(p, psbt_vectors[1].psbt, len) == 0);
    ds_rx_wipe(&rx);
}

/* The keys come live as they are heard: counted always, and the last ones
 * kept to be shown only for a PSBT (public); a seed's keys are never kept. */
static void test_rx_live_keys(void)
{
    static ut_fountain_encoder e;
    size_t n, k;

    encoder_for(&psbt_vectors[0], &e);
    CHECK(tones_next_part(&e, tones, sizeof tones) > 0);
    n = strlen(tones);
    ds_rx_init(&rx, TONES_RATE, DS_RX_PSBT);
    CHECK_EQ_INT(rx.keys, 0);
    CHECK_EQ_STR(rx.live, "");
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_PART);
    CHECK_EQ_INT(rx.keys, n);
    k = n < DS_RX_LIVE ? n : DS_RX_LIVE;
    CHECK_EQ_STR(rx.live, tones + n - k);
    ds_rx_wipe(&rx);

    CHECK(tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones) > 0);
    ds_rx_init(&rx, TONES_RATE, DS_RX_SEED);
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_GOT_SEED);
    CHECK_EQ_INT(rx.keys, strlen(tones));
    CHECK(all_zero(rx.live, sizeof rx.live));
    ds_rx_wipe(&rx);
}

/* Renders data tones at ur-tones' fast cable pace, 30/10/400 ms (SPEC 1.1,
 * only to a receiver known to support it). */
static size_t tones_render_fast(const char *tones, int16_t *out, size_t cap)
{
    ut_synth s;
    ut_synth_init(&s, tones, TONES_RATE, UT_FAST_TONE_MS, UT_FAST_GAP_MS, 400);
    return ut_synth_read(&s, out, cap);
}

/* Sends tones at the fast pace and takes the one group heard. */
static int32_t hear_fast(ds_rx *r, const char *t)
{
    size_t n = tones_render_fast(t, audio, sizeof audio / sizeof audio[0]), i;
    for (i = 0; i < n; i += 1000) {
        ds_rx_push(r, audio + i, n - i < 1000 ? n - i : 1000);
    }
    return ds_rx_poll(r);
}

/* ur-tones' fast cable pace, 30 ms + 10 ms: a seed and a whole PSBT. */
static void test_rx_fast_cable(void)
{
    static ut_fountain_encoder e;
    const psbt_vector *v = &psbt_vectors[0];
    const uint8_t *p = NULL;
    uint8_t ent[32];
    size_t len = 0, i;
    int32_t r = DS_RX_NONE;

    CHECK(tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones) > 0);
    ds_rx_init(&rx, TONES_RATE, DS_RX_SEED);
    CHECK_EQ_INT(hear_fast(&rx, tones), DS_RX_GOT_SEED);
    CHECK_EQ_INT(ds_rx_seed(&rx, "TEST", ent), 16);
    CHECK(memcmp(ent, zero16, 16) == 0);
    ds_rx_wipe(&rx);

    encoder_for(v, &e);
    ds_rx_init(&rx, TONES_RATE, DS_RX_PSBT);
    for (i = 0; i < e.seq_len && r != DS_RX_DONE; i++) {
        CHECK(tones_next_part(&e, tones, sizeof tones) > 0);
        r = hear_fast(&rx, tones);
    }
    CHECK_EQ_INT(r, DS_RX_DONE); /* no part lost at this pace */
    CHECK_EQ_INT(ds_rx_psbt(&rx, &p, &len), DS_OK);
    CHECK(len == v->psbt_len && memcmp(p, v->psbt, len) == 0);
    ds_rx_wipe(&rx);
}

/* The tones' level: the group's mean in 0.1 dBFS, 30 dB lower when the
 * audio is 30 dB quieter; -990 before any tone. */
static void test_rx_level(void)
{
    size_t n, i;
    int loud, quiet;

    CHECK(tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones) > 0);
    ds_rx_init(&rx, TONES_RATE, DS_RX_SEED);
    CHECK_EQ_INT(ds_rx_level(&rx), -990);
    CHECK_EQ_INT(ds_rx_take_sound(&rx), -990); /* nothing pushed */
    CHECK_EQ_INT(hear(&rx, tones), DS_RX_GOT_SEED);
    CHECK(ds_rx_take_sound(&rx) > -500); /* the tones, with their gaps */
    CHECK_EQ_INT(ds_rx_take_sound(&rx), -990); /* taken */
    loud = rx.group_level;
    CHECK(loud > -300 && loud < 0);
    ds_rx_wipe(&rx);

    n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
    for (i = 0; i < n; i++) {
        audio[i] = (int16_t)(audio[i] / 32); /* -30.1 dB */
    }
    ds_rx_init(&rx, TONES_RATE, DS_RX_SEED);
    for (i = 0; i < n / 2; i += 1000) {
        ds_rx_push(&rx, audio + i, 1000);
    }
    CHECK(ds_rx_level(&rx) < loud - 250); /* live, before the group ends */
    for (; i < n; i += 1000) {
        ds_rx_push(&rx, audio + i, n - i < 1000 ? n - i : 1000);
    }
    CHECK_EQ_INT(ds_rx_poll(&rx), DS_RX_GOT_SEED);
    quiet = rx.group_level;
    CHECK(quiet < loud - 280 && quiet > loud - 320);
    ds_rx_wipe(&rx);
}

TEST_MAIN(test_rx_seed_ur_pin, test_rx_seed_wrong_pin_wipes, test_rx_seed_keypad,
          test_rx_seed_replaced, test_rx_psbt_parts, test_rx_psbt_lost_frame,
          test_rx_wrong_kind, test_rx_live_keys, test_rx_fast_cable,
          test_rx_level)
