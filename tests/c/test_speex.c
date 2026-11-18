/* The Speex decoder against the shared vector (vectors/speex-v0.json,
 * generated into the build directory): the PCM's SHA-256 must match, the
 * same check the simulator's wasm makes. */
#include <stdint.h>
#include <string.h>

#include "ds_bitcoin.h"
#include "ds_core.h"
#include "ds_speex.h"
#include "speex_vector.h"
#include "test.h"

static void speex_vector_decodes_bit_exact(void)
{
    static ds_speex d;
    int16_t out[DS_SPEEX_FRAME];
    uint8_t le[2 * DS_SPEEX_FRAME];
    ds_sha256_ctx h;
    uint8_t digest[32];

    CHECK(sizeof(ds_speex) <= 4096);
    CHECK_EQ_INT(ds_speex_init(&d), DS_OK);
    ds_sha256_init(&h);
    for (size_t f = 0; f < SPEEX_VECTOR_FRAMES; f++) {
        const uint8_t *frame = speex_vector_bytes + speex_vector_offsets[f];
        uint8_t n = (uint8_t)(speex_vector_offsets[f + 1] - speex_vector_offsets[f]);
        CHECK_EQ_INT(ds_speex_frame(&d, frame, n, out), DS_OK);
        for (size_t i = 0; i < DS_SPEEX_FRAME; i++) {
            le[2 * i] = (uint8_t)((uint16_t)out[i] & 0xff);
            le[2 * i + 1] = (uint8_t)((uint16_t)out[i] >> 8);
        }
        ds_sha256_update(&h, le, sizeof le);
    }
    ds_sha256_final(&h, digest);
    CHECK_EQ_HEX(digest, 32, SPEEX_VECTOR_PCM_SHA256);
}

static void speex_rejects_oversized_frame(void)
{
    static ds_speex d;
    static uint8_t big[DS_SPEEX_MAX_BYTES + 1];
    int16_t out[DS_SPEEX_FRAME];

    CHECK_EQ_INT(ds_speex_init(&d), DS_OK);
    CHECK_EQ_INT(ds_speex_frame(&d, big, 0, out), DS_ERR_ARG);
    CHECK_EQ_INT(ds_speex_frame(&d, big, DS_SPEEX_MAX_BYTES + 1, out), DS_ERR_ARG);
    CHECK_EQ_INT(ds_speex_frame(&d, big, 255, out), DS_ERR_ARG);
    CHECK_EQ_INT(ds_speex_frame(&d, NULL, 15, out), DS_ERR_ARG);
    CHECK_EQ_INT(ds_speex_init(NULL), DS_ERR_ARG);
    /* A frame of the largest size decodes (all zeros: a valid frame). */
    CHECK_EQ_INT(ds_speex_frame(&d, big, DS_SPEEX_MAX_BYTES, out), DS_OK);
}

TEST_MAIN(speex_vector_decodes_bit_exact, speex_rejects_oversized_frame)
