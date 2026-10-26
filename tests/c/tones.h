/* Test helper: turns tone strings into audio, and seeds and PSBTs into tones,
 * the way a sender at cable pace would. */
#ifndef DS_TEST_TONES_H
#define DS_TEST_TONES_H

#include <stddef.h>
#include <stdint.h>

#include "ur_tones.h"

#define TONES_RATE 16000

/* Renders tones at 16 kHz with the cable pace 40/20/400 ms, or keypad tones
 * (they start with '*') at 80/80/600 ms: their digits repeat, and ur-tones
 * asks a machine to play them at 80 ms + 80 ms at least. Returns the number
 * of samples written (at most cap). */
static size_t tones_render(const char *tones, int16_t *out, size_t cap)
{
    ut_synth s;
    if (tones[0] == '*') {
        ut_synth_init(&s, tones, TONES_RATE, 80, 80, 600);
    } else {
        ut_synth_init(&s, tones, TONES_RATE, 40, 20, 400);
    }
    return ut_synth_read(&s, out, cap);
}

/* The tones of a seed as a UR frame; returns their length or a negative error. */
static int32_t tones_seed_ur(const uint8_t *entropy, size_t len, const char *pin,
                             char *out, size_t cap)
{
    char ur[UT_MAX_UR + 1];
    int32_t n = ut_seed_to_ur(entropy, len, pin, ur, sizeof ur);
    if (n < 0) {
        return n;
    }
    return ut_ur_to_frame(ur, out, cap);
}

/* The tones of a seed in keypad mode. */
static int32_t tones_seed_keypad(const uint8_t *entropy, size_t len, const char *pin,
                                 char *out, size_t cap)
{
    return ut_seed_to_keypad(entropy, len, pin, out, cap);
}

/* The next fountain part of a PSBT's CBOR as tones. */
static int32_t tones_next_part(ut_fountain_encoder *e, char *out, size_t cap)
{
    ut_frame f;
    int32_t n = ut_fountain_next_frame(e, "crypto-psbt", &f);
    if (n < 0) {
        return n;
    }
    return ut_frame_encode(&f, out, cap);
}

#endif
