#include "wav.h"

#include <stdio.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

/* Skips n bytes; 0 on success. */
static int skip(FILE *f, uint32_t n)
{
    while (n > 0) {
        uint8_t junk[256];
        size_t k = n < sizeof junk ? n : sizeof junk;
        if (fread(junk, 1, k, f) != k)
            return -1;
        n -= (uint32_t)k;
    }
    return 0;
}

int32_t ds_wav_read(const char *path, int16_t *samples, size_t cap, uint32_t *rate)
{
    FILE *f;
    uint8_t h[12], c[8], fmt[16];
    unsigned channels = 0;
    int have_fmt = 0;
    int32_t result = DS_ERR_AUDIO;

    if (path == NULL || rate == NULL || (samples == NULL && cap != 0))
        return DS_ERR_ARG;
    f = fopen(path, "rb");
    if (f == NULL)
        return DS_ERR_ARG;
    if (fread(h, 1, sizeof h, f) != sizeof h || memcmp(h, "RIFF", 4) ||
        memcmp(h + 8, "WAVE", 4))
        goto done;
    for (;;) {
        uint32_t size;
        if (fread(c, 1, sizeof c, f) != sizeof c)
            goto done; /* no data chunk */
        size = le32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            unsigned tag;
            if (size < sizeof fmt || fread(fmt, 1, sizeof fmt, f) != sizeof fmt)
                goto done;
            tag = le16(fmt);
            channels = le16(fmt + 2);
            *rate = le32(fmt + 4);
            /* 1: PCM; 0xFFFE: extensible (taken as PCM when 16 bits) */
            if ((tag != 1 && tag != 0xFFFE) || le16(fmt + 14) != 16 ||
                (channels != 1 && channels != 2) || *rate == 0)
                goto done;
            have_fmt = 1;
            if (skip(f, size - (uint32_t)sizeof fmt + (size & 1)) != 0)
                goto done;
        } else if (!memcmp(c, "data", 4)) {
            uint32_t frames, i;
            uint8_t b[4];
            if (!have_fmt)
                goto done;
            frames = size / (2 * channels);
            /* the header may claim more than the file holds (a truncated or
             * streamed recording): read what is there, up to cap */
            if (samples != NULL && frames > cap)
                frames = (uint32_t)cap;
            if (samples == NULL) {
                /* count what the file really holds, not what it claims */
                result = 0;
                for (i = 0; i < frames; i++) {
                    if (fread(b, 1, 2 * channels, f) != 2 * channels)
                        break;
                    result++;
                }
                goto done;
            }
            for (i = 0; i < frames; i++) {
                if (fread(b, 1, 2 * channels, f) != 2 * channels)
                    break;
                samples[i] = (int16_t)(le16(b));
            }
            result = (int32_t)i;
            goto done;
        } else if (skip(f, size + (size & 1)) != 0) {
            goto done;
        }
    }
done:
    fclose(f);
    return result;
}
