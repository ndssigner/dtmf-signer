#include "base64.h"

static const char ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

long ds_b64_encode(const uint8_t *in, size_t n, char *out, size_t cap)
{
    size_t i, o = 0;

    if (cap < (n + 2) / 3 * 4 + 1)
        return -1;
    for (i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = ALPHABET[v >> 18 & 63];
        out[o++] = ALPHABET[v >> 12 & 63];
        out[o++] = i + 1 < n ? ALPHABET[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? ALPHABET[v & 63] : '=';
    }
    out[o] = '\0';
    return (long)o;
}

static int value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

long ds_b64_decode(const char *in, size_t n, uint8_t *out, size_t cap)
{
    size_t i, o = 0;
    uint32_t acc = 0;
    int bits = 0, pad = 0;

    for (i = 0; i < n; i++) {
        int v;
        if (in[i] == ' ' || in[i] == '\n' || in[i] == '\r' || in[i] == '\t')
            continue;
        if (in[i] == '=') {
            pad++;
            continue;
        }
        v = value(in[i]);
        if (v < 0 || pad)
            return -1;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap)
                return -1;
            out[o++] = (uint8_t)(acc >> bits);
            acc &= (1u << bits) - 1;
        }
    }
    /* Leftover bits are the zero tail of the last group: 4 after one byte
     * (two '=' follow), 2 after two bytes (one '='), none otherwise. */
    if (acc != 0 || pad != (bits == 4 ? 2 : bits == 2 ? 1 : bits == 0 ? 0 : -1))
        return -1;
    return (long)o;
}
