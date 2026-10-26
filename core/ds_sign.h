/* DTMF-Signer portable core: the signing pass, the second of the two passes
 * over a PSBT.
 *
 * ds_psbt_sign reads the PSBT from the same source the analysis read, checks
 * that it is still the PSBT that was reviewed (its SHA-256 is the review's
 * psbt_sha256) before writing anything, and then streams it to a sink: every
 * byte is copied through, and each input the review marks as ours gets our
 * partial signature, inserted just before the separator of its map:
 *
 *   key   0x02 || the 33-byte compressed public key of account/chain/index
 *   value the DER ECDSA signature (RFC 6979 nonce, low S) || 0x01
 *
 * over the BIP 143 SIGHASH_ALL hash of the input, computed from the review.
 * A partial signature already in that map under our key is dropped (the
 * new one replaces it, so the key is never repeated); every other pair,
 * including other keys' signatures and unknown keys, is copied unchanged.
 * Inputs that are not ours are copied unchanged.
 *
 * Before signing an input, the key derived for it must give the P2WPKH script
 * and amount of the input's witness_utxo; and the source is hashed again while
 * it is copied. Either failing means the review and the PSBT disagree:
 * DS_ERR_PSBT, after part of the output has been written. On any error the
 * caller must discard everything the sink received. */
#ifndef DS_SIGN_H
#define DS_SIGN_H

#include "ds_psbt.h"

/* A byte sink. write takes n bytes and returns a non-negative value when it
 * has taken all of them, or a negative DS_ERR_* that ds_psbt_sign returns as
 * is. It is called with at most 64 bytes at a time. */
typedef struct {
    int32_t (*write)(void *ctx, const uint8_t *p, size_t n);
    void *ctx;
} ds_sink;

/* Signs the PSBT in src, analyzed for wallet w into *r, writing the signed
 * PSBT to dst. Returns DS_OK; DS_ERR_ARG for NULL arguments or a review that
 * cannot come from ds_psbt_analyze; DS_ERR_PSBT if the source is not the
 * reviewed PSBT (nothing written) or the checks above fail; DS_ERR_DERIVE if
 * a key cannot be derived or signing fails; or the source's or the sink's
 * error. The derived private keys, the signature hashes, the signatures and
 * the secp256k1 context are wiped on every path. */
int32_t ds_psbt_sign(const ds_source *src, const ds_wallet *w,
                     const ds_psbt_review *r, const ds_sink *dst);

#endif
