/* DTMF-Signer device logic: the text screen model, its drawing, the pager
 * for the PSBT review and the picker for the PIN and the passphrase.
 *
 * A screen is text: 8 rows of up to 21 characters (the 5x7 font plus one
 * blank column is 6 px, so 21 columns fill 126 of the 128 px). Drawing is a
 * separate function that fills the framebuffer: 8 pages of 128 columns, one
 * byte per column, bit 0 the top row of the page, a set bit a lit pixel. Row
 * r of the text is page r. Text is printable ASCII; any other byte is drawn
 * as '?'. No malloc, no globals. */
#ifndef DS_UI_H
#define DS_UI_H

#include <stddef.h>
#include <stdint.h>

#include "ds_core.h"

#define DS_UI_COLS 21
#define DS_UI_ROWS 8

/* The longest PIN or passphrase the picker takes. */
#ifndef DS_APP_TEXT_MAX
#define DS_APP_TEXT_MAX 100
#endif

typedef struct {
    char line[DS_UI_ROWS][DS_UI_COLS + 1];
    int highlight; /* the inverted row, -1 for none */
    int qr;        /* 1: a QR code owns the left 64x64 square */
    int pick;      /* 1: rows 2-7 are the picker's (ds_picker_draw) */
    int pick_page, pick_cur; /* its page and cell, so a move redraws */
} ds_screen;

/* Draws s in fb. Every row is cleared first. Without qr the lines use x 0-125
 * (21 characters); with qr they go in the right column, x 66-127 (10
 * characters, the rest of a longer line is cut) and x 0-63 is not touched
 * (it is the QR's). A highlighted row is inverted over its whole width (x 64-127
 * with qr). */
void ds_ui_render(const ds_screen *s, uint8_t fb[8][128]);

/* ---- pager ---- */

/* Shows a text (kept by pointer; it must outlive the pager) 7 rows at a time
 * with a footer row. Lines break at spaces and at '\n', a word longer than a
 * line is cut. */
typedef struct {
    const char *text;
    uint32_t page, pages;
    uint32_t seen_last; /* 1 once the last page has been reached */
} ds_pager;

/* Starts at page 0. seen_last is 1 at once when the text is a single page. */
void ds_pager_init(ds_pager *p, const char *text);

/* Fills s with the current page: 7 text rows and the footer "p 2/5  ^v  <cancel",
 * or "HOLD OK TO SIGN" on the last page. Does not change p. */
void ds_pager_page(const ds_pager *p, ds_screen *s);

/* Moves by delta pages, clamped to the first and last page. Reaching the last
 * page (here or in ds_pager_init) sets seen_last; nothing clears it. */
void ds_pager_move(ds_pager *p, int delta);

/* ---- picker ---- */

/* What is typed: the PIN (A-Z and 0-9, one page of 4 rows) or the
 * passphrase (printable ASCII, the SPEC's OP-PASSPHRASE, in four pages of
 * 3 rows: abc, ABC, 123 and the rarer symbols; space, '.' and '-' are on
 * both letter pages). Pages have 10 columns. */
enum { DS_PICK_PIN = 1, DS_PICK_TEXT };

/* Cells that are not a character. */
#define DS_PICK_DEL '\x7f'  /* deletes the last character */
#define DS_PICK_NONE '\x01' /* empty */

/* Results of ds_picker_button. */
#define DS_PICK_DONE 1 /* OK held: the entry is finished */
#define DS_PICK_BACK 2 /* delete on an empty entry: leave the picker */

/* Typing a secret with five buttons. buf, page and cur are secrets (cur is
 * the character being chosen): ds_picker_wipe when done. */
typedef struct {
    char buf[DS_APP_TEXT_MAX + 1];
    size_t len;
    int page, cur; /* the page shown and the chosen cell in it */
    int kind;      /* DS_PICK_PIN or DS_PICK_TEXT; last, not a secret */
} ds_picker;

/* Empty entry, the first page, its first cell. */
void ds_picker_init(ds_picker *p, int kind);

/* btn is a DS_BTN_* class and kind a DS_BTN_PRESS/LONG/WIPE (ds_btn.h).
 * Presses: LEFT/RIGHT move along the row and UP/DOWN between rows, wrapping
 * around; with several pages, up from the top row goes to the previous
 * page's last row and down from the last row to the next page's top row,
 * same column. OK adds the chosen character (ignored when the entry is
 * full), deletes the last one on the delete cell, and does nothing on an
 * empty cell; the cursor stays. Returns DS_PICK_BACK for delete on an empty
 * entry. A long OK returns DS_PICK_DONE; other long presses do nothing (a
 * held LEFT is the emergency wipe). Anything else returns 0. */
int ds_picker_button(ds_picker *p, int btn, int kind);

/* The chosen cell: a character, DS_PICK_DEL or DS_PICK_NONE. */
char ds_picker_cell(const ds_picker *p);

/* title and the count on row 0; on row 1 the entry in the clear, its last 20
 * characters and a cursor, or a hint while it is empty; rows 2-7 are left
 * to ds_picker_draw (s->pick set). */
void ds_picker_screen(const ds_picker *p, const char *title, ds_screen *s);

/* Draws rows 2-7 (y 16-63) of fb: the page's cells with the chosen one
 * inverted, a magnifier at the right with the chosen character three times
 * larger (so l, 1, I and | or O and 0 can be told apart), and the page tabs
 * when there are several. Call it after ds_ui_render. */
void ds_picker_draw(const ds_picker *p, uint8_t fb[8][128]);

/* Zeroes the entry, its length, the page and the cell. */
void ds_picker_wipe(ds_picker *p);

#endif
