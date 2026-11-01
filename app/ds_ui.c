#include "ds_ui.h"

#include <stdio.h>
#include <string.h>

#include "ds_btn.h"
#include "ds_font5x7.h"

#define PAGE_ROWS 7
#define QR_COLS 10
#define QR_X 66

static const uint8_t ICONS[8][5] = {
    {0x3E, 0x3E, 0x3E, 0x3E, 0x3E}, /* box, filled */
    {0x3E, 0x22, 0x22, 0x22, 0x3E}, /* box, empty */
    {0x08, 0x1C, 0x3E, 0x7F, 0x00}, /* left */
    {0x00, 0x7F, 0x3E, 0x1C, 0x08}, /* right */
    {0x08, 0x0C, 0x0E, 0x0C, 0x08}, /* up */
    {0x08, 0x18, 0x38, 0x18, 0x08}, /* down */
    {0x1C, 0x3E, 0x3E, 0x3E, 0x1C}, /* dot, filled */
    {0x1C, 0x22, 0x22, 0x22, 0x1C}, /* dot, empty */
};

const uint8_t *ds_glyph(char c)
{
    unsigned char u = (unsigned char)c;
    if (u >= 1 && u <= 8) {
        return ICONS[u - 1];
    }
    if (u < 0x20 || u > 0x7E) {
        u = '?';
    }
    return ds_font5x7[u - 0x20];
}

static const uint8_t *glyph(char c)
{
    return ds_glyph(c);
}

