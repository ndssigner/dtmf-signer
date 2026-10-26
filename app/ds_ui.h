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

#define DS_PICK_PIN "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
#define DS_PICK_TEXT                                                          \
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"    \
    "abcdefghijklmnopqrstuvwxyz{|}~"

/* Typing a secret with five buttons. buf and cur are secrets: ds_picker_wipe
 * when done. */
typedef struct {
    char buf[DS_APP_TEXT_MAX + 1];
    size_t len;
    int cur; /* index in set of the character being chosen */
    const char *set;
} ds_picker;

/* Empty buffer; the current character is the first of set. */
void ds_picker_init(ds_picker *p, const char *set);

/* btn is a DS_BTN_* class and kind a DS_BTN_PRESS/LONG/WIPE (ds_btn.h); only
 * presses act. UP/DOWN change the current character (wrapping around the
 * set), RIGHT adds it (the current character stays; ignored when the buffer
 * is full), LEFT deletes the last character (nothing when empty). OK returns
 * 1 (done); anything else 0. */
int ds_picker_button(ds_picker *p, int btn, int kind);

/* title on row 0, then the added characters as '*' followed by the current
 * one in the clear (wrapping at 21 columns), and the hint on the last row. */
void ds_picker_screen(const ds_picker *p, const char *title, ds_screen *s);

/* Zeroes the buffer, the length and the current character. */
void ds_picker_wipe(ds_picker *p);

#endif
