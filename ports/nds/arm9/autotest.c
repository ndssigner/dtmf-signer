/* Nintendo DS port: the emulator end-to-end test (see autotest.h).
 * Development only: compiled with `make AUTOTEST=1`, never released. Only
 * test vectors here (the generated autotest_vector.h): never a real seed. */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "autotest.h"
#include "autotest_vector.h" /* generated: tools/dev/nds_autotest_vector.py */
#include "ds_app.h"
#include "ds_base64.h"
#include "ds_bitcoin.h"
#include "ds_btn.h"
#include "ds_core.h"
#include "ds_ui.h"
#include "nds_view.h"
#include "ur_tones.h"

#define TAP_DOWN_MS 80     /* a tap: held, then released (ds_btn: 30 ms) */
#define TAP_UP_MS 80
#define HOLD_MS 1800       /* past DS_BTN_LONG_MS */
#define LOOK_MS 700        /* a reader's look at a new screen (> DS_APP_ARM_MS) */
#define SILENCE_MS 1000    /* between two frames of tones */
#define STATE_MAX_MS 60000 /* no screen takes longer, but the PSBT's tones */
#define LISTEN_MAX_MS 300000
#define QR_WAIT_MS 5000
#define MAX_FRAGMENT 100   /* as a sender sends (ur-tones SPEC §2.4) */

static ds_app *app;
static uint32_t rate, now_last;

/* ---- the verdict ---- */

static int done, redraw;
static char verdict[96];
static long t_pin = -1, t_derive = -1, t_sign = -1;

static void say(const char *s)
{
    nocashWrite(s, strlen(s));
    nocashWrite("\n", 1);
}

static void finish(const char *v)
{
    if (done) {
        return;
    }
    done = 1;
    redraw = 1;
    snprintf(verdict, sizeof verdict, "AUTOTEST %s", v);
    say(verdict);
}

static void fail(const char *step)
{
    char v[64];
    snprintf(v, sizeof v, "FAIL %s", step);
    finish(v);
}

/* ---- the keys ---- */

static char state[24] = "boot", fail_step[40];
static uint32_t state_since, actions;
static uint32_t key, key_up_at, key_end_at; /* the action in progress */
static int pin_i, pin_steps, review_tries;

static void tap(uint32_t k, uint32_t now)
{
    key = k;
    key_up_at = now + TAP_DOWN_MS;
    key_end_at = key_up_at + TAP_UP_MS;
    actions++;
}

static void hold(uint32_t k, uint32_t now)
{
    key = k;
    key_up_at = now + HOLD_MS;
    key_end_at = key_up_at + TAP_UP_MS * 2;
    actions++;
}

static int is(const char *s)
{
    return strcmp(state, s) == 0;
}

