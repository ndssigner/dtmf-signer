/* Screen shots for the documentation: drives the device logic through its
 * screens with test data only (the seed 00..00, PIN TEST, passphrase
 * "Ab 1!", the test PSBT vectors, synthesized tones) and prints each screen
 * as 64 lines of 128 characters ('#' a lit pixel) after a line "== name".
 * tools/render_screens.py turns that into the PNG files of docs/screens.md.
 * Not a test: it stops with a message when a screen is not the one
 * expected. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ds_app.h"
#include "ds_btn.h"
#include "psbt_vectors.h"
#include "tones.h"

static ds_app app;
static int16_t audio[TONES_RATE * 60];
static char tones[UT_GROUP + 1];
static uint8_t cbor[4096];
static ut_fountain_encoder enc;
static uint8_t fb[8][128];
static uint32_t now;

static const uint8_t zero16[16];

#define CHUNK 256

static void expect(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "screens: expected %s\n", what);
        exit(1);
    }
}

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

static void hold(int btn)
{
    ds_app_button(&app, btn, DS_BTN_LONG);
    tick();
}

/* A screen a job put up takes buttons only DS_APP_ARM_MS after it is drawn. */
static void look(void)
{
    idle(DS_APP_ARM_MS + 16);
}

static void work_done(void)
{
    int i;
    for (i = 0; i < 4 && shows("Working..."); i++) {
        tick();
    }
    expect(!shows("Working..."), "the work done");
}

static void shot(const char *name)
{
    int x, y;
    ds_app_draw(&app, fb);
    printf("== %s\n", name);
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++) {
            putchar((fb[y / 8][x] >> (y % 8)) & 1 ? '#' : '.');
        }
        putchar('\n');
    }
}

/* Feeds samples [from, to) of audio, a chunk per tick. */
static void feed_part(size_t from, size_t to)
{
    size_t i;
    for (i = from; i < to; i += CHUNK) {
        ds_app_audio(&app, audio + i, to - i < CHUNK ? to - i : CHUNK);
        tick();
    }
}

static void silence(uint32_t ms)
{
    static const int16_t zeros[CHUNK];
    uint32_t i;
    for (i = 0; i < TONES_RATE / 1000 * ms; i += CHUNK) {
        ds_app_audio(&app, zeros, CHUNK);
        tick();
    }
}

/* Walks the picker's cells to each character, then OK. */
static void type(const char *text)
{
    for (; *text; text++) {
        int steps;
        for (steps = 0; steps < 200 && ds_picker_cell(&app.pick) != *text; steps++) {
            press(steps % 10 == 9 ? DS_BTN_DOWN : DS_BTN_RIGHT);
        }
        expect(ds_picker_cell(&app.pick) == *text, "a character in the picker");
        press(DS_BTN_OK);
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
    expect(0, name);
    return NULL;
}

static void start_psbt(const psbt_vector *v)
{
    int32_t n = ut_psbt_to_cbor(v->psbt, v->psbt_len, cbor, sizeof cbor);
    expect(n > 0 && ut_fountain_encoder_init(&enc, cbor, (size_t)n, 100) >= 0, "a PSBT encoder");
}

/* Every part of the PSBT until the app stops listening. */
static void send_rest(void)
{
    uint32_t i;
    for (i = 0; i < 10 * enc.seq_len && shows("Listening (cable)"); i++) {
        size_t n;
        expect(tones_next_part(&enc, tones, sizeof tones) > 0, "a PSBT part");
        n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
        feed_part(0, n);
        silence(1000);
    }
}

/* HOME -> the seed with PIN TEST -> no passphrase -> READY. */
static void quick_ready(void)
{
    size_t n;
    ds_app_init(&app, TONES_RATE);
    now = 1000;
    tick();
    press(DS_BTN_OK);
    tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones);
    n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
    feed_part(0, n);
    silence(1000);
    type("TEST");
    hold(DS_BTN_OK);
    work_done();
    look();
    press(DS_BTN_OK);
    press(DS_BTN_OK); /* no passphrase */
    work_done();
    look();
    press(DS_BTN_OK);
    expect(shows("Sign PSBT"), "READY");
}

