/* ds_app: the whole device flow driven by synthesized line-in audio (256
 * samples at a time, a tick every 16 ms of audio) and button events. */
#include <stdint.h>
#include <string.h>

#include "ds_app.h"
#include "ds_base64.h"
#include "ds_btn.h"
#include "psbt_vectors.h"
#include "test.h"
#include "tones.h"

/* Large structs and buffers: all static. */
static ds_app app;
static int16_t audio[TONES_RATE * 40];
static char tones[UT_GROUP + 1];
static uint8_t cbor[2048];
static ut_fountain_encoder enc;
static ut_fountain_decoder dec;
static uint8_t work[UT_FOUNTAIN_WORK(DS_APP_CBOR_MAX)];
static uint8_t want[DS_APP_OUT_MAX];
static char echo[DS_APP_ECHO_MAX];
static uint8_t raw[DS_APP_OUT_MAX];
static uint8_t fb[8][128];
static uint32_t now;

static const uint8_t zero16[16];

#define CHUNK 256 /* 16 ms at 16 kHz */

static int all_zero(const void *p, size_t n)
{
    const uint8_t *b = p;
    size_t i;
    for (i = 0; i < n; i++) {
        if (b[i] != 0) {
            return 0;
        }
    }
    return 1;
}

/* One turn of the port's loop: the tick, then the screen taken and drawn. */
static void tick(void)
{
    now += 16;
    ds_app_tick(&app, now);
    if (ds_app_take_dirty(&app)) {
        ds_app_draw(&app, fb);
    }
}

static void idle(uint32_t ms)
{
    uint32_t t;
    for (t = 0; t < ms; t += 16) {
        tick();
    }
}

/* The screen holds this text on some row. */
static int shows(const char *text)
{
    int r;
    for (r = 0; r < DS_UI_ROWS; r++) {
        if (strstr(app.screen.line[r], text) != NULL) {
            return 1;
        }
    }
    return 0;
}

static void press(int btn)
{
    ds_app_button(&app, btn, DS_BTN_PRESS);
    tick();
}

/* Feeds audio while the state stays the same, then 1 s of silence (stopping
 * as soon as the state changes). Returns 1 if the state changed. */
static int feed(const int16_t *x, size_t n)
{
    int st = app.state;
    size_t i;
    static const int16_t silence[CHUNK];
    for (i = 0; i < n && app.state == st; i += CHUNK) {
        ds_app_audio(&app, x + i, n - i < CHUNK ? n - i : CHUNK);
        tick();
    }
    for (i = 0; i < TONES_RATE && app.state == st; i += CHUNK) {
        ds_app_audio(&app, silence, CHUNK);
        tick();
    }
    return app.state != st;
}

static int feed_tones(const char *t)
{
    size_t n = tones_render(t, audio, sizeof audio / sizeof audio[0]);
    return feed(audio, n);
}

/* Waits as a reader would: a screen a job put up takes no button until
 * DS_APP_ARM_MS after it was drawn. */
static void look(void)
{
    idle(DS_APP_ARM_MS + 16);
}

/* Ticks until "Working..." is gone (the heavy work ran). */
static void work_done(void)
{
    int i;
    CHECK(shows("Working..."));
    for (i = 0; i < 4 && shows("Working..."); i++) {
        tick();
    }
    CHECK(!shows("Working..."));
}

/* Types text with the picker: walks the cells row by row (RIGHT along a
 * row, DOWN to the next row or page) until the character, then OK. */
static void type(const char *text)
{
    for (; *text; text++) {
        int steps;
        for (steps = 0; steps < 200 && ds_picker_cell(&app.pick) != *text; steps++) {
            press(steps % 10 == 9 ? DS_BTN_DOWN : DS_BTN_RIGHT);
        }
        CHECK(ds_picker_cell(&app.pick) == *text);
        press(DS_BTN_OK);
    }
}

/* Holds OK: ends an entry. */
static void hold_ok(void)
{
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    tick();
}

/* Takes every echo line; returns 1 if one starts with prefix (copied to
 * echo), and checks none holds a secret. */
static int echo_has(const char *prefix, const char *secret)
{
    int found = 0;
    int32_t n;
    char line[DS_APP_ECHO_MAX];
    while ((n = ds_app_take_echo(&app, line, sizeof line)) != 0) {
        CHECK(n > 0);
        if (n < 0) {
            break;
        }
        CHECK_EQ_INT((size_t)n, strlen(line));
        if (secret != NULL) {
            CHECK(strstr(line, secret) == NULL);
        }
        if (!found && strncmp(line, prefix, strlen(prefix)) == 0) {
            strcpy(echo, line);
            found = 1;
        }
    }
    return found;
}

/* Takes the echo up to the end of the next signed PSBT, decoding its
 * chunks into buf: returns its length (and its id), -2 on an abort, -1 if
 * it is not there or broken. Other lines in between are skipped. */
static int32_t take_signed(uint8_t *buf, size_t cap, uint32_t *id)
{
    char line[DS_APP_ECHO_MAX];
    int32_t n;
    size_t len = 0;
    unsigned begin_id = 0, end_id = 0, bytes = 0;
    int begun = 0;

    while ((n = ds_app_take_echo(&app, line, sizeof line)) > 0) {
        CHECK(strstr(line, "TEST") == NULL);
        if (sscanf(line, "signed-psbt-begin %u %u", &begin_id, &bytes) == 2) {
            CHECK(!begun);
            begun = 1;
            len = 0;
        } else if (strncmp(line, "signed-psbt ", 12) == 0) {
            int32_t k;
            CHECK(begun);
            CHECK(strlen(line + 12) <= 64);
            k = ds_base64_decode(line + 12, strlen(line + 12), buf + len, cap - len);
            CHECK(k > 0);
            if (k <= 0) {
                return -1;
            }
            len += (size_t)k;
        } else if (sscanf(line, "signed-psbt-end %u", &end_id) == 1) {
            CHECK(begun);
            CHECK_EQ_INT(end_id, begin_id);
            CHECK_EQ_INT(len, bytes);
            *id = end_id;
            return begun && end_id == begin_id && len == bytes ? (int32_t)len : -1;
        } else if (strncmp(line, "signed-psbt-abort ", 18) == 0) {
            return -2;
        }
    }
    CHECK(n == 0);
    return -1;
}

