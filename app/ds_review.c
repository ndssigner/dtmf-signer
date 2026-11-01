/* DTMF-Signer device logic: the review cards. See ds_review.h. */
#include "ds_review.h"

#include <stdio.h>
#include <string.h>

#include "ds_bitcoin.h"
#include "ds_font5x7.h"

#define GROUPS_PER_ROW 4

/* ---- the text ---- */

static void btc(char *out, size_t cap, uint64_t sat)
{
    snprintf(out, cap, "%lu.%08lu", (unsigned long)(sat / 100000000u),
             (unsigned long)(sat % 100000000u));
}

static int has_warnings(const ds_psbt_review *r)
{
    uint32_t k;
    if (r->fee_warning) {
        return 1;
    }
    for (k = 0; k < r->n_in; k++) {
        if (!r->in[k].ours) {
            return 1;
        }
    }
    return 0;
}

uint32_t ds_review_cards(const ds_psbt_review *r)
{
    return r->n_out + 2 + (has_warnings(r) ? 1 : 0);
}

/* Row 0: the title, then "k/N" at the right. */
static void title(ds_screen *s, const char *t, uint32_t i, uint32_t n)
{
    char pos[16];
    size_t lt = strlen(t), lp;
    snprintf(pos, sizeof pos, "%u/%u", (unsigned)(i + 1), (unsigned)n);
    lp = strlen(pos);
    memset(s->line[0], ' ', DS_UI_COLS);
    memcpy(s->line[0], t, lt);
    memcpy(s->line[0] + DS_UI_COLS - lp, pos, lp);
    s->line[0][DS_UI_COLS] = '\0';
}

/* A label and an amount at the right; the label gives way, never the amount. */
static void label_amount(char *row, const char *label, uint64_t sat)
{
    char a[24];
    size_t la, room;
    btc(a, sizeof a, sat);
    la = strlen(a);
    room = la < DS_UI_COLS ? DS_UI_COLS - la - 1 : 0;
    memset(row, ' ', DS_UI_COLS);
    memcpy(row, label, strlen(label) < room ? strlen(label) : room);
    memcpy(row + DS_UI_COLS - la, a, la);
    row[DS_UI_COLS] = '\0';
}

static void output_card(const ds_psbt_review *r, uint32_t k, ds_screen *s)
{
    const ds_out *o = &r->out[k];
    char addr[96], amt[24];
    int32_t n = -1;
    int g, groups, rows;

    if (o->kind == DS_OUT_CHANGE) {
        title(s, "Change, yours", k, ds_review_cards(r));
        s->chip = 13;
    } else if (o->kind == DS_OUT_OWN) {
        title(s, "To this wallet", k, ds_review_cards(r));
        s->chip = 14;
    } else {
        title(s, "Send to", k, ds_review_cards(r));
    }
    btc(amt, sizeof amt, o->amount);
    snprintf(s->line[1], sizeof s->line[1], "%s BTC", amt);
    if (o->full_len <= DS_TX_SCRIPT_KEEP) {
        n = ds_address(o->script, o->full_len, r->testnet, addr, sizeof addr);
    }
    if (n <= 0) {
        snprintf(s->line[2], sizeof s->line[2], "Script, %u bytes", (unsigned)o->full_len);
        s->tall = 1;
        return;
    }
    groups = (n + 3) / 4;
    rows = (groups + GROUPS_PER_ROW - 1) / GROUPS_PER_ROW;
    s->tall = rows <= 4;
    s->addr_from = 2;
    s->addr_rows = rows;
    for (g = 0; g < groups; g++) {
        char *line = s->line[2 + g / GROUPS_PER_ROW];
        size_t at = strlen(line);
        size_t take = n - 4 * g < 4 ? (size_t)(n - 4 * g) : 4;
        if (g % GROUPS_PER_ROW != 0) {
            line[at++] = ' ';
        }
        memcpy(line + at, addr + 4 * g, take);
        line[at + take] = '\0';
    }
}

static void fee_card(const ds_psbt_review *r, uint32_t i, ds_screen *s)
{
    uint64_t send = 0, change = 0;
    uint32_t k;
    char amt[24];
    for (k = 0; k < r->n_out; k++) {
        if (r->out[k].kind == DS_OUT_EXTERNAL) {
            send += r->out[k].amount;
        } else {
            change += r->out[k].amount;
        }
    }
    title(s, "Fee", i, ds_review_cards(r));
    btc(amt, sizeof amt, r->fee);
    snprintf(s->line[1], sizeof s->line[1], "%s BTC", amt);
    s->tall = 1;
    label_amount(s->line[2], "Sending", send);
    label_amount(s->line[3], "Change", change);
    label_amount(s->line[4], "Total", send + r->fee);
}

static void warnings_card(const ds_psbt_review *r, uint32_t i, ds_screen *s)
{
    uint32_t k, shown = 0, left = 0;
    int row = 1;
    title(s, "Check", i, ds_review_cards(r));
    if (r->fee_warning) {
        strcpy(s->line[row++], "High fee");
    }
    for (k = 0; k < r->n_in; k++) {
        if (r->in[k].ours) {
            continue;
        }
        if (row < 6) {
            snprintf(s->line[row++], sizeof s->line[row], "Input %u: %s", (unsigned)(k + 1),
                     r->in[k].no_witness ? "no witness" : "not ours");
            shown++;
        } else {
            left++;
        }
    }
    if (left > 0) {
        snprintf(s->line[row], sizeof s->line[row], "and %u more", (unsigned)left);
    }
    (void)shown;
}

