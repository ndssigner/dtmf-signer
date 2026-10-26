/* Addresses of standard output scripts: base58check, bech32 and bech32m. */
#include "ds_bitcoin.h"

#include <string.h>

#include "ur_tones.h"

static const char B58[] UT_ROM =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const char B32[] UT_ROM = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

/* Base58Check of version byte + 20 bytes: 25 bytes -> at most 35 digits. */
static int32_t base58check(uint8_t version, const uint8_t h[20], char *out,
                           size_t cap)
{
    uint8_t raw[25], sum[32];
    uint8_t dig[35]; /* base-58 digits, least significant first */
    size_t nd = 0, zeros = 0, i, j, total;

    raw[0] = version;
    memcpy(raw + 1, h, 20);
    ds_sha256d(raw, 21, sum);
    memcpy(raw + 21, sum, 4);

    while (zeros < 25 && raw[zeros] == 0) {
        zeros++;
    }
    memset(dig, 0, sizeof dig);
    for (i = zeros; i < 25; i++) {
        unsigned carry = raw[i];
        for (j = 0; j < nd || carry != 0; j++) {
            carry += 256u * (j < nd ? dig[j] : 0u);
            dig[j] = (uint8_t)(carry % 58);
            carry /= 58;
        }
        nd = j;
    }
    total = zeros + nd;
    if (cap < total + 1) {
        return DS_ERR_SPACE;
    }
    for (i = 0; i < zeros; i++) {
        out[i] = '1';
    }
    for (i = 0; i < nd; i++) {
        out[zeros + i] = (char)UT_ROM_U8((const uint8_t *)&B58[dig[nd - 1 - i]]);
    }
    out[total] = '\0';
    return (int32_t)total;
}

static uint32_t polymod_step(uint32_t chk, uint8_t v)
{
    static const uint32_t gen[5] UT_ROM = {0x3b6a57b2u, 0x26508e6du,
                                           0x1ea119fau, 0x3d4233ddu,
                                           0x2a1462b3u};
    uint32_t top = chk >> 25;
    int i;

    chk = (chk & 0x1ffffffu) << 5 ^ v;
    for (i = 0; i < 5; i++) {
        if ((top >> i) & 1) {
            chk ^= UT_ROM_U32(&gen[i]);
        }
    }
    return chk;
}

/* Segwit address: hrp "bc"/"tb", witness version, program (BIP 173/350). */
static int32_t segwit(int testnet, unsigned ver, const uint8_t *prog,
                      size_t plen, char *out, size_t cap)
{
    uint8_t data[1 + 52 + 6]; /* version + up to 52 groups (32 bytes) + checksum */
    size_t nd = 0, i, o = 0;
    uint32_t chk = 1, mod;
    uint32_t acc = 0;
    int bits = 0;
    const char *hrp = testnet ? "tb" : "bc";

    data[nd++] = (uint8_t)ver;
    for (i = 0; i < plen; i++) {
        acc = acc << 8 | prog[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            data[nd++] = (uint8_t)((acc >> bits) & 31);
        }
    }
    if (bits > 0) {
        data[nd++] = (uint8_t)((acc << (5 - bits)) & 31);
    }
    if (cap < 2 + 1 + nd + 6 + 1) {
        return DS_ERR_SPACE;
    }
    for (i = 0; i < 2; i++) {
        chk = polymod_step(chk, (uint8_t)((uint8_t)hrp[i] >> 5));
    }
    chk = polymod_step(chk, 0);
    for (i = 0; i < 2; i++) {
        chk = polymod_step(chk, (uint8_t)((uint8_t)hrp[i] & 31));
    }
    for (i = 0; i < nd; i++) {
        chk = polymod_step(chk, data[i]);
    }
    for (i = 0; i < 6; i++) {
        chk = polymod_step(chk, 0);
    }
    mod = chk ^ (ver == 0 ? 1u : 0x2bc830a3u);
    for (i = 0; i < 6; i++) {
        data[nd++] = (uint8_t)((mod >> (5 * (5 - i))) & 31);
    }
    out[o++] = hrp[0];
    out[o++] = hrp[1];
    out[o++] = '1';
    for (i = 0; i < nd; i++) {
        out[o++] = (char)UT_ROM_U8((const uint8_t *)&B32[data[i]]);
    }
    out[o] = '\0';
    return (int32_t)o;
}

int32_t ds_address(const uint8_t *s, size_t len, int testnet, char *out,
                   size_t cap)
{
    if (len == 25 && s[0] == 0x76 && s[1] == 0xa9 && s[2] == 0x14 &&
        s[23] == 0x88 && s[24] == 0xac) {
        return base58check(testnet ? 0x6f : 0x00, s + 3, out, cap);
    }
    if (len == 23 && s[0] == 0xa9 && s[1] == 0x14 && s[22] == 0x87) {
        return base58check(testnet ? 0xc4 : 0x05, s + 2, out, cap);
    }
    if (len == 22 && s[0] == 0x00 && s[1] == 0x14) {
        return segwit(testnet, 0, s + 2, 20, out, cap);
    }
    if (len == 34 && s[0] == 0x00 && s[1] == 0x20) {
        return segwit(testnet, 0, s + 2, 32, out, cap);
    }
    if (len == 34 && s[0] == 0x51 && s[1] == 0x20) {
        return segwit(testnet, 1, s + 2, 32, out, cap);
    }
    return DS_ERR_UNSUPPORTED;
}
