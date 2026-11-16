#include "ds_crc32.h"

/* Four bits at a time: a 16-entry table (64 bytes of flash) is about four
 * times faster than bit by bit, which matters for a half-megabyte pack
 * checked at every boot. */
static const uint32_t nibble_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
    0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
};

uint32_t ds_crc32(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ nibble_table[crc & 15];
        crc = (crc >> 4) ^ nibble_table[crc & 15];
    }
    return ~crc;
}
