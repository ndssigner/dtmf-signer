/* Standard base64 (RFC 4648, with padding), for PSBT text. */
#ifndef DS_HOST_BASE64_H
#define DS_HOST_BASE64_H

#include <stddef.h>
#include <stdint.h>

/* Encodes n bytes into out as NUL-terminated text. Returns the text length,
 * or -1 if cap (which counts the NUL) is too small. */
long ds_b64_encode(const uint8_t *in, size_t n, char *out, size_t cap);

/* Decodes n characters. Spaces and line breaks are skipped; anything else
 * outside the alphabet, bad padding or a bad length is an error. Returns the
 * number of bytes written, or -1 (also if cap is too small). */
long ds_b64_decode(const char *in, size_t n, uint8_t *out, size_t cap);

#endif
