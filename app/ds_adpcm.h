/* IMA ADPCM decoding (4 bits per sample) for the spoken prompts. */
#ifndef DS_ADPCM_H
#define DS_ADPCM_H

#include <stddef.h>
#include <stdint.h>

/* The decoder state; a clip's start state comes from its pack entry. */
typedef struct {
    int16_t pred;
    uint8_t index; /* 0..88 */
} ds_adpcm;

/* Decodes `nibbles` samples from `in` (two per byte, the low nibble first)
 * into `out`, advancing *st so a clip can be decoded in pieces. */
void ds_adpcm_decode(ds_adpcm *st, const uint8_t *in, size_t nibbles, int16_t *out);

#endif
