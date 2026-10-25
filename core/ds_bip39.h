/* BIP-39: entropy to mnemonic words to seed. English word list only. */
#ifndef DS_BIP39_H
#define DS_BIP39_H

#include "ds_core.h"

/* Writes the mnemonic for 16 or 32 bytes of entropy to out as NUL-terminated
 * words separated by single spaces. Returns the length without the NUL, or
 * DS_ERR_ARG (bad len) / DS_ERR_SPACE (cap too small, including the NUL).
 * out holds a secret: the caller wipes it. */
int32_t ds_bip39_mnemonic(const uint8_t *entropy, size_t len, char *out, size_t cap);

/* Seed = PBKDF2-HMAC-SHA512(mnemonic, "mnemonic" + passphrase, 2048, 64).
 * passphrase NULL means "". Only printable ASCII (0x20-0x7E), at most 256
 * bytes: otherwise DS_ERR_PASSPHRASE. DS_ERR_ARG for a bad len. The
 * intermediate mnemonic and salt are wiped on every path. */
int32_t ds_bip39_seed(const uint8_t *entropy, size_t len, const char *passphrase,
                      uint8_t seed[64]);

#endif