static int shows(const char *text)
{
    int r;
    for (r = 0; r < DS_UI_ROWS; r++) {
        if (strstr(app->screen.line[r], text) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* OK on the menu row holding label, UP until it is highlighted. */
static void menu(const char *label, uint32_t now)
{
    int h = app->screen.highlight;
    if (h >= 0 && h < DS_UI_ROWS && strstr(app->screen.line[h], label) != NULL) {
        tap(NDS_KEY_A, now);
    } else if (actions > 0 && !shows(label)) {
        fail(state);
    } else {
        tap(NDS_KEY_UP, now);
    }
}

/* ---- the signed PSBT, from the echo ---- */

static uint8_t signed_psbt[DS_APP_OUT_MAX];
static uint8_t cbor[DS_APP_CBOR_MAX];
static size_t signed_len;
static int collecting, collected; /* collected: the end came, to check */
static uint32_t collected_at;

static void hex(const uint8_t *p, size_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2 * i] = H[p[i] >> 4];
        out[2 * i + 1] = H[p[i] & 15];
    }
    out[2 * n] = '\0';
}

/* The echoed PSBT against the host's, then the QR's message against its CBOR. */
static void check_signed(void)
{
    ds_sha256_ctx c;
    uint8_t d[32];
    char h[65], v[80];
    int32_t n;

    ds_sha256_init(&c);
    ds_sha256_update(&c, signed_psbt, signed_len);
    ds_sha256_final(&c, d);
    hex(d, sizeof d, h);
    if (signed_len != AUTOTEST_SIGNED_LEN || strcmp(h, AUTOTEST_SIGNED_SHA256) != 0) {
        fail("signed-psbt");
        return;
    }
    n = ut_psbt_to_cbor(signed_psbt, signed_len, cbor, sizeof cbor);
    if (n <= 0 || ds_app_qr_kind(app) != DS_QR_PSBT || !app->screen.qr ||
        app->qr.e.msg == NULL || app->qr.e.len != (uint32_t)n ||
        memcmp(app->qr.e.msg, cbor, (size_t)n) != 0) {
        fail("signed-qr");
        return;
    }
    snprintf(v, sizeof v, "PASS %s", h);
    finish(v);
}

/* ---- the script ---- */

uint32_t nds_autotest_keys(uint32_t now)
{
    uint32_t in_state = now - state_since;

    now_last = now;
    if (now < key_up_at) {
        return key;
    }
    if (now < key_end_at || done) {
        return 0;
    }
    if (actions > 400) {
        fail("too-many-keys");
        return 0;
    }
    if (in_state > (strncmp(state, "listen-", 7) == 0 ? LISTEN_MAX_MS : STATE_MAX_MS)) {
        snprintf(fail_step, sizeof fail_step, "timeout-%s", state);
        fail(fail_step);
        return 0;
    }
    if (in_state < LOOK_MS) {
        return 0;
    }

    if (is("boot") || is("welcome")) { /* ds_app_init echoes no state */
        tap(NDS_KEY_A, now);
    } else if (is("home")) {
        menu("Load seed", now);
    } else if (is("air-warning")) {
        tap(NDS_KEY_A, now);
    } else if (is("pin")) {
        if (pin_i == (int)strlen(AUTOTEST_PIN)) {
            hold(NDS_KEY_A, now); /* the PIN is complete */
        } else if (ds_picker_cell(&app->pick) == AUTOTEST_PIN[pin_i]) {
            tap(NDS_KEY_A, now);
            pin_i++;
            pin_steps = 0;
        } else if (pin_steps++ > 200) {
            fail("pin-typing");
        } else {
            /* as a person walks the cells: along the row, then down */
            tap(pin_steps % 10 == 0 ? NDS_KEY_DOWN : NDS_KEY_RIGHT, now);
        }
    } else if (is("seed-fingerprint")) {
        if (!shows(AUTOTEST_SEED_FP)) {
            fail("seed-fingerprint");
        } else {
            tap(NDS_KEY_A, now);
        }
    } else if (is("passphrase?")) {
        tap(NDS_KEY_A, now); /* none */
    } else if (is("wallet-fingerprint")) {
        if (!shows(AUTOTEST_WALLET_FP)) {
            fail("wallet-fingerprint");
        } else {
            tap(NDS_KEY_A, now);
        }
    } else if (is("ready")) {
        menu("Sign PSBT", now);
    } else if (is("review")) {
        if (app->pager.page + 1 < app->pager.pages) {
            tap(NDS_KEY_DOWN, now); /* every page, as a reader */
        } else if (review_tries >= 6) {
            fail("review");
        } else if (review_tries++ % 2 == 0) {
            tap(NDS_KEY_A, now); /* arms the review, if it is not yet */
        } else {
            hold(NDS_KEY_A, now); /* signs */
        }
    } else if (is("signed")) {
        if (collected && app->screen.qr) {
            collected = 0;
            check_signed();
        } else if (collected && now - collected_at > QR_WAIT_MS) {
            fail("signed-qr");
        }
    }
    /* listen-seed, listen-psbt, working: nothing to press */
    return 0;
}

/* ---- the microphone ---- */

static ut_synth synth;
static ut_fountain_encoder enc;
static char tones[UT_GROUP + 1];
static uint8_t psbt_cbor[DS_APP_CBOR_MAX];
static int source; /* 0 silence, 1 the seed, 2 the PSBT */
static uint32_t mic_t0, silence_left;
static uint64_t mic_given;

/* The next frame of tones of the source, into the synth. */
static int next_frame(void)
{
    char ur[UT_MAX_UR + 1];
    int32_t n;

    if (source == 1) {
        n = ut_seed_to_ur(autotest_entropy, sizeof autotest_entropy, AUTOTEST_PIN, ur, sizeof ur);
        n = n < 0 ? n : ut_ur_to_frame(ur, tones, sizeof tones);
        ds_wipe(ur, sizeof ur);
    } else {
        ut_frame f;
        n = ut_fountain_next_frame(&enc, "crypto-psbt", &f);
        n = n < 0 ? n : ut_frame_encode(&f, tones, sizeof tones);
    }
    if (n <= 0) {
        fail("tones");
        source = 0;
        return 0;
    }
    ut_synth_init(&synth, tones, rate, 40, 20, 400);
    return 1;
}

void nds_autotest_mic_start(uint32_t now)
{
    int32_t n;

    mic_t0 = now;
    mic_given = 0;
    silence_left = 0;
    source = 0;
    if (is("listen-seed")) {
        source = 1;
    } else if (is("listen-psbt")) {
        n = ut_psbt_to_cbor(autotest_psbt, sizeof autotest_psbt, psbt_cbor, sizeof psbt_cbor);
        if (n <= 0 || ut_fountain_encoder_init(&enc, psbt_cbor, (size_t)n, MAX_FRAGMENT) < 0) {
            fail("psbt-tones");
            return;
        }
        source = 2;
    }
    if (source != 0) {
        next_frame();
    }
}

size_t nds_autotest_mic(int16_t *out, size_t cap, uint32_t now)
{
    uint64_t due = (uint64_t)(now - mic_t0) * rate / 1000u;
    size_t n = due > mic_given ? (size_t)(due - mic_given) : 0, i = 0;

    if (n > cap) {
        n = cap;
    }
    while (i < n) {
        size_t k = 0;
        if (source != 0 && silence_left == 0) {
            k = ut_synth_read(&synth, out + i, n - i);
            if (k == 0) { /* the frame is over: a silence, then the next */
                silence_left = rate * SILENCE_MS / 1000u;
            }
        }
        if (k == 0) {
            k = n - i;
            if (source != 0 && k > silence_left) {
                k = silence_left;
            }
            memset(out + i, 0, k * sizeof out[0]);
            if (source != 0) {
                silence_left -= (uint32_t)k;
                if (silence_left == 0 && !next_frame()) {
                    silence_left = 0;
                }
            }
        }
        i += k;
    }
    mic_given += n;
    return n;
}

/* ---- the echo ---- */

void nds_autotest_echo(const char *line)
{
    unsigned a, b;
    long t;

    if (strncmp(line, "state ", 6) == 0) {
        if (strcmp(line + 6, "error") == 0 || strcmp(line + 6, "wiped") == 0) {
            snprintf(fail_step, sizeof fail_step, "%s-after-%s", line + 6, state);
            fail(fail_step);
        }
        snprintf(state, sizeof state, "%s", line + 6);
        state_since = now_last;
        if (is("review")) {
            review_tries = 0;
        }
    } else if (sscanf(line, "t pin %ld", &t) == 1) {
        t_pin = t;
        redraw = 1;
    } else if (sscanf(line, "t derive %ld", &t) == 1) {
        t_derive = t;
        redraw = 1;
    } else if (sscanf(line, "t sign %ld", &t) == 1) {
        t_sign = t;
        redraw = 1;
    } else if (sscanf(line, "signed-psbt-begin %u %u", &a, &b) == 2) {
        collecting = 1;
        signed_len = 0;
    } else if (collecting && strncmp(line, "signed-psbt ", 12) == 0) {
        int32_t k = ds_base64_decode(line + 12, strlen(line + 12), signed_psbt + signed_len,
                                     sizeof signed_psbt - signed_len);
        if (k <= 0) {
            fail("signed-echo");
            collecting = 0;
        } else {
            signed_len += (size_t)k;
        }
    } else if (collecting && sscanf(line, "signed-psbt-end %u", &a) == 1) {
        collecting = 0;
        collected = 1;
        collected_at = now_last;
    } else if (strncmp(line, "signed-psbt-abort", 17) == 0) {
        fail("signed-abort");
    }
}

/* ---- the screen ---- */

int nds_autotest_take_redraw(void)
{
    int r = redraw;
    redraw = 0;
    return r;
}

/* text at 1x (the app's 5x7 font, 6 px a character) from (x, y). */
static void text(uint16_t *px, int x, int y, const char *s, uint16_t on)
{
    for (; *s && x + 5 <= NDS_W; s++, x += 6) {
        const uint8_t *g = ds_glyph(*s);
        int c, r;
        for (c = 0; c < 5; c++) {
            for (r = 0; r < 8; r++) {
                if ((g[c] >> r) & 1) {
                    px[(y + r) * NDS_W + x + c] = on;
                }
            }
        }
    }
}

void nds_autotest_overlay(uint16_t *bottom, uint16_t on)
{
    char l[48];

    snprintf(l, sizeof l, "AUTOTEST t pin %ld derive %ld", t_pin, t_derive);
    text(bottom, 2, 4, l, on);
    snprintf(l, sizeof l, "sign %ld ms (emulator)", t_sign);
    text(bottom, 2, 14, l, on);
    if (done) {
        /* "AUTOTEST PASS <64 hex>" wraps over the last rows (42 a row) */
        size_t n = strlen(verdict);
        snprintf(l, sizeof l, "%.42s", verdict);
        text(bottom, 2, 164, l, on);
        if (n > 42) {
            snprintf(l, sizeof l, "%.42s", verdict + 42);
            text(bottom, 2, 174, l, on);
        }
    }
}

void nds_autotest_init(ds_app *a, uint32_t mic_rate)
{
    app = a;
    rate = mic_rate;
}
