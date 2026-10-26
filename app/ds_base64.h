/* DTMF-Signer device logic: standard base64 (RFC 4648, with padding), for
 * PSBT text: the host's input and output and the device's USB echo. No
 * malloc, no globals. */
#ifndef DS_BASE64_H
#define DS_BASE64_H

#include <stddef.h>
#include <stdint.h>

#include "ds_core.h"

/* The text length of n bytes, without the NUL. */
#define DS_BASE64_LEN(n) (((n) + 2) / 3 * 4)

/* Encodes n bytes into out as NUL-terminated text. Returns the text length,
 * or DS_ERR_SPACE if cap (which counts the NUL) is too small. */
int32_t ds_base64_encode(const uint8_t *in, size_t n, char *out, size_t cap);

/* Decodes n characters. Spaces and line breaks are skipped; anything else
 * outside the alphabet, bad padding or a bad length is DS_ERR_ARG. Returns
 * the number of bytes written, or DS_ERR_SPACE if cap is too small. */
int32_t ds_base64_decode(const char *in, size_t n, uint8_t *out, size_t cap);

#endif