/* Collected echo lines, for checks across a flow. */
static char seen[8192];

static void collect_echo(void)
{
    int32_t n;
    char line[DS_APP_ECHO_MAX];
    while ((n = ds_app_take_echo(&app, line, sizeof line)) > 0) {
        size_t l = strlen(seen);
        if (l + (size_t)n + 2 < sizeof seen) {
            memcpy(seen + l, line, (size_t)n);
            seen[l + (size_t)n] = '\n';
            seen[l + (size_t)n + 1] = '\0';
        }
    }
}

static const psbt_vector *vector(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof psbt_vectors / sizeof psbt_vectors[0]; i++) {
        if (strcmp(psbt_vectors[i].name, name) == 0) {
            return &psbt_vectors[i];
        }
    }
    CHECK(0);
    return &psbt_vectors[0];
}

/* HOME -> Load seed -> the seed 00..00 with PIN "TEST" -> Seed fingerprint. */
static void to_seed_fp(void)
{
    ds_app_init(&app, TONES_RATE);
    now = 1000;
    tick();
    CHECK(shows("Load seed"));
    CHECK_EQ_INT(ds_app_contrast(&app), DS_APP_UI_CONTRAST);
    press(DS_BTN_OK);
    CHECK(shows("Listening (cable)"));
    CHECK(shows("waiting"));
    CHECK(shows("keys 0  lost 0"));
    CHECK(tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones) > 0);
    CHECK(feed_tones(tones));
    CHECK(shows("PIN"));
    CHECK(shows("OK add, hold OK: none"));
    type("TEST");
    CHECK(shows("TEST_")); /* in the clear: only the user sees the screen */
    hold_ok();
    /* The tick after the button only shows "Working...": nothing done yet. */
    CHECK(shows("Working..."));
    CHECK_EQ_INT(app.s.state, DS_S_EMPTY);
    work_done();
    CHECK(shows("Seed fingerprint"));
    CHECK(shows("73c5da0a"));
    CHECK(shows("OK: continue"));
    CHECK(shows("<: discard"));
    CHECK(all_zero(&app.pick, offsetof(ds_picker, kind)));
    CHECK(all_zero(&app.rx, sizeof app.rx));
}

/* Seed fingerprint -> passphrase (NULL: none) -> READY. */
static void to_ready(const char *passphrase, uint32_t fp)
{
    char hex[9];
    to_seed_fp();
    look();
    press(DS_BTN_OK);
    CHECK(shows("Passphrase?"));
    CHECK(shows("OK: none"));
    CHECK(shows(">: type it"));
    if (passphrase != NULL) {
        press(DS_BTN_RIGHT);
        type(passphrase);
        CHECK(shows(passphrase));
        hold_ok();
    } else {
        press(DS_BTN_OK); /* none */
    }
    work_done();
    CHECK(all_zero(&app.pick, offsetof(ds_picker, kind)));
    CHECK(shows("Wallet fingerprint"));
    snprintf(hex, sizeof hex, "%08x", (unsigned)fp);
    CHECK(shows(hex));
    CHECK_EQ_INT(app.s.state, DS_S_READY);
    look();
    press(DS_BTN_OK);
    CHECK(shows("Wallet "));
    CHECK(shows(hex));
    CHECK(shows("Sign PSBT"));
    CHECK(shows("Show XPUB"));
    CHECK(shows("Wipe"));
}

/* READY -> Sign PSBT -> the vector's parts until the app stops listening. */
static void send_psbt(const psbt_vector *v)
{
    int32_t n = ut_psbt_to_cbor(v->psbt, v->psbt_len, cbor, sizeof cbor);
    uint32_t i;
    CHECK(n > 0);
    CHECK(ut_fountain_encoder_init(&enc, cbor, (size_t)n, 100) >= 0);
    CHECK(shows("Sign PSBT"));
    for (i = 0; i < 8 && !(shows("Sign PSBT") && app.screen.highlight >= 0 &&
                           strstr(app.screen.line[app.screen.highlight], "Sign PSBT"));
         i++) {
        press(DS_BTN_UP);
    }
    press(DS_BTN_OK);
    CHECK(shows("Listening (cable)"));
    CHECK(shows("parts 0/"));
    for (i = 0; i < 10 * enc.seq_len && shows("Listening (cable)"); i++) {
        CHECK(tones_next_part(&enc, tones, sizeof tones) > 0);
        feed_tones(tones);
    }
    CHECK(shows("Working..."));
}

static void test_app_seed_flow(void)
{
    seen[0] = '\0';
    to_ready(NULL, 0x73c5da0au);
    collect_echo();
    CHECK(strstr(seen, "t pin ") != NULL);
    CHECK(strstr(seen, "t derive ") != NULL);
    CHECK(strstr(seen, "fingerprint 73c5da0a") != NULL);
    CHECK(strstr(seen, "TEST") == NULL);
    CHECK(app.s.w.wallet_fp == 0x73c5da0au);
    ds_app_init(&app, TONES_RATE);
}

static void test_app_seed_discard(void)
{
    to_seed_fp();
    look();
    press(DS_BTN_LEFT);
    CHECK(shows("Load seed"));
    CHECK(app.s.state != DS_S_SEED && app.s.state != DS_S_READY);
    CHECK(all_zero(&app.s, sizeof app.s));
    CHECK(all_zero(&app.rx, sizeof app.rx));
    CHECK(all_zero(&app.pick, offsetof(ds_picker, kind)));
    ds_app_init(&app, TONES_RATE);
}

static void test_app_passphrase(void)
{
    ds_wallet w;
    uint32_t fp;
    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 16, "TREZOR", 0), DS_OK);
    fp = w.wallet_fp;
    ds_wallet_wipe(&w);
    CHECK(fp != 0x73c5da0au);
    seen[0] = '\0';
    to_ready("TREZOR", fp);
    collect_echo();
    CHECK(strstr(seen, "TREZOR") == NULL);
    CHECK(strstr(seen, "TEST") == NULL);
    ds_app_init(&app, TONES_RATE);
}

