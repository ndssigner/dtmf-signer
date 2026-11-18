/* Speex narrowband for the voice packs, from the firmware's own code: the
 * vendored libspeex in fixed point (quality 2, 5.95 kbit/s) and, to decode,
 * app/ds_speex, so what this tool decodes is what the signer plays.
 *
 *     spxtool encode IN.wav OUT.spx   (IN: WAV, 8 kHz, mono, 16-bit PCM)
 *     spxtool decode IN.spx OUT.raw   (OUT: 8 kHz mono s16le samples)
 *
 * A .spx file is the frames one after another, each a length byte (1..62)
 * and then its bytes; a 20 ms frame is 160 samples, and the last one is
 * completed with silence. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ds_core.h"
#include "ds_speex.h"
#include "speex/speex.h"
#include "spx_arena.h"

#define SPX_QUALITY 2
#define SPX_RATE 8000u

static int fail(const char *what, const char *path)
{
    fprintf(stderr, "spxtool: %s: %s\n", path, what);
    return 1;
}

/* The whole file, or NULL. */
static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    uint8_t *buf = NULL;
    size_t cap = 0, n = 0;
    for (;;) {
        if (n == cap) {
            cap = cap ? 2 * cap : 65536;
            uint8_t *more = realloc(buf, cap);
            if (!more) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = more;
        }
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (got == 0) {
            break;
        }
    }
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        free(buf);
        return NULL;
    }
    *len = n;
    return buf;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* The samples of a WAV that is 8 kHz, mono, 16-bit PCM; NULL otherwise. */
static const uint8_t *wav_samples(const uint8_t *w, size_t len, size_t *count)
{
    if (len < 12 || memcmp(w, "RIFF", 4) != 0 || memcmp(w + 8, "WAVE", 4) != 0) {
        return NULL;
    }
    int fmt_ok = 0;
    size_t at = 12;
    while (at + 8 <= len) {
        uint32_t size = le32(w + at + 4);
        const uint8_t *body = w + at + 8;
        size_t room = len - at - 8;
        if (memcmp(w + at, "fmt ", 4) == 0) {
            if (size < 16 || room < 16) {
                return NULL;
            }
            uint16_t format = le16(body);
            fmt_ok = (format == 1 || format == 0xFFFE) && le16(body + 2) == 1 &&
                     le32(body + 4) == SPX_RATE && le16(body + 14) == 16;
        } else if (memcmp(w + at, "data", 4) == 0) {
            if (!fmt_ok) {
                return NULL;
            }
            *count = (size < room ? size : room) / 2;
            return body;
        }
        if (size > room) {
            return NULL;
        }
        at += 8 + (size_t)size + (size & 1u);
    }
    return NULL;
}

static int encode(const char *in, const char *out)
{
    size_t len, count;
    uint8_t *wav = slurp(in, &len);
    if (!wav) {
        return fail("cannot read", in);
    }
    const uint8_t *pcm = wav_samples(wav, len, &count);
    if (!pcm) {
        free(wav);
        return fail("not a WAV of 8 kHz, mono, 16-bit PCM", in);
    }

    static uint8_t mem[16384]; /* the encoder's state (spx_arena) */
    spx_arena_use(mem, sizeof mem);
    void *enc = speex_encoder_init(&speex_nb_mode);
    spx_arena_use(NULL, 0);
    if (!enc) {
        free(wav);
        return fail("encoder state does not fit", in);
    }
    spx_int32_t quality = SPX_QUALITY;
    speex_encoder_ctl(enc, SPEEX_SET_QUALITY, &quality);

    FILE *f = fopen(out, "wb");
    if (!f) {
        free(wav);
        return fail("cannot write", out);
    }
    char bitbuf[256];
    SpeexBits bits;
    speex_bits_init_buffer(&bits, bitbuf, sizeof bitbuf);
    int bad = 0;
    for (size_t s = 0; s < count && !bad; s += DS_SPEEX_FRAME) {
        int16_t frame[DS_SPEEX_FRAME] = {0};
        for (size_t i = 0; i < DS_SPEEX_FRAME && s + i < count; i++) {
            frame[i] = (int16_t)le16(pcm + 2 * (s + i));
        }
        char bytes[DS_SPEEX_MAX_BYTES];
        speex_bits_reset(&bits);
        speex_encode_int(enc, frame, &bits);
        int n = speex_bits_write(&bits, bytes, sizeof bytes);
        uint8_t nb = (uint8_t)n;
        bad = n < 1 || n > DS_SPEEX_MAX_BYTES || fwrite(&nb, 1, 1, f) != 1 ||
              fwrite(bytes, 1, (size_t)n, f) != (size_t)n;
    }
    free(wav);
    if (fclose(f) != 0 || bad) {
        return fail("cannot write", out);
    }
    return 0;
}

static int decode(const char *in, const char *out)
{
    size_t len;
    uint8_t *spx = slurp(in, &len);
    if (!spx) {
        return fail("cannot read", in);
    }
    static ds_speex d;
    if (ds_speex_init(&d) != DS_OK) {
        free(spx);
        return fail("decoder state does not fit", in);
    }
    FILE *f = fopen(out, "wb");
    if (!f) {
        free(spx);
        return fail("cannot write", out);
    }
    int bad_frame = 0, bad_write = 0;
    for (size_t at = 0; at < len && !bad_frame && !bad_write;) {
        uint8_t n = spx[at++];
        int16_t pcm[DS_SPEEX_FRAME];
        uint8_t le[2 * DS_SPEEX_FRAME];
        if (n > len - at || ds_speex_frame(&d, spx + at, n, pcm) != DS_OK) {
            bad_frame = 1;
            break;
        }
        at += n;
        for (size_t i = 0; i < DS_SPEEX_FRAME; i++) {
            le[2 * i] = (uint8_t)((uint16_t)pcm[i] & 0xffu);
            le[2 * i + 1] = (uint8_t)((uint16_t)pcm[i] >> 8);
        }
        bad_write = fwrite(le, 1, sizeof le, f) != sizeof le;
    }
    free(spx);
    bad_write |= fclose(f) != 0;
    if (bad_frame) {
        return fail("bad frame", in);
    }
    if (bad_write) {
        return fail("cannot write", out);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "encode") == 0) {
        return encode(argv[2], argv[3]);
    }
    if (argc == 4 && strcmp(argv[1], "decode") == 0) {
        return decode(argv[2], argv[3]);
    }
    fprintf(stderr, "usage: spxtool encode IN.wav OUT.spx\n"
                    "       spxtool decode IN.spx OUT.raw\n");
    return 2;
}
