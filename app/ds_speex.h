/* Speex narrowband decoding (libspeex, fixed point) for the spoken prompts:
 * one 20 ms frame at a time, 160 samples at 8 kHz. */
#ifndef DS_SPEEX_H
#define DS_SPEEX_H

#include <stdint.h>

#define DS_SPEEX_FRAME 160     /* samples per frame (20 ms at 8 kHz) */
#define DS_SPEEX_MAX_BYTES 62  /* the largest narrowband frame (quality 10) */
#define DS_SPEEX_MEM 2560      /* bytes for libspeex's decoder state */

/* The decoder; opaque, no heap (libspeex's state lives in mem). */
typedef struct {
    void *st;
    union {
        uint64_t align;
        uint8_t b[DS_SPEEX_MEM];
    } mem;
} ds_speex;

/* A fresh decoder, perceptual enhancement on. DS_OK, or DS_ERR_ARG (d NULL,
 * or the state does not fit in mem). */
int32_t ds_speex_init(ds_speex *d);

/* Decodes one frame of nbytes (1..DS_SPEEX_MAX_BYTES) into out. DS_OK, or
 * DS_ERR_ARG for a bad argument, a terminator or a corrupt frame (out is
 * then silence when it is given). */
int32_t ds_speex_frame(ds_speex *d, const uint8_t *bits, uint8_t nbytes,
                       int16_t out[DS_SPEEX_FRAME]);

#endif