static int32_t mem_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    memcpy(buf, (const uint8_t *)ctx + off, n);
    return (int32_t)n;
}

typedef struct {
    uint8_t *p;
    size_t len, cap;
} mem_dst;

static int32_t mem_write(void *ctx, const uint8_t *p, size_t n)
{
    mem_dst *d = ctx;
    if (d->len + n > d->cap) {
        return DS_ERR_SPACE;
    }
    memcpy(d->p + d->len, p, n);
    d->len += n;
    return (int32_t)n;
}

/* The vector signed directly with the core, into want. */
static size_t sign_direct(const psbt_vector *v)
{
    static ds_psbt_review r;
    ds_wallet w;
    ds_source src;
    ds_sink snk;
    mem_dst d = {want, 0, sizeof want};
    CHECK_EQ_INT(ds_wallet_load(&w, v->entropy, v->entropy_len, NULL, v->testnet), DS_OK);
    src.read = mem_read;
    src.ctx = (void *)v->psbt;
    src.len = (uint32_t)v->psbt_len;
    snk.write = mem_write;
    snk.ctx = &d;
    CHECK_EQ_INT(ds_psbt_analyze(&src, &w, &r), DS_OK);
    CHECK_EQ_INT(ds_psbt_sign(&src, &w, &r, &snk), DS_OK);
    ds_wallet_wipe(&w);
    return d.len;
}

static void lower(char *s)
{
    for (; *s; s++) {
        if (*s >= 'A' && *s <= 'Z') {
            *s = (char)(*s - 'A' + 'a');
        }
    }
}

/* The QR screen says how many parts there are and which one is shown,
 * cycling, and after the first pass that they loop: never a count that only
 * grows. */
static void check_qr_text(size_t i)
{
    char want_s[16];
    uint32_t n = app.qr.e.seq_len;
    if (n == 1) {
        CHECK(shows("1 part"));
        CHECK(!shows("looping"));
        return;
    }
    snprintf(want_s, sizeof want_s, "%u parts", (unsigned)n);
    CHECK(shows(want_s));
    snprintf(want_s, sizeof want_s, "part %u/%u", (unsigned)(i % n + 1), (unsigned)n);
    CHECK(shows(want_s));
    CHECK(shows("looping") == (i >= n));
}

/* Reads the QR's URs for seq_len + 3 parts of 250 ms: the decoded message. */
static size_t read_qr(const char *type, const uint8_t **msg)
{
    const char *t = NULL;
    size_t len = 0, i, parts;
    int32_t r = 0;
    char ur[DS_QR_UR_MAX];
    CHECK(ut_fountain_decoder_init(&dec, work, sizeof work) >= 0);
    parts = app.qr.e.seq_len + 3;
    for (i = 0; i < parts; i++) {
        if (i > 0) {
            now += DS_APP_PART_MS;
            ds_app_tick(&app, now);
        }
        CHECK(app.screen.qr);
        CHECK_EQ_INT(app.qr.shown, i + 1);
        check_qr_text(i);
        strcpy(ur, app.qr.ur);
        lower(ur);
        r = ut_fountain_receive(&dec, ur);
        CHECK(r >= 0);
    }
    CHECK_EQ_INT(r, 1);
    CHECK(ut_fountain_result(&dec, &t, msg, &len) >= 0);
    CHECK(t != NULL && strcmp(t, type) == 0);
    return len;
}

/* Walks the pager to its last page. */
static void read_all_pages(void)
{
    uint32_t i;
    idle(DS_APP_ARM_MS); /* a reader takes longer than this */
    for (i = 0; i < app.pager.pages; i++) {
        press(DS_BTN_DOWN);
    }
    CHECK(shows("Hold OK: sign"));
}

/* Goes to the last card at once (the presses come before DS_APP_ARM_MS, so
 * they do not arm the review). */
static void to_last_card(void)
{
    uint32_t i;
    for (i = 0; i + 1 < app.pager.pages; i++) {
        ds_app_button(&app, DS_BTN_DOWN, DS_BTN_PRESS);
    }
    tick();
    CHECK_EQ_INT(app.pager.page + 1, app.pager.pages);
}

