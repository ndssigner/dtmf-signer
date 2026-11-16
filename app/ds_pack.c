#include "ds_pack.h"

#include "ds_core.h"

#define HEADER 8u /* "DSV1", language, u16 count */
#define ENTRY 12u /* u32 offset, u32 bytes, i16 pred, u8 index, u8 pad */

static uint32_t rd32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static const uint8_t *entry(const ds_pack *p, uint16_t id)
{
    return p->data + HEADER + (uint32_t)id * ENTRY;
}

int32_t ds_pack_open(ds_pack *p, const uint8_t *data, uint32_t len)
{
    if (!data || len < HEADER || data[0] != 'D' || data[1] != 'S' || data[2] != 'V' ||
        data[3] != '1')
        return DS_ERR_ARG;
    for (int k = 4; k < 6; k++)
        if (data[k] < 'a' || data[k] > 'z')
            return DS_ERR_ARG;
    uint16_t count = (uint16_t)(data[6] | data[7] << 8);
    /* count is at most 65535, so the table size cannot overflow 32 bits. */
    if ((uint32_t)count * ENTRY > len - HEADER)
        return DS_ERR_ARG;

    ds_pack q = {data, len, count, {(char)data[4], (char)data[5], 0}};
    for (uint16_t id = 0; id < count; id++) {
        const uint8_t *e = entry(&q, id);
        uint32_t offset = rd32(e), bytes = rd32(e + 4);
        /* Compared as len - offset so that offset + bytes cannot wrap. */
        if (offset > len || bytes > len - offset || e[10] > 88)
            return DS_ERR_ARG;
    }
    *p = q;
    return DS_OK;
}

int32_t ds_pack_clip(const ds_pack *p, uint16_t id, const uint8_t **adpcm,
                     uint32_t *bytes, ds_adpcm *start)
{
    if (id >= p->count)
        return DS_ERR_ARG;
    const uint8_t *e = entry(p, id);
    *adpcm = p->data + rd32(e);
    *bytes = rd32(e + 4);
    start->pred = (int16_t)(uint16_t)(e[8] | e[9] << 8);
    start->index = e[10];
    return DS_OK;
}
