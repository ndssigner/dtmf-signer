/* Reader for the voice packs ("DSV3", the layout is in tools/voice/make_pack.py):
 * the clips, the number grammar each pack carries, and the flash region
 * where packs other than the built-in English one are found. */
#ifndef DS_PACK_H
#define DS_PACK_H

#include <stddef.h>
#include <stdint.h>

#define DS_PACK_MAX_SCALES 8
#define DS_PACK_MAX_IRREGULARS 255
#define DS_PACK_NONE 0xFFFFu   /* no clip (a small number built from scales, no plural) */
#define DS_PACK_ALIGN 4096u    /* packs in the region start on these boundaries */
#define DS_PACKS_MAX 4

typedef struct {
    const uint8_t *data; /* the whole pack, not copied */
    uint32_t len;        /* the pack's own length (total_len) */
    uint16_t count;
    char lang[3]; /* two lowercase letters, NUL-terminated */
    const uint8_t *small;     /* u16[100] */
    const uint8_t *scales;    /* nscales entries */
    const uint8_t *irregulars;
    uint8_t nscales;
    uint16_t nirr;
} ds_pack;

/* Checks a pack at the start of data[0..len) (its total_len may be shorter
 * than len): magic, format, a prompt count equal to DS_P_COUNT, the CRC-32,
 * every clip inside the pack and made of whole frames (each length
 * 1..DS_SPEEX_MAX_BYTES), the grammar's bounds and clip numbers. A pack
 * comes from a flash region the user writes, so nothing in it is trusted;
 * after this, ds_pack_clip and ds_pack_number need no further checks.
 * DS_OK, or DS_ERR_ARG when the pack is malformed or for another firmware. */
int32_t ds_pack_open(ds_pack *p, const uint8_t *data, uint32_t len);

/* Where clip `id` lives: its Speex frames back to back (each a length byte,
 * then that many bytes) and their total bytes (0 for an empty clip).
 * DS_ERR_ARG when `id` is out of range. */
int32_t ds_pack_clip(const ds_pack *p, uint16_t id, const uint8_t **frames,
                     uint32_t *bytes);

/* The clips that say `n` in the pack's language, into out[0..cap). Returns
 * their count, DS_ERR_SPACE when cap is too small, or DS_ERR_ARG when the
 * pack's grammar cannot say n. */
int32_t ds_pack_number(const ds_pack *p, uint64_t n, uint16_t *out, size_t cap);

/* The packs the device can speak: slot 0 the built-in one, then those found
 * in the region; fp is the first DS_PACK_FP bytes of each pack's SHA-256
 * (said in About, so it can be compared with the published hash). */
#define DS_PACK_FP 8
typedef struct {
    ds_pack pack[DS_PACKS_MAX];
    uint8_t fp[DS_PACKS_MAX][DS_PACK_FP];
    int n;
} ds_packs;

/* Fills ps: `builtin` (already opened) first, then every valid, complete
 * pack starting on a 4096-byte boundary of region[0..len), up to
 * DS_PACKS_MAX in all; anything else there is ignored. region may be NULL
 * when len is 0. DS_OK, or DS_ERR_ARG on NULL arguments. */
int32_t ds_packs_scan(ds_packs *ps, const ds_pack *builtin, const uint8_t *region,
                      uint32_t len);

#endif