static void test_app_sign_flow(void)
{
    const psbt_vector *v = vector("one input with change");
    const uint8_t *m = NULL, *p = NULL;
    size_t m_len, p_len, w_len;
    int32_t n;
    uint32_t id = 0;

    w_len = sign_direct(v);
    to_ready(NULL, 0x73c5da0au);
    send_psbt(v);
    work_done();
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);
    CHECK(app.pager.pages > 1);
    CHECK_EQ_INT(app.pager.page, 0);
    CHECK(shows("Send to"));

    /* Holding OK on page 1 does nothing. */
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    tick();
    tick();
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);
    CHECK_EQ_INT(app.pager.page, 0);
    CHECK(all_zero(app.out, sizeof app.out));

    /* A press of OK on the last page does nothing either. */
    read_all_pages();
    press(DS_BTN_OK);
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);

    /* Every page seen, but back on page 1: holding OK does nothing. */
    while (app.pager.page != 0) {
        press(DS_BTN_UP);
    }
    CHECK(app.pager.seen_last);
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    tick();
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);
    CHECK_EQ_INT(app.pager.page, 0);
    read_all_pages();

    /* On the last page, after every page: hold OK signs. */
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    CHECK(shows("Working..."));
    CHECK_EQ_INT(app.s.state, DS_S_CONFIRMED);
    work_done();
    CHECK(shows("Signed"));
    CHECK(app.screen.qr);
    CHECK_EQ_INT(app.s.state, DS_S_READY);
    CHECK(app.qr_scale == 1 || app.qr_scale == 2);
    CHECK(!shows("1px") && !shows("2px")); /* a test detail, not for the user */
    CHECK_EQ_INT(ds_app_contrast(&app), 0x20);
    ds_app_draw(&app, fb);
    CHECK(!all_zero(fb, sizeof fb));

    m_len = read_qr("crypto-psbt", &m);
    CHECK(ut_cbor_to_psbt(m, m_len, &p, &p_len) >= 0);
    CHECK_EQ_INT(p_len, w_len);
    CHECK(p_len == w_len && memcmp(p, want, w_len) == 0);

    n = take_signed(raw, sizeof raw, &id);
    CHECK_EQ_INT(n, (int32_t)w_len);
    CHECK_EQ_INT(id, 1);
    CHECK(n > 0 && memcmp(raw, want, w_len) == 0);

    /* LEFT leaves the signed screen: out and cbor are zeroed. */
    press(DS_BTN_LEFT);
    CHECK(shows("Sign PSBT"));
    CHECK(all_zero(app.out, sizeof app.out));
    CHECK(all_zero(app.cbor, sizeof app.cbor));
    CHECK_EQ_INT(app.out_len, 0);

    /* A second PSBT signed after the first: its own bytes, id 2. */
    v = vector("fee above 10% of external outputs");
    w_len = sign_direct(v);
    send_psbt(v);
    work_done();
    read_all_pages();
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    work_done();
    CHECK(shows("Signed"));
    m_len = read_qr("crypto-psbt", &m);
    CHECK(ut_cbor_to_psbt(m, m_len, &p, &p_len) >= 0);
    CHECK(p_len == w_len && memcmp(p, want, w_len) == 0);
    n = take_signed(raw, sizeof raw, &id);
    CHECK_EQ_INT(n, (int32_t)w_len);
    CHECK_EQ_INT(id, 2);
    CHECK(n > 0 && memcmp(raw, want, w_len) == 0);
    press(DS_BTN_LEFT);
    CHECK(all_zero(app.out, sizeof app.out));
    ds_app_init(&app, TONES_RATE);
}

/* An OK held from "Working..." into the review, and taps carried over from
 * before it: the review has several cards, so the hold alone cannot sign,
 * and taps that came too soon do not arm it, even on the last card. */
static void test_app_single_page(void)
{
    const psbt_vector *v = vector("no external outputs, fee under the limit");
    const uint8_t *m = NULL, *p = NULL;
    size_t m_len, p_len, w_len = sign_direct(v);
    uint32_t id = 0;

    /* OK held from "Working..." into the review: its LONG arrives once the
     * one-page review is up. It must not sign. */
    to_ready(NULL, 0x73c5da0au);
    send_psbt(v);
    work_done();
    CHECK(app.pager.pages >= 3);
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    tick();
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);
    to_last_card();
    CHECK(app.pager.seen_last);
    CHECK(shows("Tap OK, then hold OK"));
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    tick();
    tick();
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);
    CHECK(!shows("Working..."));
    CHECK(all_zero(app.out, sizeof app.out));
    /* Released after the LONG (no event), held again: still a LONG with no
     * PRESS taken in the review, so still nothing. */
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    tick();
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);

    /* A tap well after the review was drawn, then a fresh hold: signs. */
    idle(DS_APP_ARM_MS);
    press(DS_BTN_OK);
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);
    CHECK(shows("Hold OK: sign"));
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    CHECK_EQ_INT(app.s.state, DS_S_CONFIRMED);
    work_done();
    CHECK(shows("Signed"));
    m_len = read_qr("crypto-psbt", &m);
    CHECK(ut_cbor_to_psbt(m, m_len, &p, &p_len) >= 0);
    CHECK(p_len == w_len && memcmp(p, want, w_len) == 0);
    CHECK_EQ_INT(take_signed(raw, sizeof raw, &id), (int32_t)w_len);
    ds_app_init(&app, TONES_RATE);
}

/* Events reach ds_app late (ADC ring lag, debounce): a tap delivered just
 * after the review appeared may have been made during "Working...". Only a
 * PRESS delivered DS_APP_ARM_MS after the port took the review arms it. */
static void test_app_arm_delay(void)
{
    const psbt_vector *v = vector("no external outputs, fee under the limit");
    int i;

    to_ready(NULL, 0x73c5da0au);
    send_psbt(v);
    /* Ticks until the review is up (the helper takes and draws it). */
    for (i = 0; i < 4 && !app.screen.review; i++) {
        tick();
    }
    to_last_card();
    CHECK(shows("Tap OK, then hold OK"));
    /* A PRESS before any tick in the review, then a LONG: no sign. */
    ds_app_button(&app, DS_BTN_OK, DS_BTN_PRESS);
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    tick();
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);
    CHECK(shows("Tap OK, then hold OK"));
    /* A PRESS 100 ms into the review, then a LONG: no sign. */
    idle(100);
    ds_app_button(&app, DS_BTN_OK, DS_BTN_PRESS);
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    tick();
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);
    /* A PRESS after DS_APP_ARM_MS, then a LONG: signs. */
    idle(DS_APP_ARM_MS);
    ds_app_button(&app, DS_BTN_OK, DS_BTN_PRESS);
    tick();
    CHECK(shows("Hold OK: sign"));
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    CHECK_EQ_INT(app.s.state, DS_S_CONFIRMED);
    work_done();
    CHECK(shows("Signed"));
    ds_app_init(&app, TONES_RATE);

    /* A port that never took the review screen: nothing arms it. */
    to_ready(NULL, 0x73c5da0au);
    send_psbt(v);
    for (i = 0; i < 100; i++) {
        now += 16;
        ds_app_tick(&app, now); /* never taken, never drawn */
    }
    for (i = 0; i + 1 < (int)app.pager.pages; i++) {
        ds_app_button(&app, DS_BTN_DOWN, DS_BTN_PRESS);
    }
    now += 16;
    ds_app_tick(&app, now);
    CHECK(shows("Tap OK, then hold OK"));
    ds_app_button(&app, DS_BTN_OK, DS_BTN_PRESS);
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    now += 16;
    ds_app_tick(&app, now);
    CHECK_EQ_INT(app.s.state, DS_S_REVIEW);
    ds_app_init(&app, TONES_RATE);
}

