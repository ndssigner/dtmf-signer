/* The display, one driver per controller (chosen at build time). The
 * framebuffer is 8 pages of 128 columns, one byte per column, bit 0 at the
 * top of the page, as ds_app_draw makes it. */
#ifndef DS_DISPLAY_H
#define DS_DISPLAY_H

#include <stdint.h>

void ds_display_init(void);
void ds_display_flush(const uint8_t fb[8][128]);
void ds_display_contrast(uint8_t v);

#endif