int main(void)
{
    size_t n, i;

    (void)tones_seed_keypad; /* a helper of tones.h not used here */

    /* Loading the seed. */
    ds_app_init(&app, TONES_RATE);
    now = 1000;
    tick();
    shot("home");
    press(DS_BTN_OK);
    shot("seed-waiting");
    tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones);
    n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
    feed_part(0, n / 2);
    shot("seed-receiving");
    feed_part(n / 2, n);
    silence(1000);
    expect(shows("PIN"), "the PIN picker");
    shot("pin-empty");
    type("TE");
    shot("pin-typing");
    type("ST");
    hold(DS_BTN_OK);
    shot("working");
    work_done();
    shot("seed-fingerprint");
    look();
    press(DS_BTN_OK);
    shot("passphrase-ask");
    press(DS_BTN_RIGHT);
    type("Ab");
    shot("passphrase-letters");
    type(" 1");
    shot("passphrase-digits");
    type("!");
    hold(DS_BTN_OK);
    work_done();
    shot("wallet-fingerprint");
    look();
    press(DS_BTN_OK);
    shot("ready");

    /* The xpub. */
    press(DS_BTN_DOWN);
    press(DS_BTN_OK);
    work_done();
    expect(app.screen.qr, "the xpub QR");
    shot("xpub");
    press(DS_BTN_LEFT);

    /* Signing (no passphrase: the vectors are for the wallet 73c5da0a). */
    quick_ready();
    start_psbt(vector("one input with change"));
    press(DS_BTN_OK);
    shot("sign-waiting");
    expect(tones_next_part(&enc, tones, sizeof tones) > 0, "a PSBT part");
    n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
    feed_part(0, n);
    silence(1000);
    expect(tones_next_part(&enc, tones, sizeof tones) > 0, "a PSBT part");
    n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
    feed_part(0, n / 2);
    shot("sign-receiving");
    feed_part(n / 2, n);
    silence(1000);
    send_rest();
    work_done();
    expect(app.pager.pages == 4, "four review cards");
    shot("review-send");
    idle(DS_APP_ARM_MS);
    press(DS_BTN_DOWN);
    shot("review-change");
    press(DS_BTN_DOWN);
    shot("review-fee");
    press(DS_BTN_DOWN);
    shot("review-confirm");
    hold(DS_BTN_OK);
    work_done();
    expect(app.screen.qr, "the signed QR");
    idle(DS_APP_PART_MS * (app.qr.e.seq_len + 1));
    shot("signed");
    press(DS_BTN_LEFT);

    /* What can go wrong. */
    quick_ready();
    start_psbt(vector("fake change: our path, foreign key"));
    press(DS_BTN_OK);
    send_rest();
    work_done();
    shot("rejected");

    ds_app_init(&app, TONES_RATE);
    now = 1000;
    tick();
    press(DS_BTN_OK);
    start_psbt(vector("one input with change"));
    expect(tones_next_part(&enc, tones, sizeof tones) > 0, "a PSBT part");
    n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
    feed_part(0, n);
    silence(500);
    expect(shows("Not a seed"), "Not a seed");
    shot("not-a-seed");
    idle(DS_APP_MSG_MS);

    {
        uint32_t x = 1;
        tones_seed_ur(zero16, 16, "TEST", tones, sizeof tones);
        n = tones_render(tones, audio, sizeof audio / sizeof audio[0]);
        for (i = 0; i < n; i++) {
            x = x * 1103515245u + 12345u;
            audio[i] = (int16_t)(audio[i] / 256 + (int)((x >> 16) % 601) - 300);
        }
    }
    feed_part(0, n / 2);
    expect(shows("too quiet"), "too quiet to read");
    shot("too-quiet");

    ds_app_button(&app, DS_BTN_LEFT, DS_BTN_WIPE);
    tick();
    shot("wiped");
    return 0;
}