/* Leaving the signed screen before the echo is taken: an abort, never the
 * bytes of another PSBT. */
static void test_app_echo_abort(void)
{
    uint32_t id = 0;
    to_ready(NULL, 0x73c5da0au);
    send_psbt(vector("one input with change"));
    work_done();
    read_all_pages();
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    work_done();
    CHECK(shows("Signed"));
    {
        char line[DS_APP_ECHO_MAX];
        int k, chunks = 0;
        /* Take a little of it: up to the first chunk. */
        for (k = 0; k < 100 && chunks == 0; k++) {
            CHECK(ds_app_take_echo(&app, line, sizeof line) > 0);
            chunks = strncmp(line, "signed-psbt ", 12) == 0;
        }
        CHECK(chunks);
        CHECK_EQ_INT(ds_app_take_echo(&app, line, 10), DS_ERR_SPACE);
    }
    look();
    press(DS_BTN_LEFT);
    CHECK_EQ_INT(take_signed(raw, sizeof raw, &id), -2);
    ds_app_init(&app, TONES_RATE);
}

/* A full queue says how many lines it dropped. */
static void test_app_echo_dropped(void)
{
    char line[DS_APP_ECHO_MAX];
    unsigned dropped = 0;
    int i;
    ds_app_init(&app, TONES_RATE);
    now = 0;
    tick();
    for (i = 0; i < 100; i++) {
        press(DS_BTN_OK);   /* Load seed */
        press(DS_BTN_LEFT); /* back HOME */
    }
    CHECK(ds_app_take_echo(&app, line, sizeof line) > 0);
    CHECK(sscanf(line, "echo-dropped %u", &dropped) == 1);
    CHECK(dropped > 100);
    CHECK(ds_app_take_echo(&app, line, sizeof line) > 0);
    CHECK(strncmp(line, "state ", 6) == 0);
    ds_app_init(&app, TONES_RATE);
}

/* Four QR parts a second, with no drift at 16 ms ticks, and no burst after
 * a stall. */
static void test_app_qr_pace(void)
{
    int i;
    uint32_t shown;
    to_ready(NULL, 0x73c5da0au);
    press(DS_BTN_DOWN);
    press(DS_BTN_OK); /* Show XPUB */
    work_done();
    shown = app.qr.shown;
    for (i = 0; i < 63; i++) {
        tick(); /* 1008 ms */
    }
    CHECK_EQ_INT(app.qr.shown - shown, 4);
    for (i = 0; i < 1000 / 16 * 9; i++) {
        tick();
    }
    shown = app.qr.shown;
    now += 3000; /* a stalled loop */
    ds_app_tick(&app, now);
    CHECK_EQ_INT(app.qr.shown - shown, 1);
    tick();
    tick();
    CHECK_EQ_INT(app.qr.shown - shown, 1);
    ds_app_init(&app, TONES_RATE);
}

static void check_wiped(void)
{
    CHECK(shows("Wiped"));
    CHECK_EQ_INT(ds_app_wants_reboot(&app), 1);
    CHECK(all_zero(&app.s, sizeof app.s));
    CHECK(all_zero(&app.rx, sizeof app.rx));
    CHECK(all_zero(app.out, sizeof app.out));
    CHECK(all_zero(app.cbor, sizeof app.cbor));
    CHECK(all_zero(&app.qr, sizeof app.qr));
    CHECK(all_zero(&app.pick, offsetof(ds_picker, kind)));
    CHECK_EQ_INT(app.job, 0);
    idle(100);
    CHECK(shows("Wiped"));
    CHECK(all_zero(&app.s, sizeof app.s));
    CHECK(all_zero(app.out, sizeof app.out));
}

/* WIPE while working, on the XPUB and signed screens and on an error. */
static void test_app_wipe_anywhere(void)
{
    /* While "Working..." (the review pending). */
    to_ready(NULL, 0x73c5da0au);
    send_psbt(vector("one input with change"));
    CHECK(shows("Working..."));
    ds_app_button(&app, DS_BTN_LEFT, DS_BTN_WIPE);
    check_wiped();

    /* While "Working..." for the signature (confirmed, not signed). */
    to_ready(NULL, 0x73c5da0au);
    send_psbt(vector("one input with change"));
    work_done();
    read_all_pages();
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    CHECK_EQ_INT(app.s.state, DS_S_CONFIRMED);
    ds_app_button(&app, DS_BTN_LEFT, DS_BTN_WIPE);
    check_wiped();

    /* On the XPUB screen. */
    to_ready(NULL, 0x73c5da0au);
    press(DS_BTN_DOWN);
    press(DS_BTN_OK);
    work_done();
    CHECK(shows("XPUB"));
    ds_app_button(&app, DS_BTN_LEFT, DS_BTN_WIPE);
    check_wiped();

    /* On the signed screen. */
    to_ready(NULL, 0x73c5da0au);
    send_psbt(vector("one input with change"));
    work_done();
    read_all_pages();
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    work_done();
    CHECK(shows("Signed"));
    ds_app_button(&app, DS_BTN_LEFT, DS_BTN_WIPE);
    check_wiped();

    /* On an error screen. */
    to_ready(NULL, 0x73c5da0au);
    send_psbt(vector("fake change: our path, foreign key"));
    work_done();
    CHECK(shows("Rejected"));
    ds_app_button(&app, DS_BTN_LEFT, DS_BTN_WIPE);
    check_wiped();
    ds_app_init(&app, TONES_RATE);
}

static void test_app_timings(void)
{
    /* Every heavy step echoes its time. */
    const psbt_vector *v = vector("one input with change");
    seen[0] = '\0';
    to_ready(NULL, 0x73c5da0au);
    send_psbt(v);
    work_done();
    read_all_pages();
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    work_done();
    tick();
    collect_echo();
    CHECK(strstr(seen, "t pin ") != NULL);
    CHECK(strstr(seen, "t derive ") != NULL);
    CHECK(strstr(seen, "t review ") != NULL);
    CHECK(strstr(seen, "t sign ") != NULL);
    CHECK(strstr(seen, "t qr ") != NULL);
    CHECK(strstr(seen, "signed-psbt-begin 1 ") != NULL);
    CHECK(strstr(seen, "signed-psbt ") != NULL);
    CHECK(strstr(seen, "signed-psbt-end 1") != NULL);
    CHECK(strstr(seen, "TEST") == NULL);
    ds_app_init(&app, TONES_RATE);
}

