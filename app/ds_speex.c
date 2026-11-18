/* See ds_speex.h. libspeex allocates its decoder state through spx_arena,
 * here from the ds_speex itself, so there is no heap. */
#include <string.h>

#include "ds_core.h"
#include "ds_speex.h"
#include "speex/speex.h"
#include "spx_arena.h"

int32_t ds_speex_init(ds_speex *d)
{
    if (!d) {
        return DS_ERR_ARG;
    }
    memset(d, 0, sizeof *d);
    spx_arena_use(d->mem.b, sizeof d->mem.b);
    d->st = speex_decoder_init(&speex_nb_mode);
    spx_arena_use(NULL, 0);
    if (!d->st) {
        return DS_ERR_ARG;
    }
    spx_int32_t enh = 1; /* the decoder's perceptual post-filter */
    speex_decoder_ctl(d->st, SPEEX_SET_ENH, &enh);
    return DS_OK;
}

int32_t ds_speex_frame(ds_speex *d, const uint8_t *bits, uint8_t nbytes,
                       int16_t out[DS_SPEEX_FRAME])
{
    if (!out) {
        return DS_ERR_ARG;
    }
    if (!d || !d->st || !bits || nbytes == 0 || nbytes > DS_SPEEX_MAX_BYTES) {
        memset(out, 0, DS_SPEEX_FRAME * sizeof out[0]);
        return DS_ERR_ARG;
    }
    /* libspeex reads through a char pointer it does not write: a copy keeps
     * the caller's bytes const. */
    char frame[DS_SPEEX_MAX_BYTES];
    SpeexBits sb;
    memcpy(frame, bits, nbytes);
    speex_bits_set_bit_buffer(&sb, frame, nbytes);
    if (speex_decode_int(d->st, &sb, out) != 0) {
        memset(out, 0, DS_SPEEX_FRAME * sizeof out[0]);
        return DS_ERR_ARG;
    }
    return DS_OK;
}