static void confirm_card(const ds_psbt_review *r, uint32_t i, int armed, ds_screen *s)
{
    uint64_t send = 0;
    uint32_t k;
    char amt[24];
    for (k = 0; k < r->n_out; k++) {
        if (r->out[k].kind == DS_OUT_EXTERNAL) {
            send += r->out[k].amount;
        }
    }
    title(s, "Sign?", i, ds_review_cards(r));
    btc(amt, sizeof amt, send + r->fee);
    snprintf(s->line[1], sizeof s->line[1], "%s BTC", amt);
    s->tall = 1;
    strcpy(s->line[2], "out, fee included");
    strcpy(s->line[4], armed ? "    Hold OK: sign" : "Tap OK, then hold OK");
    s->highlight = 4;
    strcpy(s->line[5], "<: cancel");
}

void ds_review_card(const ds_psbt_review *r, uint32_t i, int armed, ds_screen *s)
{
    uint32_t n = ds_review_cards(r);
    memset(s, 0, sizeof *s);
    s->highlight = -1;
    s->review = 1;
    if (i < r->n_out) {
        output_card(r, i, s);
    } else if (i == r->n_out) {
        fee_card(r, i, s);
    } else if (i + 1 < n) {
        warnings_card(r, i, s);
    } else {
        confirm_card(r, i, armed, s);
    }
}

/* ---- the drawing ---- */

/* Address glyphs with a tail below the line (rows 2-7), as column bytes. */
static const struct {
    char c;
    uint8_t col[5];
} TAILS[] = {
    { 'g', { 0x18, 0xA4, 0xA4, 0xA8, 0x7C } },
    { 'j', { 0x40, 0x80, 0x84, 0x7D, 0x00 } },
    { 'p', { 0xFC, 0x24, 0x24, 0x24, 0x18 } },
    { 'q', { 0x18, 0x24, 0x24, 0x28, 0xFC } },
    { 'y', { 0x1C, 0xA0, 0xA0, 0xA0, 0x7C } },
};

static const uint8_t *glyph_of(char c, int addr)
{
    unsigned char u = (unsigned char)c;
    size_t k;
    if (addr) {
        for (k = 0; k < sizeof TAILS / sizeof TAILS[0]; k++) {
            if (TAILS[k].c == c) {
                return TAILS[k].col;
            }
        }
    }
    if (u < 0x20 || u > 0x7E) {
        u = '?';
    }
    return ds_font5x7[u - 0x20];
}

static void put(uint8_t fb[8][128], int x, int y, int on)
{
    if (x < 0 || x > 127 || y < 0 || y > 63) {
        return;
    }
    if (on) {
        fb[y / 8][x] |= (uint8_t)(1u << (y % 8));
    } else {
        fb[y / 8][x] &= (uint8_t)~(1u << (y % 8));
    }
}

/* A row of text at y: scale 1 or 2 (twice as tall), lit or dark. */
static void row_text(uint8_t fb[8][128], int x, int y, const char *t, int scale, int addr,
                     int on)
{
    int c, r, d;
    for (; *t && x < 128; t++, x += 6) {
        const uint8_t *g = glyph_of(*t, addr);
        for (c = 0; c < 5; c++) {
            for (r = 0; r < 8; r++) {
                if ((g[c] >> r) & 1) {
                    for (d = 0; d < scale; d++) {
                        put(fb, x + c, y + r * scale + d, on);
                    }
                }
            }
        }
    }
}

static void fill(uint8_t fb[8][128], int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            put(fb, x, y, 1);
        }
    }
}

void ds_review_draw(const ds_screen *s, uint8_t fb[8][128])
{
    int r, x, base, rule;

    memset(fb, 0, 8 * 128);
    /* Row 0, its first chip characters inverted. */
    if (s->chip > 0) {
        char head[DS_UI_COLS + 1];
        snprintf(head, sizeof head, "%.*s", s->chip, s->line[0]);
        fill(fb, 0, 0, 6 * s->chip, 8);
        row_text(fb, 1, 1, head, 1, 0, 0);
        row_text(fb, 6 * s->chip, 0, s->line[0] + s->chip, 1, 0, 1);
    } else {
        row_text(fb, 0, 0, s->line[0], 1, 0, 1);
    }
    /* Row 1: twice as tall when the card says so. */
    if (s->tall) {
        row_text(fb, 0, 11, s->line[1], 2, 0, 1);
        rule = 29;
        base = 32;
    } else {
        row_text(fb, 0, 9, s->line[1], 1, 0, 1);
        rule = 18;
        base = 20;
    }
    for (x = 0; x < 128; x += 2) {
        put(fb, x, rule, 1);
    }
    for (r = 2; r < DS_UI_ROWS; r++) {
        int ry = base + 8 * (r - 2);
        int addr = r >= s->addr_from && r < s->addr_from + s->addr_rows;
        if (ry > 56 || s->line[r][0] == '\0') {
            continue;
        }
        if (r == s->highlight) {
            fill(fb, 0, ry - 1, 127, ry + 7);
            row_text(fb, 0, ry, s->line[r], 1, 0, 0);
        } else {
            row_text(fb, 0, ry, s->line[r], 1, addr, 1);
        }
    }
}
