#include <stdint.h>
#include <string.h>

#include "ds_btn.h"
#include "ds_font5x7.h"
#include "ds_psbt.h"
#include "ds_ui.h"
#include "ds_wallet.h"
#include "psbt_vectors.h"
#include "test.h"

static uint8_t fb[8][128];
static ds_screen scr;
static ds_picker pk;
static ds_pager pg;
static char text[2048];

static void test_font(void)
{
    int a, b;
    /* Every printable glyph but the space has pixels, and none repeats. */
    for (a = 1; a < 95; a++) {
        int any = 0, i;
        for (i = 0; i < 5; i++) {
            any |= ds_font5x7[a][i];
            CHECK((ds_font5x7[a][i] & 0x80) == 0);
        }
        CHECK(any);
        for (b = a + 1; b < 95; b++) {
            CHECK(memcmp(ds_font5x7[a], ds_font5x7[b], 5) != 0);
        }
    }
    for (a = 0; a < 5; a++) {
        CHECK_EQ_INT(ds_font5x7[0][a], 0);
    }
}

static void test_render_text(void)
{
    int i, x;

    memset(&scr, 0, sizeof scr);
    scr.highlight = -1;
    strcpy(scr.line[0], "A");
    strcpy(scr.line[2], "HI");
    memset(fb, 0x55, sizeof fb);
    ds_ui_render(&scr, fb);
    for (i = 0; i < 5; i++) {
        CHECK_EQ_INT(fb[0][i], ds_font5x7['A' - 0x20][i]);
    }
    CHECK_EQ_INT(fb[0][5], 0); /* the spacing column */
    for (x = 6; x < 128; x++) {
        CHECK_EQ_INT(fb[0][x], 0); /* the rest of the row is cleared */
    }
    for (i = 0; i < 5; i++) {
        CHECK_EQ_INT(fb[2][6 + i], ds_font5x7['I' - 0x20][i]);
    }
    CHECK_EQ_INT(fb[1][10], 0);

    /* The highlighted row comes out inverted over its whole width. */
    scr.highlight = 2;
    ds_ui_render(&scr, fb);
    for (i = 0; i < 5; i++) {
        CHECK_EQ_INT(fb[2][i], (uint8_t)~ds_font5x7['H' - 0x20][i]);
        CHECK_EQ_INT(fb[2][6 + i], (uint8_t)~ds_font5x7['I' - 0x20][i]);
    }
    CHECK_EQ_INT(fb[2][5], 0xFF);
    CHECK_EQ_INT(fb[2][127], 0xFF);
    CHECK_EQ_INT(fb[0][1], ds_font5x7['A' - 0x20][1]);

    /* The 21st column ends at x 125; non-printable bytes draw as '?'. */
    memset(&scr, 0, sizeof scr);
    scr.highlight = -1;
    memset(scr.line[1], 'W', DS_UI_COLS);
    scr.line[3][0] = '\x01';
    ds_ui_render(&scr, fb);
    for (i = 0; i < 5; i++) {
        CHECK_EQ_INT(fb[1][120 + i], ds_font5x7['W' - 0x20][i]);
        CHECK_EQ_INT(fb[3][i], ds_font5x7['?' - 0x20][i]);
    }
    CHECK_EQ_INT(fb[1][126], 0);
    CHECK_EQ_INT(fb[1][127], 0);
}

static void test_render_qr(void)
{
    int p, x, i;

    memset(&scr, 0, sizeof scr);
    scr.highlight = 1;
    scr.qr = 1;
    strcpy(scr.line[0], "ABCDEFGHIJKLMNOP"); /* cut at 10 characters */
    strcpy(scr.line[1], "x");
    memset(fb, 0xA5, sizeof fb);
    ds_ui_render(&scr, fb);
    for (p = 0; p < 8; p++) {
        for (x = 0; x < 64; x++) {
            CHECK_EQ_INT(fb[p][x], 0xA5);
        }
    }
    for (i = 0; i < 5; i++) {
        CHECK_EQ_INT(fb[0][66 + i], ds_font5x7['A' - 0x20][i]);
        CHECK_EQ_INT(fb[0][66 + 54 + i], ds_font5x7['J' - 0x20][i]);
        CHECK_EQ_INT(fb[1][66 + i], (uint8_t)~ds_font5x7['x' - 0x20][i]);
    }
    CHECK_EQ_INT(fb[0][66 + 60], 0); /* no 11th character */
    CHECK_EQ_INT(fb[0][127], 0);
    CHECK_EQ_INT(fb[0][64], 0);
    CHECK_EQ_INT(fb[1][127], 0xFF);
}

/* ---- pager ---- */

static const psbt_vector *find(const char *name)
{
    size_t k;
    for (k = 0; k < sizeof psbt_vectors / sizeof psbt_vectors[0]; k++) {
        if (strcmp(psbt_vectors[k].name, name) == 0) {
            return &psbt_vectors[k];
        }
    }
    CHECK(0);
    return NULL;
}

typedef struct {
    const uint8_t *p;
    uint32_t len;
} mem_src;

