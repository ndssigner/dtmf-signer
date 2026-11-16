/* CRC-32 (IEEE 802.3, the zlib one): the integrity check of a voice pack. */
#ifndef DS_CRC32_H
#define DS_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* Continues a CRC: start with crc = 0, feed the data in any pieces. */
uint32_t ds_crc32(uint32_t crc, const uint8_t *data, size_t len);

#endif