static void test_app_reject(void)
{
    to_ready(NULL, 0x73c5da0au);
    send_psbt(vector("fake change: our path, foreign key"));
    work_done();
    CHECK(shows("Rejected"));
    CHECK(shows("DS_ERR_CHANGE"));
    CHECK_EQ_INT(app.s.state, DS_S_READY);
    CHECK(all_zero(app.out, sizeof app.out));
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG); /* no way to sign from here */
    tick();
    CHECK_EQ_INT(app.s.state, DS_S_READY);
    CHECK(all_zero(app.out, sizeof app.out));
    look();
    press(DS_BTN_OK);
    CHECK(shows("Sign PSBT"));
    ds_app_init(&app, TONES_RATE);
}

/* A tap carried over from "Working..." (its event lags the samples) must
 * not act on the screen the job put up before the user could see it: the
 * seed fingerprint, the wallet fingerprint and the error screens take a
 * button only DS_APP_ARM_MS after the port drew them. */
static void test_app_job_screens_wait(void)
{
    int i;

    /* Seed fingerprint: neither OK nor LEFT acts at once or 100 ms in. */
    to_seed_fp();
    press(DS_BTN_OK);
    CHECK(shows("Seed fingerprint"));
    press(DS_BTN_LEFT);
    CHECK(shows("Seed fingerprint"));
    CHECK_EQ_INT(app.s.state, DS_S_SEED);
    idle(100);
    press(DS_BTN_OK);
    CHECK(shows("Seed fingerprint"));
    look();
    press(DS_BTN_OK);
    CHECK(shows("Passphrase?"));

    /* Wallet fingerprint. */
    press(DS_BTN_OK);
    work_done();
    CHECK(shows("Wallet fingerprint"));
    press(DS_BTN_OK);
    CHECK(shows("Wallet fingerprint"));
    look();
    press(DS_BTN_OK);
    CHECK(shows("Sign PSBT"));

    /* An error screen. */
    send_psbt(vector("fake change: our path, foreign key"));
    work_done();
    CHECK(shows("Rejected"));
    press(DS_BTN_OK);
    CHECK(shows("Rejected"));
    look();
    press(DS_BTN_OK);
    CHECK(shows("Sign PSBT"));
    ds_app_init(&app, TONES_RATE);

    /* A port that never takes the screen: no button ever acts on it. */
    ds_app_init(&app, TONES_RATE);
    now = 1000;
    tick();
    press(DS_BTN_OK);
    CHECK(tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones) > 0);
    CHECK(feed_tones(tones));
    type("TEST");
    hold_ok();
    CHECK(shows("Working..."));
    for (i = 0; i < 100; i++) {
        now += 16;
        ds_app_tick(&app, now); /* never taken, never drawn */
    }
    CHECK(shows("Seed fingerprint"));
    ds_app_button(&app, DS_BTN_OK, DS_BTN_PRESS);
    ds_app_button(&app, DS_BTN_LEFT, DS_BTN_PRESS);
    CHECK(shows("Seed fingerprint"));
    CHECK_EQ_INT(app.s.state, DS_S_SEED);
    ds_app_init(&app, TONES_RATE);
}

static void test_app_cancel(void)
{
    to_ready(NULL, 0x73c5da0au);
    send_psbt(vector("one input with change"));
    work_done();
    CHECK_EQ_INT(app.pager.page, 0);
    read_all_pages();
    press(DS_BTN_LEFT);
    CHECK(shows("Sign PSBT"));
    CHECK(shows("Wallet 73c5da0a"));
    CHECK_EQ_INT(app.s.state, DS_S_READY);
    CHECK(all_zero(app.out, sizeof app.out));
    CHECK(all_zero(app.cbor, sizeof app.cbor));
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    tick();
    CHECK_EQ_INT(app.s.state, DS_S_READY);
    ds_app_init(&app, TONES_RATE);
}

static void test_app_wrong_data(void)
{
    uint32_t i;
    to_ready(NULL, 0x73c5da0au);
    CHECK(tones_seed_ur(zero16, 16, NULL, tones, sizeof tones) > 0);
    press(DS_BTN_OK); /* Sign PSBT */
    CHECK(shows("Listening (cable)"));
    {
        size_t n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
        for (i = 0; i < n; i += CHUNK) {
            ds_app_audio(&app, audio + i, n - i < CHUNK ? n - i : CHUNK);
            tick();
            if (shows("Not a PSBT")) {
                break;
            }
        }
        for (i = 0; i < 100 && !shows("Not a PSBT"); i++) {
            static const int16_t silence[CHUNK];
            ds_app_audio(&app, silence, CHUNK);
            tick();
        }
    }
    CHECK(shows("Not a PSBT"));
    CHECK(shows("Listening (cable)"));
    idle(DS_APP_MSG_MS + 32);
    CHECK(!shows("Not a PSBT"));
    CHECK(shows("Listening (cable)"));

    /* Listening went on: a PSBT still arrives. */
    {
        const psbt_vector *v = vector("one input with change");
        int32_t n = ut_psbt_to_cbor(v->psbt, v->psbt_len, cbor, sizeof cbor);
        CHECK(ut_fountain_encoder_init(&enc, cbor, (size_t)n, 100) >= 0);
        for (i = 0; i < 10 * enc.seq_len && shows("Listening (cable)"); i++) {
            CHECK(tones_next_part(&enc, tones, sizeof tones) > 0);
            feed_tones(tones);
        }
        CHECK(shows("Working..."));
        work_done();
        CHECK_EQ_INT(app.pager.page, 0);
    }
    ds_app_init(&app, TONES_RATE);
}

