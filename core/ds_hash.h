/* DTMF-Signer portable core: SHA-512, HMAC-SHA512, PBKDF2-HMAC-SHA512,
 * RIPEMD-160 and HASH160. SHA-256 comes from ur-tones (ut_sha256). */
#ifndef DS_HASH_H
#define DS_HASH_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t h[8];
    uint8_t buf[128];
    uint64_t len; /* total bytes absorbed */
    uint32_t fill; /* bytes pending in buf */
} ds_sha512_ctx;

void ds_sha512_init(ds_sha512_ctx *c);
void ds_sha512_update(ds_sha512_ctx *c, const uint8_t *d, size_t n);
/* Writes the digest and wipes the context. */
void ds_sha512_final(ds_sha512_ctx *c, uint8_t out[64]);
void ds_sha512(const uint8_t *d, size_t n, uint8_t out[64]);

void ds_hmac_sha512(const uint8_t *key, size_t klen, const uint8_t *msg,
                    size_t mlen, uint8_t out[64]);

/* RFC 8018 PBKDF2 with HMAC-SHA512; derives outlen bytes (any length). */
void ds_pbkdf2_hmac_sha512(const uint8_t *pw, size_t pwlen,
                           const uint8_t *salt, size_t saltlen,
                           uint32_t iterations, uint8_t *out, size_t outlen);

void ds_ripemd160(const uint8_t *d, size_t n, uint8_t out[20]);
/* RIPEMD-160 of SHA-256. */
void ds_hash160(const uint8_t *d, size_t n, uint8_t out[20]);

#endif
