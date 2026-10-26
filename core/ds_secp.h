/* DTMF-Signer portable core (internal): a libsecp256k1 context in a caller's
 * stack buffer, shared by BIP-32 and signing.
 *
 * secp256k1_context_static only verifies: pubkey_create and ecdsa_sign need
 * the signing tables, which the static context lacks. So a context is built
 * in a stack buffer (no malloc, no globals; about 216 bytes), used for one
 * operation, then destroyed and its buffer wiped. It is never randomized (no
 * entropy source), so there is no blinding: a side-channel consideration for
 * the Pico port. seckey_verify and seckey_tweak_add work with
 * secp256k1_context_static. */
#ifndef DS_SECP_H
#define DS_SECP_H

#include "ds_core.h"
#include "secp256k1.h"
#include "secp256k1_preallocated.h"

#define DS_SECP_BYTES 512

/* The preallocated context needs memory aligned for any type: a union with
 * the widest scalar types guarantees it in C99. */
typedef union {
    uint8_t bytes[DS_SECP_BYTES];
    uint64_t u64;
    long double ld;
    void *ptr;
} ds_secp_buf;

/* A context in *buf, or NULL if it does not fit. Pair with ds_secp_destroy,
 * also when this returns NULL. */
static inline secp256k1_context *ds_secp_create(ds_secp_buf *buf)
{
    if (secp256k1_context_preallocated_size(SECP256K1_CONTEXT_NONE) > sizeof *buf) {
        return NULL;
    }
    return secp256k1_context_preallocated_create(buf, SECP256K1_CONTEXT_NONE);
}

/* Destroys ctx (when not NULL) and wipes *buf. */
static inline void ds_secp_destroy(secp256k1_context *ctx, ds_secp_buf *buf)
{
    if (ctx != NULL) {
        secp256k1_context_preallocated_destroy(ctx);
    }
    ds_wipe(buf, sizeof *buf);
}

#endif