static void test_app_wrong_seed_data(void)
{
    /* A PSBT part while waiting for a seed: "Not a seed", listening goes on. */
    const psbt_vector *v = vector("one input with change");
    int32_t n = ut_psbt_to_cbor(v->psbt, v->psbt_len, cbor, sizeof cbor);
    size_t k, len;
    int got = 0;
    static const int16_t silence[CHUNK];

    ds_app_init(&app, TONES_RATE);
    now = 1000;
    tick();
    press(DS_BTN_OK);
    CHECK(shows("Listening (cable)"));
    CHECK(ut_fountain_encoder_init(&enc, cbor, (size_t)n, 100) >= 0);
    CHECK(tones_next_part(&enc, tones, sizeof tones) > 0);
    len = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
    for (k = 0; k < len + TONES_RATE && !got; k += CHUNK) {
        if (k < len) {
            ds_app_audio(&app, audio + k, len - k < CHUNK ? len - k : CHUNK);
        } else {
            ds_app_audio(&app, silence, CHUNK);
        }
        tick();
        got = shows("Not a seed");
    }
    CHECK(got);
    CHECK(shows("Listening (cable)"));
    idle(DS_APP_MSG_MS + 32);
    CHECK(!shows("Not a seed"));
    CHECK(shows("Listening (cable)"));

    /* Audio lost by the port: counted on screen, listening goes on. */
    ds_app_audio_lost(&app, TONES_RATE / 4);
    tick();
    CHECK(shows("lost 1"));
    CHECK(tones_seed_keypad(zero16, 16, NULL, tones, sizeof tones) > 0);
    /* A keypad seed without PIN, at keypad pace (tones_render: 80 ms + 80 ms,
     * its digits repeat). */
    CHECK(feed_tones(tones));
    CHECK(shows("hold OK: none"));
    hold_ok(); /* no PIN */
    work_done();
    CHECK(shows("Seed fingerprint"));
    CHECK(shows("73c5da0a"));
    ds_app_init(&app, TONES_RATE);
}

static void test_app_wipe(void)
{
    to_ready(NULL, 0x73c5da0au);
    send_psbt(vector("one input with change"));
    work_done();
    CHECK_EQ_INT(app.pager.page, 0);
    ds_app_button(&app, DS_BTN_LEFT, DS_BTN_LONG); /* on the way to WIPE */
    tick();
    CHECK_EQ_INT(app.pager.page, 0);
    ds_app_button(&app, DS_BTN_LEFT, DS_BTN_WIPE);
    tick();
    CHECK(shows("Wiped"));
    CHECK_EQ_INT(ds_app_wants_reboot(&app), 1);
    CHECK(all_zero(&app.s, sizeof app.s));
    CHECK(all_zero(&app.rx, sizeof app.rx));
    CHECK(all_zero(app.out, sizeof app.out));
    CHECK(all_zero(app.cbor, sizeof app.cbor));
    CHECK(all_zero(&app.pick, offsetof(ds_picker, kind)));
    /* Nothing works any more. */
    ds_app_button(&app, DS_BTN_OK, DS_BTN_LONG);
    press(DS_BTN_OK);
    idle(100);
    CHECK(shows("Wiped"));
    CHECK(all_zero(&app.s, sizeof app.s));

    /* Wipe from the menu. */
    to_ready(NULL, 0x73c5da0au);
    while (!(app.screen.highlight >= 0 &&
             strstr(app.screen.line[app.screen.highlight], "Wipe"))) {
        press(DS_BTN_DOWN);
    }
    press(DS_BTN_OK);
    CHECK(shows("Wiped"));
    CHECK_EQ_INT(ds_app_wants_reboot(&app), 1);
    CHECK(all_zero(&app.s, sizeof app.s));
    ds_app_init(&app, TONES_RATE);
    CHECK_EQ_INT(ds_app_wants_reboot(&app), 0);
}

static void test_app_xpub(void)
{
    uint8_t acct[512];
    char desc[200];
    const uint8_t *m = NULL;
    size_t m_len;
    int32_t n;
    ds_wallet w;

    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 16, NULL, 0), DS_OK);
    n = ds_wallet_account_cbor(&w, acct, sizeof acct);
    CHECK(n > 0);
    CHECK(ds_wallet_descriptor(&w, desc, sizeof desc) > 0);
    ds_wallet_wipe(&w);

    to_ready(NULL, 0x73c5da0au);
    (void)echo_has("", NULL);
    press(DS_BTN_DOWN); /* Show XPUB */
    press(DS_BTN_OK);
    work_done();
    CHECK(app.screen.qr);
    CHECK(shows("XPUB"));
    CHECK(shows("73c5da0a"));
    CHECK_EQ_INT(ds_app_contrast(&app), 0x20);
    m_len = read_qr("crypto-account", &m);
    CHECK_EQ_INT(m_len, n);
    CHECK(m_len == (size_t)n && memcmp(m, acct, m_len) == 0);
    CHECK(echo_has("wpkh(", NULL));
    CHECK_EQ_STR(echo, desc);
    press(DS_BTN_LEFT);
    CHECK(shows("Show XPUB"));
    CHECK(!app.screen.qr);
    CHECK_EQ_INT(ds_app_contrast(&app), DS_APP_UI_CONTRAST);
    ds_app_init(&app, TONES_RATE);
}

static void test_app_fits_qr(void)
{
    ds_app_init(&app, TONES_RATE);
    CHECK_EQ_INT(ds_app_fits_qr(&app, 186, 1), 1);
    CHECK_EQ_INT(ds_app_fits_qr(&app, DS_APP_PSBT_MAX - 32 * 112, 32), 1);
    /* With the default limits the largest PSBT the receiver takes, with
     * every input signed, fits at 1 px. */
    CHECK_EQ_INT(ds_app_fits_qr(&app, DS_APP_PSBT_MAX, DS_MAX_INPUTS), 1);
    /* Beyond the limits: never. */
    CHECK_EQ_INT(ds_app_fits_qr(&app, DS_APP_PSBT_MAX + 1, 0), 0);
    CHECK_EQ_INT(ds_app_fits_qr(&app, 100, DS_MAX_INPUTS + 1), 0);
    CHECK(all_zero(app.cbor, sizeof app.cbor));
    CHECK(app.qr.type == NULL);
}