static int32_t mem_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    const mem_src *m = ctx;
    if (off > m->len || n > m->len - off) {
        return DS_ERR_ARG;
    }
    memcpy(buf, m->p + off, n);
    return (int32_t)n;
}

static int summary(char *out, size_t cap)
{
    const psbt_vector *v = find("one input with change");
    ds_wallet w;
    ds_psbt_review rv;
    mem_src m;
    ds_source src;
    int32_t n;

    if (!v) {
        return 0;
    }
    m.p = v->psbt;
    m.len = (uint32_t)v->psbt_len;
    src.read = mem_read;
    src.ctx = &m;
    src.len = m.len;
    CHECK_EQ_INT(ds_wallet_load(&w, v->entropy, v->entropy_len, v->passphrase,
                                v->testnet), DS_OK);
    CHECK_EQ_INT(ds_psbt_analyze(&src, &w, &rv), DS_OK);
    n = ds_psbt_summary_text(&rv, out, cap);
    CHECK(n > 0);
    ds_wallet_wipe(&w);
    return n > 0;
}

/* The text with its line breaks, spaces and cuts removed. */
static size_t squeeze(const char *s, char *out)
{
    size_t n = 0;
    for (; *s; s++) {
        if (*s != '\n' && *s != ' ') {
            out[n++] = *s;
        }
    }
    out[n] = 0;
    return n;
}

static void test_pager_wrap(void)
{
    static char got[4096], want[4096];
    size_t g = 0, i;
    uint32_t p;

    if (!summary(text, sizeof text)) {
        return;
    }
    ds_pager_init(&pg, text);
    CHECK(pg.pages >= 2);
    got[0] = 0;
    for (p = 0; p < pg.pages; p++) {
        int r;
        pg.page = p;
        ds_pager_page(&pg, &scr);
        for (r = 0; r < DS_UI_ROWS; r++) {
            CHECK(strlen(scr.line[r]) <= DS_UI_COLS);
        }
        for (r = 0; r < 7; r++) {
            g += squeeze(scr.line[r], got + g);
        }
    }
    squeeze(text, want);
    CHECK_EQ_STR(got, want);
    CHECK(strstr(got, "tb1") != NULL || strstr(got, "bc1") != NULL ||
          strstr(got, "BTC") != NULL);
    for (i = 0; i < strlen(want); i++) {
        CHECK(want[i] != '\n');
    }
}

static void test_pager_words(void)
{
    ds_pager p;

    /* A word that does not fit moves to the next line; a long one is cut. */
    ds_pager_init(&p, "aaaa bbbb cccc dddd eeee ffff\n"
                      "0123456789012345678901234567890123456789012345\n"
                      "end\n");
    ds_pager_page(&p, &scr);
    CHECK_EQ_STR(scr.line[0], "aaaa bbbb cccc dddd");
    CHECK_EQ_STR(scr.line[1], "eeee ffff");
    CHECK_EQ_STR(scr.line[2], "012345678901234567890");
    CHECK_EQ_STR(scr.line[3], "123456789012345678901");
    CHECK_EQ_STR(scr.line[4], "2345");
    CHECK_EQ_STR(scr.line[5], "end");
    CHECK_EQ_STR(scr.line[6], "");
    CHECK_EQ_INT(p.pages, 1);
    ds_pager_init(&p, "");
    CHECK_EQ_INT(p.pages, 1);
}

static void test_pager_last_page(void)
{
    uint32_t i;
    int r;

    if (!summary(text, sizeof text)) {
        return;
    }
    ds_pager_init(&pg, text);
    CHECK(pg.pages >= 2);
    CHECK_EQ_INT(pg.page, 0);
    CHECK_EQ_INT(pg.seen_last, 0);
    for (i = 0; i < pg.pages; i++) {
        char want[32];
        ds_pager_page(&pg, &scr);
        CHECK_EQ_INT(scr.highlight, -1);
        CHECK_EQ_INT(scr.qr, 0);
        if (i + 1 == pg.pages) {
            CHECK_EQ_STR(scr.line[7], "HOLD OK TO SIGN");
            CHECK_EQ_INT(pg.seen_last, 1);
        } else {
            sprintf(want, "p %u/%u  ^v  <cancel", (unsigned)(i + 1), (unsigned)pg.pages);
            CHECK_EQ_STR(scr.line[7], want);
            CHECK(strstr(scr.line[7], "HOLD") == NULL);
            CHECK_EQ_INT(pg.seen_last, 0);
        }
        for (r = 0; r < 7; r++) {
            CHECK(strstr(scr.line[r], "HOLD OK") == NULL);
        }
        ds_pager_move(&pg, 1);
    }
    /* Clamped at the last page; the flag stays. */
    CHECK_EQ_INT(pg.page, pg.pages - 1);
    ds_pager_move(&pg, 5);
    CHECK_EQ_INT(pg.page, pg.pages - 1);
    ds_pager_move(&pg, -1000);
    CHECK_EQ_INT(pg.page, 0);
    CHECK_EQ_INT(pg.seen_last, 1);
    ds_pager_init(&pg, text);
    CHECK_EQ_INT(pg.seen_last, 0);
    ds_pager_move(&pg, -1);
    CHECK_EQ_INT(pg.page, 0);
    /* A jump to the last page counts as having seen it. */
    ds_pager_move(&pg, 1000);
    CHECK_EQ_INT(pg.page, pg.pages - 1);
    CHECK_EQ_INT(pg.seen_last, 1);
    /* A one-page text is the last page from the start. */
    ds_pager_init(&pg, "FEE: 0.00001 BTC\n");
    CHECK_EQ_INT(pg.pages, 1);
    CHECK_EQ_INT(pg.seen_last, 1);
}

