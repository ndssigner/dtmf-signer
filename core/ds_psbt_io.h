/* DTMF-Signer portable core (internal): bounded reads from a PSBT source and
 * key-value map walking, shared by the analysis and the signing pass. */
#ifndef DS_PSBT_IO_H
#define DS_PSBT_IO_H

#include "ds_psbt.h"

#define DS_PSBT_CACHE 64

/* A read buffer over a source: buf holds the bytes [base, base + fill). */
typedef struct {
    const ds_source *src;
    uint32_t base, fill;
    uint8_t buf[DS_PSBT_CACHE];
} ds_psbt_cache;

/* Copies n bytes at off; DS_ERR_PSBT if they are not all inside the source,
 * or the source's own error. */
int32_t ds_psbt_get(ds_psbt_cache *c, uint32_t off, uint8_t *out, size_t n);

/* Reads a compact size at *off, advancing it; values that cannot fit in the
 * rest of the source are rejected by the callers' range checks. */
int32_t ds_psbt_get_compact(ds_psbt_cache *c, uint32_t *off, uint64_t *v);

/* A key-value pair's place in the source. */
typedef struct {
    uint32_t key, key_len, val, val_len, next;
    uint8_t type; /* first key byte */
} ds_psbt_entry;

/* Reads the pair at off: 1 and *e, or 0 at the map's separator (e->next is
 * then just past it), or an error. */
int32_t ds_psbt_read_entry(ds_psbt_cache *c, uint32_t off, ds_psbt_entry *e);

/* SHA-256 of the whole source. */
int32_t ds_psbt_hash_source(ds_psbt_cache *c, uint8_t out[32]);

#endif