static void test_app_settings(void)
{
    ds_app_init(&app, TONES_RATE);
    now = 0;
    tick();
    ds_app_audio_lost(&app, 100); /* not listening: ignored */
    CHECK_EQ_INT(app.lost, 0);
    CHECK(shows("Network: mainnet"));
    CHECK(shows("QR contrast: 20"));
    CHECK(shows("QR size: 2px"));
    press(DS_BTN_DOWN);
    press(DS_BTN_OK);
    CHECK(shows("Network: testnet"));
    press(DS_BTN_DOWN);
    press(DS_BTN_OK);
    CHECK(shows("QR contrast: 40"));
    press(DS_BTN_DOWN);
    press(DS_BTN_OK);
    CHECK(shows("QR size: 1px"));
    ds_app_button(&app, DS_BTN_DOWN, DS_BTN_PRESS);
    CHECK(ds_app_take_dirty(&app));
    CHECK(!ds_app_take_dirty(&app));
    ds_app_init(&app, TONES_RATE);
}

/* While listening the screen tells whether tones are coming in: "receiving"
 * within 0.5 s of a key, "waiting" otherwise, and the keys heard. A PSBT's
 * last keys are shown as they come; a seed's never are. */
static void test_app_activity(void)
{
    size_t n, i;
    uint32_t keys0;
    static const int16_t silence[CHUNK];

    ds_app_init(&app, TONES_RATE);
    now = 1000;
    tick();
    press(DS_BTN_OK);
    CHECK(shows("waiting"));
    CHECK(!shows("receiving"));
    CHECK(tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones) > 0);
    n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
    for (i = 0; i < n / 2; i += CHUNK) {
        ds_app_audio(&app, audio + i, CHUNK);
        tick();
    }
    CHECK(shows("receiving"));
    CHECK(!shows("keys 0 "));
    CHECK(!shows("Volume up"));
    CHECK_EQ_STR(app.screen.line[2], "");
    for (i = 0; i < TONES_RATE; i += CHUNK) {
        ds_app_audio(&app, silence, CHUNK);
        tick();
    }
    CHECK(shows("waiting"));
    CHECK(shows("Listening (cable)"));
    CHECK(!shows("Volume up"));

    CHECK(!shows("Volume up") && !shows("too quiet"));

    /* The same tones 30 dB quieter (-38.7 dBFS): read, so no warning while
     * they come; cut off, the frame fails, and at that level the screen
     * asks for more volume. */
    for (i = 0; i < n; i++) {
        audio[i] = (int16_t)(audio[i] / 32);
    }
    idle(DS_APP_MSG_MS);
    for (i = 0; i < n / 2; i += CHUNK) {
        ds_app_audio(&app, audio + i, CHUNK);
        tick();
    }
    CHECK(shows("receiving"));
    CHECK(!shows("Volume up") && !shows("too quiet"));
    for (i = 0; i < TONES_RATE; i += CHUNK) {
        ds_app_audio(&app, silence, CHUNK);
        tick();
    }
    CHECK(shows("Not a seed"));
    CHECK(shows("Too quiet: volume up"));
    idle(DS_APP_MSG_MS);
    CHECK(!shows("Volume up"));

    /* Sound too faint for any key (tones at -48 dBFS in noise): not
     * "waiting" but "too quiet to read"; silence again, "waiting". */
    keys0 = app.rx.keys;
    {
        uint32_t x = 1;
        CHECK(tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones) > 0);
        n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
        for (i = 0; i < n; i++) {
            x = x * 1103515245u + 12345u;
            audio[i] = (int16_t)(audio[i] / 256 + (int)((x >> 16) % 601) - 300);
        }
    }
    for (i = 0; i < n / 2; i += CHUNK) {
        ds_app_audio(&app, audio + i, CHUNK);
        tick();
    }
    CHECK_EQ_INT(app.rx.keys, keys0); /* no key at all */
    CHECK(shows("! too quiet to read"));
    CHECK(shows("Volume up"));
    CHECK(!shows("waiting"));
    for (i = 0; i < TONES_RATE; i += CHUNK) {
        ds_app_audio(&app, silence, CHUNK);
        tick();
    }
    CHECK(shows("waiting"));
    CHECK(!shows("Volume up"));
    ds_app_init(&app, TONES_RATE);

    to_ready(NULL, 0x73c5da0au);
    press(DS_BTN_OK); /* Sign PSBT */
    CHECK(shows("waiting"));
    CHECK(shows("parts 0/?"));
    {
        int32_t c = ut_psbt_to_cbor(psbt_vectors[0].psbt, psbt_vectors[0].psbt_len,
                                    cbor, sizeof cbor);
        CHECK(c > 0);
        CHECK(ut_fountain_encoder_init(&enc, cbor, (size_t)c, 100) >= 0);
    }
    CHECK(tones_next_part(&enc, tones, sizeof tones) > 0);
    n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
    for (i = 0; i < n / 2; i += CHUNK) {
        ds_app_audio(&app, audio + i, CHUNK);
        tick();
    }
    CHECK(shows("receiving"));
    CHECK(strlen(app.screen.line[2]) > 0);
    CHECK(strstr(tones, app.screen.line[2]) != NULL);
    ds_app_init(&app, TONES_RATE);
}

TEST_MAIN(test_app_seed_flow, test_app_seed_discard, test_app_passphrase,
          test_app_sign_flow, test_app_timings, test_app_reject, test_app_cancel,
          test_app_wrong_data, test_app_wrong_seed_data, test_app_wipe, test_app_xpub,
          test_app_fits_qr, test_app_settings, test_app_single_page,
          test_app_echo_abort, test_app_echo_dropped, test_app_qr_pace,
          test_app_wipe_anywhere, test_app_arm_delay, test_app_job_screens_wait,
          test_app_activity)