/* ---- picker ---- */

static void press(int btn, int times)
{
    while (times-- > 0) {
        CHECK_EQ_INT(ds_picker_button(&pk, btn, DS_BTN_PRESS), 0);
    }
}

static void test_picker_pin(void)
{
    size_t i;

    ds_picker_init(&pk, DS_PICK_PIN);
    CHECK_EQ_INT(pk.len, 0);
    press(DS_BTN_UP, 19); /* T */
    press(DS_BTN_RIGHT, 1);
    press(DS_BTN_UP, 21); /* wraps from T over 9 and back to E */
    press(DS_BTN_RIGHT, 1);
    press(DS_BTN_UP, 14); /* S */
    press(DS_BTN_RIGHT, 1);
    CHECK_EQ_INT(pk.len, 3);
    CHECK_EQ_STR(pk.buf, "TES");
    ds_picker_screen(&pk, "Enter PIN", &scr);
    CHECK_EQ_STR(scr.line[0], "Enter PIN");
    CHECK_EQ_STR(scr.line[1], "***S");
    CHECK_EQ_STR(scr.line[7], "^v char >add <del OK");
    CHECK_EQ_INT(scr.highlight, -1);
    CHECK_EQ_INT(scr.qr, 0);
    press(DS_BTN_UP, 1); /* T */
    press(DS_BTN_RIGHT, 1);
    CHECK_EQ_STR(pk.buf, "TEST");
    ds_picker_screen(&pk, "Enter PIN", &scr);
    CHECK_EQ_STR(scr.line[1], "****T"); /* four added, T is the current one */
    CHECK_EQ_INT(ds_picker_button(&pk, DS_BTN_OK, DS_BTN_PRESS), 1);
    CHECK_EQ_STR(pk.buf, "TEST");

    ds_picker_wipe(&pk);
    for (i = 0; i < sizeof pk.buf; i++) {
        CHECK_EQ_INT(pk.buf[i], 0);
    }
    CHECK_EQ_INT(pk.len, 0);
    CHECK_EQ_INT(pk.cur, 0);
}

static void test_picker_rules(void)
{
    int i;

    ds_picker_init(&pk, DS_PICK_PIN);
    /* DOWN wraps to the last character; LEFT on an empty buffer does nothing. */
    press(DS_BTN_DOWN, 1);
    CHECK_EQ_INT(pk.set[pk.cur], '9');
    press(DS_BTN_LEFT, 1);
    CHECK_EQ_INT(pk.len, 0);
    press(DS_BTN_RIGHT, 2);
    CHECK_EQ_STR(pk.buf, "99");
    press(DS_BTN_LEFT, 1);
    CHECK_EQ_STR(pk.buf, "9");
    CHECK_EQ_INT(pk.buf[1], 0);
    /* LONG events are ignored. */
    CHECK_EQ_INT(ds_picker_button(&pk, DS_BTN_RIGHT, DS_BTN_LONG), 0);
    CHECK_EQ_INT(ds_picker_button(&pk, DS_BTN_OK, DS_BTN_LONG), 0);
    CHECK_EQ_INT(pk.len, 1);
    /* A full buffer ignores RIGHT. */
    for (i = 0; i < 200; i++) {
        ds_picker_button(&pk, DS_BTN_RIGHT, DS_BTN_PRESS);
    }
    CHECK_EQ_INT(pk.len, DS_APP_TEXT_MAX);
    CHECK_EQ_INT(pk.buf[DS_APP_TEXT_MAX], 0);
    ds_picker_screen(&pk, "Passphrase", &scr);
    CHECK_EQ_STR(scr.line[1], "*********************");
    CHECK_EQ_STR(scr.line[5], "****************9"); /* 100 stars + the current one */
    for (i = 0; i < 8; i++) {
        CHECK(strlen(scr.line[i]) <= DS_UI_COLS);
    }
    ds_picker_wipe(&pk);

    /* The text set covers 0x20-0x7E, starting at the space. */
    ds_picker_init(&pk, DS_PICK_TEXT);
    CHECK_EQ_INT(strlen(DS_PICK_TEXT), 95);
    for (i = 0; i < 95; i++) {
        CHECK_EQ_INT(DS_PICK_TEXT[i], 0x20 + i);
    }
    press(DS_BTN_UP, 33); /* A */
    press(DS_BTN_RIGHT, 1);
    CHECK_EQ_STR(pk.buf, "A");
    ds_picker_wipe(&pk);
}

TEST_MAIN(test_font, test_render_text, test_render_qr, test_pager_wrap,
          test_pager_words, test_pager_last_page, test_picker_pin,
          test_picker_rules)
