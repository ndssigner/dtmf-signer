/* BIP-39: entropy to mnemonic words to seed. */
#include "ds_bip39.h"

#include <string.h>

#include "ds_hash.h"
#include "ur_tones.h"

#include "ds_bip39_words.h"

#define MAX_PASSPHRASE 256
#define PREFIX "mnemonic"
#define PREFIX_LEN 8
/* 24 words of at most 8 letters, 23 spaces, NUL. */
#define MAX_SENTENCE 216

static size_t word_len(unsigned idx)
{
    const char *w = &DS_BIP39_WORDS[idx * DS_BIP39_WORD_WIDTH];
    size_t n = 0;
    while (n < DS_BIP39_WORD_WIDTH && UT_ROM_U8((const uint8_t *)&w[n]) != 0) {
        n++;
    }
    return n;
}

int32_t ds_bip39_mnemonic(const uint8_t *entropy, size_t len, char *out, size_t cap)
{
    uint8_t bits[33]; /* entropy followed by the first checksum byte */
    uint8_t sha[32];
    size_t nwords, i, total = 0, pos = 0;
    unsigned idx[24];
    int32_t rc = DS_OK;

    if (entropy == NULL || out == NULL || (len != 16 && len != 32)) {
        return DS_ERR_ARG;
    }
    nwords = (len * 8 + len / 4) / 11;
    memcpy(bits, entropy, len);
    ut_sha256(entropy, len, sha);
    bits[len] = sha[0];
    for (i = 0; i < nwords; i++) {
        unsigned v = 0, b;
        for (b = 0; b < 11; b++) {
            size_t bit = i * 11 + b;
            v = (v << 1) | (unsigned)((bits[bit / 8] >> (7 - bit % 8)) & 1u);
        }
        idx[i] = v;
        total += word_len(v) + (i ? 1 : 0);
    }
    if (total + 1 > cap) {
        rc = DS_ERR_SPACE;
    } else {
        for (i = 0; i < nwords; i++) {
            size_t n = word_len(idx[i]), k;
            if (i) {
                out[pos++] = ' ';
            }
            for (k = 0; k < n; k++) {
                out[pos++] = (char)UT_ROM_U8(
                    (const uint8_t *)&DS_BIP39_WORDS[idx[i] * DS_BIP39_WORD_WIDTH + k]);
            }
        }
        out[pos] = '\0';
        rc = (int32_t)pos;
    }
    ds_wipe(bits, sizeof bits);
    ds_wipe(sha, sizeof sha);
    ds_wipe(idx, sizeof idx);
    return rc;
}

int32_t ds_bip39_seed(const uint8_t *entropy, size_t len, const char *passphrase,
                      uint8_t seed[64])
{
    char sentence[MAX_SENTENCE];
    uint8_t salt[PREFIX_LEN + MAX_PASSPHRASE];
    size_t plen = 0, i;
    int32_t n, rc;

    if (seed == NULL) {
        return DS_ERR_ARG;
    }
    if (passphrase != NULL) {
        plen = strlen(passphrase);
        if (plen > MAX_PASSPHRASE) {
            return DS_ERR_PASSPHRASE;
        }
        for (i = 0; i < plen; i++) {
            unsigned char c = (unsigned char)passphrase[i];
            if (c < 0x20 || c > 0x7e) {
                return DS_ERR_PASSPHRASE;
            }
        }
    }
    n = ds_bip39_mnemonic(entropy, len, sentence, sizeof sentence);
    if (n < 0) {
        rc = n;
    } else {
        memcpy(salt, PREFIX, PREFIX_LEN);
        if (plen) {
            memcpy(salt + PREFIX_LEN, passphrase, plen);
        }
        ds_pbkdf2_hmac_sha512((const uint8_t *)sentence, (size_t)n, salt,
                              PREFIX_LEN + plen, 2048, seed, 64);
        rc = DS_OK;
    }
    ds_wipe(sentence, sizeof sentence);
    ds_wipe(salt, sizeof salt);
    return rc;
}
