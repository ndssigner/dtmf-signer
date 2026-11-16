/* Reader for a voice pack (the layout is in tools/voice/make_pack.py). */
#ifndef DS_PACK_H
#define DS_PACK_H

#include <stdint.h>

#include "ds_adpcm.h"

typedef struct {
    const uint8_t *data; /* the whole pack, not copied */
    uint32_t len;
    uint16_t count;
    char lang[3]; /* two lowercase letters, NUL-terminated */
} ds_pack;

/* Checks the pack (magic, language, the table and every clip inside `len`,
 * start indexes in range) so ds_pack_clip needs no further checks.
 * DS_OK, or DS_ERR_ARG when the pack is malformed. */
int32_t ds_pack_open(ds_pack *p, const uint8_t *data, uint32_t len);

/* Where clip `id` lives: its ADPCM bytes, their count and the decoder state
 * at its start. DS_ERR_ARG when `id` is out of range. */
int32_t ds_pack_clip(const ds_pack *p, uint16_t id, const uint8_t **adpcm,
                     uint32_t *bytes, ds_adpcm *start);

#endif
