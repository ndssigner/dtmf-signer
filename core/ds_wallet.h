/* Wallet layer: seed and wallet fingerprints and the BIP-84 account. */
#ifndef DS_WALLET_H
#define DS_WALLET_H

#include "ds_bip32.h"

typedef struct {
    uint32_t seed_fp;   /* master fingerprint with an empty passphrase */
    uint32_t wallet_fp; /* master fingerprint with the given passphrase */
    ds_hdkey account;   /* m/84'/0'/0' (m/84'/1'/0' on testnet): secret */
    uint32_t path[3];   /* the account path; ut_xpub points into it */
    int testnet;
} ds_wallet;

/* Derives the wallet from 16 or 32 bytes of entropy. passphrase NULL means "".
 * Errors are those of ds_bip39_seed (DS_ERR_ARG, DS_ERR_PASSPHRASE) and
 * ds_bip32_* . The seeds and master keys are wiped on every path; on error
 * *w is wiped. */
int32_t ds_wallet_load(ds_wallet *w, const uint8_t *entropy, size_t len,
                       const char *passphrase, int testnet);

/* Account xpub text ("xpub", or "tpub" on testnet). Returns the length or a
 * negative DS_ERR_* (DS_ERR_SPACE if cap is too small, else DS_ERR_ARG). */
int32_t ds_wallet_xpub(const ds_wallet *w, char *out, size_t cap);

/* wpkh([wallet_fp/84'/0'/0']xpub...)#checksum. Returns the length or a
 * negative DS_ERR_* (as ds_wallet_xpub). */
int32_t ds_wallet_descriptor(const ds_wallet *w, char *out, size_t cap);

/* The account as the CBOR of a crypto-account UR (BCR-2020-015, current =
 * 0): the wallet fingerprint and wpkh(@0) with the account key, as
 * ds_wallet_descriptor describes it. Returns the length or a negative
 * DS_ERR_* (as ds_wallet_xpub). */
int32_t ds_wallet_account_cbor(const ds_wallet *w, uint8_t *out, size_t cap);

void ds_wallet_wipe(ds_wallet *w);

#endif
