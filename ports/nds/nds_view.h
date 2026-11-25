/* Nintendo DS port: pure-C view helpers (no libnds), tested on the host.
 *
 * The app draws a 128x64 monochrome framebuffer (8 pages of 8 rows, one byte
 * per column, bit 0 the top row of a page, a set bit a light pixel). The DS
 * shows it doubled on the bottom screen, the current QR code large on the top
 * one, and its keys become the app's button classes. Screens are 256x192 of
 * 16-bit pixels. */
#ifndef NDS_VIEW_H
#define NDS_VIEW_H

#include <stdint.h>

#include "ds_qr.h"

#define NDS_W 256
#define NDS_H 192

/* libnds key bits, defined here to stay free of libnds. */
#define NDS_KEY_A 0x001
#define NDS_KEY_B 0x002
#define NDS_KEY_RIGHT 0x010
#define NDS_KEY_LEFT 0x020
#define NDS_KEY_UP 0x040
#define NDS_KEY_DOWN 0x080

/* fb doubled into rows 32-159 of out (NDS_W * NDS_H pixels); rows 0-31 and
 * 160-191 are off. */
void nds_view_scale(const uint8_t fb[8][128], uint16_t *out, uint16_t on, uint16_t off);

/* The whole screen light, then the current code of q (after ds_qr_next) at
 * the largest integer scale s with (size + 8) * s <= NDS_H, centred (4 quiet
 * modules each side). Returns s, or 0 (screen all light) when q has not been
 * started. */
int nds_view_qr(const ds_qr *q, uint16_t *out, uint16_t dark, uint16_t light);

/* Held DS keys to a DS_BTN_* class. Priority: OK (A), LEFT (B or left), UP,
 * DOWN, RIGHT; DS_BTN_NONE for nothing relevant. */
int nds_view_button(uint32_t held);

/* A touch at DS bottom-screen pixel (tx 0-255, ty 0-191) to the fb pixel it
 * shows: x = tx / 2, y = (ty - 32) / 2. Returns 1 inside the fb (rows
 * 32-159), 0 outside (x and y untouched). */
int nds_view_touch(int tx, int ty, int *x, int *y);

#endif