/* One glyph at (x, y) in fb, sx times wider and sy times taller. */
static void put_glyph(uint8_t fb[8][128], int x, int y, char ch, int sx, int sy)
{
    const uint8_t *g = glyph(ch);
    int c, r, dx, dy;
    for (c = 0; c < 5; c++) {
        for (r = 0; r < 8; r++) {
            if (!((g[c] >> r) & 1)) {
                continue;
            }
            for (dx = 0; dx < sx; dx++) {
                for (dy = 0; dy < sy; dy++) {
                    int px = x + c * sx + dx, py = y + r * sy + dy;
                    if (px >= 0 && px < 128 && py >= 0 && py < 64) {
                        fb[py / 8][px] |= (uint8_t)(1u << (py % 8));
                    }
                }
            }
        }
    }
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
        if (s->big > 0 && (row == s->big || row == s->big + 1)) {
            continue;
        }
        for (c = 0; c < cols && t[c] != '\0'; c++) {
            const uint8_t *g = glyph(t[c]);
            for (i = 0; i < 5; i++) {
                page[x0 + 6 * c + i] = (uint8_t)(g[i] ^ inv);
            }
        }
    }
    if (s->big > 0 && s->big + 1 < DS_UI_ROWS) {
        const char *t = s->line[s->big];
        int n = (int)strlen(t), x = (128 - (12 * n - 2)) / 2;
        for (c = 0; c < n && c < 10; c++) {
            put_glyph(fb, x + 12 * c, 8 * s->big, t[c], 2, 2);
        }
    }
    if (s->footer) {
        for (i = 0; i < 128; i += 2) {
            fb[6][i] |= 0x80; /* y 55 */
        }
    }
    for (i = 0; i < 2; i++) {
        const ds_bar *b = &s->bars[i];
        int x, y, y0 = 8 * b->row, x0 = 38, end;
        if (b->row <= 0 || b->row >= DS_UI_ROWS || b->x1 <= x0 + 2) {
            continue;
        }
        end = x0 + 1 + (b->fill < 0 ? 0 : b->fill > 1000 ? 1000 : b->fill) *
                           (b->x1 - x0 - 2) / 1000;
        for (x = x0; x <= b->x1; x++) {
            for (y = y0; y <= y0 + 6; y++) {
                int edge = x == x0 || x == b->x1 || y == y0 || y == y0 + 6;
                int on = edge || (b->fill > 0 && x <= end);
                if (on) {
                    fb[y / 8][x] |= (uint8_t)(1u << (y % 8));
                } else {
                    fb[y / 8][x] &= (uint8_t)~(1u << (y % 8));
                }
            }
        }
        if (b->mark >= 0) {
            int t = x0 + 1 + b->mark * (b->x1 - x0 - 2) / 1000;
            for (y = y0 - 1; y <= y0 + 7; y++) {
                if (y < 0 || y > 63) {
                    continue;
                }
                if (y > y0 && y < y0 + 6 && b->fill > 0 && t <= end) {
                    fb[y / 8][t] &= (uint8_t)~(1u << (y % 8)); /* dark on the fill */
                } else {
                    fb[y / 8][t] |= (uint8_t)(1u << (y % 8));
                }
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

void ds_pager_init_n(ds_pager *p, uint32_t pages)
{
    p->text = "";
    p->page = 0;
    p->pages = pages == 0 ? 1 : pages;
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

#define PICK_COLS 10

/* The pages, 10 cells per row; "\x7f" is delete, "\x01" empty. */
static const char *const PIN_PAGES[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\x7f\x01\x01\x01",
};
static const char *const TEXT_PAGES[] = {
    "abcdefghijklmnopqrstuvwxyz .-\x7f",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ .-\x7f",
    "0123456789!\"#$%&'()*+,/:;=?@_\x7f",
    "<>[\\]^`{|}~\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x7f",
};
static const char *const TEXT_TABS[] = { "abc", "ABC", "123", "<{~" };

static int pick_pages(const ds_picker *p)
{
    return p->kind == DS_PICK_PIN ? 1 : 4;
}

static const char *pick_page(const ds_picker *p)
{
    return p->kind == DS_PICK_PIN ? PIN_PAGES[0] : TEXT_PAGES[p->page];
}

static int pick_rows(const ds_picker *p)
{
    return (int)strlen(pick_page(p)) / PICK_COLS;
}

void ds_picker_init(ds_picker *p, int kind)
{
    memset(p->buf, 0, sizeof p->buf);
    p->len = 0;
    p->page = 0;
    p->cur = 0;
    p->kind = kind;
}

char ds_picker_cell(const ds_picker *p)
{
    return pick_page(p)[p->cur];
}

int ds_picker_button(ds_picker *p, int btn, int kind)
{
    int rows = pick_rows(p), pages = pick_pages(p);
    int row = p->cur / PICK_COLS, col = p->cur % PICK_COLS;
    char c;

    if (kind == DS_BTN_LONG) {
        return btn == DS_BTN_OK ? DS_PICK_DONE : 0;
    }
    if (kind != DS_BTN_PRESS) {
        return 0;
    }
    switch (btn) {
    case DS_BTN_LEFT:
        col = (col + PICK_COLS - 1) % PICK_COLS;
        break;
    case DS_BTN_RIGHT:
        col = (col + 1) % PICK_COLS;
        break;
    case DS_BTN_UP:
        if (row > 0) {
            row--;
        } else {
            p->page = (p->page + pages - 1) % pages;
            row = pick_rows(p) - 1;
        }
        break;
    case DS_BTN_DOWN:
        if (row < rows - 1) {
            row++;
        } else {
            p->page = (p->page + 1) % pages;
            row = 0;
        }
        break;
    case DS_BTN_OK:
        c = ds_picker_cell(p);
        if (c == DS_PICK_DEL) {
            if (p->len == 0) {
                return DS_PICK_BACK;
            }
            p->buf[--p->len] = '\0';
        } else if (c != DS_PICK_NONE && p->len < DS_APP_TEXT_MAX) {
            p->buf[p->len++] = c;
            p->buf[p->len] = '\0';
        }
        return 0;
    default:
        return 0;
    }
    p->cur = row * PICK_COLS + col;
    return 0;
}

void ds_picker_screen(const ds_picker *p, const char *title, ds_screen *s)
{
    size_t from = p->len > DS_UI_COLS - 1 ? p->len - (DS_UI_COLS - 1) : 0;

    screen_clear(s);
    snprintf(s->line[0], sizeof s->line[0], "%.*s", DS_UI_COLS, title);
    if (p->len > 0) {
        char n[8];
        size_t w = (size_t)snprintf(n, sizeof n, "%u", (unsigned)p->len);
        size_t t = strlen(s->line[0]);
        if (t + 1 + w <= DS_UI_COLS) {
            memset(s->line[0] + t, ' ', DS_UI_COLS - w - t);
            memcpy(s->line[0] + DS_UI_COLS - w, n, w + 1);
        }
        snprintf(s->line[1], sizeof s->line[1], "%s_", p->buf + from);
    } else {
        strcpy(s->line[1], p->kind == DS_PICK_PIN ? "OK add, hold OK: none"
                                                  : "OK add, hold OK: done");
    }
    s->pick = 1;
    s->pick_page = p->page;
    s->pick_cur = p->cur;
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

/* A character at (x, y), scale times larger, lit or (on 0) dark. */
static void big_char(uint8_t fb[8][128], int x, int y, char c, int scale, int on)
{
    const uint8_t *g = glyph(c);
    int col, row, dx, dy;
    for (col = 0; col < 5; col++) {
        for (row = 0; row < 7; row++) {
            if ((g[col] >> row) & 1) {
                for (dx = 0; dx < scale; dx++) {
                    for (dy = 0; dy < scale; dy++) {
                        put(fb, x + col * scale + dx, y + row * scale + dy, on);
                    }
                }
            }
        }
    }
}

/* A cell's mark: its character, an open box for the space, an arrow for
 * delete. */
static void cell_mark(uint8_t fb[8][128], int x, int y, char c, int on)
{
    int i;
    if (c == ' ') {
        for (i = 0; i < 5; i++) {
            put(fb, x + i, y + 6, on);
        }
        put(fb, x, y + 5, on);
        put(fb, x + 4, y + 5, on);
    } else if (c == DS_PICK_DEL) {
        for (i = 0; i < 5; i++) {
            put(fb, x + i, y + 3, on);
        }
        put(fb, x + 1, y + 2, on);
        put(fb, x + 1, y + 4, on);
        put(fb, x + 2, y + 1, on);
        put(fb, x + 2, y + 5, on);
    } else if (c != DS_PICK_NONE) {
        big_char(fb, x, y, c, 1, on);
    }
}

static void small_text(uint8_t fb[8][128], int x, int y, const char *t, int on)
{
    for (; *t; t++, x += 6) {
        big_char(fb, x, y, *t, 1, on);
    }
}

void ds_picker_draw(const ds_picker *p, uint8_t fb[8][128])
{
    const char *cells = pick_page(p);
    int n = (int)strlen(cells), i, x, y;
    char c = ds_picker_cell(p);

    for (i = 2; i < 8; i++) {
        memset(fb[i], 0, 128);
    }
    for (x = 0; x < 128; x += 2) {
        put(fb, x, 17, 1);
    }
    for (i = 0; i < n; i++) {
        int cx = (i % PICK_COLS) * 10 + 2, cy = 20 + (i / PICK_COLS) * 10;
        int sel = i == p->cur;
        if (sel) {
            for (y = cy - 2; y <= cy + 8; y++) {
                for (x = cx - 2; x <= cx + 6; x++) {
                    put(fb, x, y, 1);
                }
            }
        }
        cell_mark(fb, cx, cy, cells[i], !sel);
    }
    /* The magnifier. */
    for (x = 102; x <= 127; x++) {
        put(fb, x, 19, 1);
        put(fb, x, 48, 1);
    }
    for (y = 19; y <= 48; y++) {
        put(fb, 102, y, 1);
        put(fb, 127, y, 1);
    }
    if (c == ' ') {
        small_text(fb, 106, 30, "spc", 1);
    } else if (c == DS_PICK_DEL) {
        small_text(fb, 106, 30, "del", 1);
    } else if (c != DS_PICK_NONE) {
        big_char(fb, 108, 23, c, 3, 1);
    }
    /* The tabs. */
    if (pick_pages(p) > 1) {
        for (i = 0, x = 0; i < 4; i++) {
            int w = 6 * (int)strlen(TEXT_TABS[i]) + 1;
            if (i == p->page) {
                for (y = 50; y <= 58; y++) {
                    int xx;
                    for (xx = x; xx <= x + w; xx++) {
                        put(fb, xx, y, 1);
                    }
                }
            }
            small_text(fb, x + 1, 51, TEXT_TABS[i], i != p->page);
            x += w + 6;
        }
    }
}

void ds_picker_wipe(ds_picker *p)
{
    ds_wipe(p->buf, sizeof p->buf);
    p->len = 0;
    p->page = 0;
    p->cur = 0;
}
