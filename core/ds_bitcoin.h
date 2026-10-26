/* DTMF-Signer portable core: incremental SHA-256, Bitcoin compact-size
 * integers and address encoding for the standard output scripts. */
#ifndef DS_BITCOIN_H
#define DS_BITCOIN_H

#include "ds_core.h"

typedef struct {
    uint32_t h[8];
    uint8_t buf[64];
    uint64_t len; /* total bytes absorbed */
    uint32_t fill; /* bytes pending in buf */
} ds_sha256_ctx;

void ds_sha256_init(ds_sha256_ctx *c);
void ds_sha256_update(ds_sha256_ctx *c, const uint8_t *d, size_t n);
/* Writes the digest and wipes the context. */
void ds_sha256_final(ds_sha256_ctx *c, uint8_t out[32]);
/* SHA-256 of SHA-256 (the Bitcoin "hash256"). */
void ds_sha256d(const uint8_t *d, size_t n, uint8_t out[32]);

/* Reads a compact-size integer from p[0..n). Returns the bytes consumed
 * (1, 3, 5 or 9) and stores the value, or DS_ERR_PSBT if p is too short. */
int32_t ds_compact_decode(const uint8_t *p, size_t n, uint64_t *value);
/* Writes the shortest compact-size encoding of v; returns its length. */
size_t ds_compact_encode(uint64_t v, uint8_t out[9]);

/* Address text of an output script, NUL-terminated. Returns its length
 * without the NUL, DS_ERR_SPACE if cap is too small (including the NUL), or
 * DS_ERR_UNSUPPORTED for any script that is not P2PKH, P2SH, P2WPKH, P2WSH
 * or P2TR (the caller then shows "script of N bytes"). testnet selects the
 * version bytes (0x6f/0xc4) and the "tb" prefix instead of 0x00/0x05 and "bc". */
int32_t ds_address(const uint8_t *script, size_t len, int testnet, char *out,
                   size_t cap);

#endif
