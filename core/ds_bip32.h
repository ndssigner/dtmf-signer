/* BIP-32: master key, private child derivation, paths and xpub export. */
#ifndef DS_BIP32_H
#define DS_BIP32_H

#include "ds_core.h"
#include "ur_tones.h"

typedef struct {
    uint8_t key[32];   /* private key (secret) */
    uint8_t chain[32]; /* chain code (secret: with a pub it derives all children) */
    uint8_t pub[33];   /* compressed public key */
    uint32_t fp;       /* this key's fingerprint: HASH160(pub)[0..3], big-endian */
    uint32_t parent_fp; /* the parent's fp; 0 for the master */
    uint8_t depth;
} ds_hdkey;

/* Master key from a 16..64 byte seed (DS_ERR_ARG otherwise). */
int32_t ds_bip32_master(const uint8_t *seed, size_t len, ds_hdkey *out);

/* CKDpriv. Hardened when index & 0x80000000. DS_ERR_DERIVE if the index yields
 * an invalid key (probability about 2^-127; BIP-32 says to skip to the next
 * index, here the caller decides). DS_ERR_ARG if parent->depth is 255.
 * out may alias parent; on error out is left untouched. */
int32_t ds_bip32_child(const ds_hdkey *parent, uint32_t index, ds_hdkey *out);

/* Derives master/path[0]/.../path[n-1]. Intermediate keys are wiped on every
 * path. On error out is wiped. */
int32_t ds_bip32_path(const ds_hdkey *master, const uint32_t *path, size_t n,
                      ds_hdkey *out);

/* Fills out with k's public data. path (n steps from the master, hardened with
 * | 0x80000000) must stay valid while out is used; n should equal k->depth. */
void ds_bip32_to_xpub(const ds_hdkey *k, uint32_t source_fp, const uint32_t *path,
                      size_t n, int testnet, ut_xpub *out);

void ds_hdkey_wipe(ds_hdkey *k);

#endif
