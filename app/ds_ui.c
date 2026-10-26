#include "ds_ui.h"

#include <stdio.h>
#include <string.h>

#include "ds_btn.h"
#include "ds_font5x7.h"

#define PAGE_ROWS 7
#define QR_COLS 10
#define QR_X 66

static const uint8_t *glyph(char c)
{
    unsigned char u = (unsigned char)c;
    if (u < 0x20 || u > 0x7E) {
        u = '?';
    }
    return ds_font5x7[u - 0x20];
}

void ds_ui_render(const ds_screen *s, uint8_t fb[8][128])
{
    int row, c, i;

    for (row = 0; row < DS_UI_ROWS; row++) {
        int x0 = s->qr ? QR_X : 0;
        int cols = s->qr ? QR_COLS : DS_UI_COLS;
        int from = s->qr ? 64 : 0;
        uint8_t inv = (row == s->highlight) ? 0xFF : 0x00;
        uint8_t *page = fb[row];
        const char *t = s->line[row];

        memset(page + from, inv, (size_t)(128 - from));
        for (c = 0; c < cols && t[c] != '\0'; c++) {
            const uint8_t *g = glyph(t[c]);
            for (i = 0; i < 5; i++) {
                page[x0 + 6 * c + i] = (uint8_t)(g[i] ^ inv);
            }
        }
    }
}

/* ---- pager ---- */

static void screen_clear(ds_screen *s)
{
    memset(s, 0, sizeof *s);
    s->highlight = -1;
}

/* Takes the line that starts at *pos: up to DS_UI_COLS characters, ending at
 * a '\n', or at the last space that fits, or cut at DS_UI_COLS. Copies it in
 * out (NUL-terminated) and moves *pos to the start of the next line. Returns
 * 0 when there is no text left. */
static int next_line(const char *t, size_t *pos, char *out)
{
    size_t p = *pos, n = 0, take;

    if (t[p] == '\0') {
        return 0;
    }
    while (n < DS_UI_COLS && t[p + n] != '\0' && t[p + n] != '\n') {
        n++;
    }
    take = n;
    if (n == DS_UI_COLS && t[p + n] != '\0' && t[p + n] != '\n' && t[p + n] != ' ') {
        /* Mid-word: break at the last space if there is one (not at the start). */
        size_t k = n;
        while (k > 0 && t[p + k - 1] != ' ') {
            k--;
        }
        if (k > 0) {
            take = k - 1;
            while (take > 0 && t[p + take - 1] == ' ') {
                take--;
            }
            n = k; /* continue after the spaces */
        }
        /* else: a word of 21 or more characters, cut where it is. */
    }
    memcpy(out, t + p, take);
    out[take] = '\0';
    p += n;
    if (t[p] == '\n') {
        p++;
    } else {
        while (t[p] == ' ') {
            p++;
        }
        if (t[p] == '\n') {
            p++;
        }
    }
    *pos = p;
    return 1;
}

static uint32_t count_lines(const char *t)
{
    size_t pos = 0;
    uint32_t n = 0;
    char buf[DS_UI_COLS + 1];

    while (next_line(t, &pos, buf)) {
        n++;
    }
    return n;
}

void ds_pager_init(ds_pager *p, const char *text)
{
    uint32_t lines = count_lines(text);

    p->text = text;
    p->page = 0;
    p->pages = lines == 0 ? 1 : (lines + PAGE_ROWS - 1) / PAGE_ROWS;
    p->seen_last = p->pages == 1;
}

void ds_pager_page(const ds_pager *p, ds_screen *s)
{
    size_t pos = 0;
    uint32_t skip = p->page * PAGE_ROWS;
    int r;
    char buf[DS_UI_COLS + 1];

    screen_clear(s);
    while (skip > 0 && next_line(p->text, &pos, buf)) {
        skip--;
    }
    for (r = 0; r < PAGE_ROWS; r++) {
        if (!next_line(p->text, &pos, s->line[r])) {
            break;
        }
    }
    if (p->page + 1 >= p->pages) {
        strcpy(s->line[PAGE_ROWS], "HOLD OK TO SIGN");
    } else {
        snprintf(s->line[PAGE_ROWS], sizeof s->line[PAGE_ROWS], "p %u/%u  ^v  <cancel",
                 (unsigned)(p->page + 1), (unsigned)p->pages);
    }
}

void ds_pager_move(ds_pager *p, int delta)
{
    int64_t np = (int64_t)p->page + delta;

    if (np < 0) {
        np = 0;
    }
    if (np > (int64_t)p->pages - 1) {
        np = (int64_t)p->pages - 1;
    }
    p->page = (uint32_t)np;
    if (p->page + 1 >= p->pages) {
        p->seen_last = 1;
    }
}

/* ---- picker ---- */

static int set_len(const ds_picker *p)
{
    return (int)strlen(p->set);
}

void ds_picker_init(ds_picker *p, const char *set)
{
    memset(p->buf, 0, sizeof p->buf);
    p->len = 0;
    p->cur = 0;
    p->set = set;
}

int ds_picker_button(ds_picker *p, int btn, int kind)
{
    int n = set_len(p);

    if (kind != DS_BTN_PRESS || n == 0) {
        return 0;
    }
    switch (btn) {
    case DS_BTN_UP:
        p->cur = (p->cur + 1) % n;
        break;
    case DS_BTN_DOWN:
        p->cur = (p->cur + n - 1) % n;
        break;
    case DS_BTN_RIGHT:
        if (p->len < DS_APP_TEXT_MAX) {
            p->buf[p->len++] = p->set[p->cur];
            p->buf[p->len] = '\0';
        }
        break;
    case DS_BTN_LEFT:
        if (p->len > 0) {
            p->buf[--p->len] = '\0';
        }
        break;
    case DS_BTN_OK:
        return 1;
    default:
        break;
    }
    return 0;
}

void ds_picker_screen(const ds_picker *p, const char *title, ds_screen *s)
{
    size_t i;
    size_t total = p->len + 1; /* the stars and the current character */

    screen_clear(s);
    snprintf(s->line[0], sizeof s->line[0], "%.*s", DS_UI_COLS, title);
    for (i = 0; i < total && i < (size_t)(DS_UI_ROWS - 2) * DS_UI_COLS; i++) {
        char c = i < p->len ? '*' : p->set[p->cur];
        s->line[1 + i / DS_UI_COLS][i % DS_UI_COLS] = c;
    }
    strcpy(s->line[DS_UI_ROWS - 1], "^v char >add <del OK");
}

void ds_picker_wipe(ds_picker *p)
{
    ds_wipe(p->buf, sizeof p->buf);
    p->len = 0;
    p->cur = 0;
}
