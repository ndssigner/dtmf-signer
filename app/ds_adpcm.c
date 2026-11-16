#include "ds_adpcm.h"

/* The standard IMA tables; tools/voice/adpcm.py is the reference. */
static const int16_t step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767,
};
static const int8_t index_table[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

void ds_adpcm_decode(ds_adpcm *st, const uint8_t *in, size_t nibbles, int16_t *out)
{
    int32_t pred = st->pred;
    /* The state may come from a pack in a user-written flash region; an
     * index past the table must not read beyond it. */
    int32_t index = st->index > 88 ? 88 : st->index;

    for (size_t n = 0; n < nibbles; n++) {
        unsigned code = (n & 1) ? (unsigned)(in[n >> 1] >> 4) : (unsigned)(in[n >> 1] & 15);
        int32_t step = step_table[index];
        int32_t diff = step >> 3;

        if (code & 4)
            diff += step;
        if (code & 2)
            diff += step >> 1;
        if (code & 1)
            diff += step >> 2;
        pred += (code & 8) ? -diff : diff;
        if (pred > 32767)
            pred = 32767;
        else if (pred < -32768)
            pred = -32768;
        index += index_table[code & 7];
        if (index < 0)
            index = 0;
        else if (index > 88)
            index = 88;
        out[n] = (int16_t)pred;
    }
    st->pred = (int16_t)pred;
    st->index = (uint8_t)index;
}
