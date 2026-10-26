#include "ds_qr.h"

#include <string.h>


static void upper(char *s)
{
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char)(*s - 'a' + 'A');
        }
    }
}

/* The highest part number sent: the sequence starts over after it. */
static uint32_t last_part(const ut_fountain_encoder *e)
{
    return 2 * e->seq_len + 10;
}

/* Makes part number n (as uppercase text in q->ur) and encodes it as a QR
 * code of at most max_version; returns the size or a negative error. */
static int32_t make_part(ds_qr *q, uint32_t n, int max_version)
{
    q->e.seq_num = n - 1;
    if (ut_fountain_next_ur(&q->e, q->type, q->ur, sizeof q->ur) < 0) {
        return DS_ERR_LIMIT;
    }
    upper(q->ur);
    if (!qrcodegen_encodeText(q->ur, q->tmp, q->qr, qrcodegen_Ecc_LOW, 1,
                              max_version, qrcodegen_Mask_AUTO, false)) {
        return DS_ERR_LIMIT;
    }
    return qrcodegen_getSize(q->qr);
}

/* Part number W made with the message CRC-32 in its CBOR forced to the
 * longest form (5 bytes): the CRC is the one field whose size depends on the
 * message's bytes, so the measure holds for every message of this length
 * (ds_app_fits_qr probes with zeros). Which fragments a part mixes depends
 * on the CRC too, not its size. */
static int32_t make_last_part(ds_qr *q, int max_version)
{
    uint32_t crc = q->e.checksum;
    int32_t r;

    q->e.checksum = 0xFFFFFFFFu;
    r = make_part(q, last_part(&q->e), max_version);
    q->e.checksum = crc;
    return r;
}

/* Tries the largest fragment (200 down) whose part number W, the highest
 * sent, really encodes in the version (the sequence number only grows its
 * digits and its CBOR bytes up to W). Returns 0 with the encoder ready at
 * the start, or DS_ERR_LIMIT. */
static int32_t choose(ds_qr *q, const uint8_t *msg, size_t len, int version)
{
    size_t f = UT_MAX_FRAGMENT;
    while (f >= 1) {
        size_t got;
        if (ut_fountain_encoder_init(&q->e, msg, len, f) < 0) {
            return DS_ERR_LIMIT; /* smaller fragments only need more parts */
        }
        if (make_last_part(q, version) >= 0) {
            q->e.seq_num = 0;
            return 0;
        }
        /* Every maximum down to this fragment's own length gives the same
         * fragment: skip them (each init is a CRC-32 over the message). */
        got = q->e.fragment;
        f = (got < f ? got : f) - 1;
    }
    return DS_ERR_LIMIT;
}

int32_t ds_qr_start(ds_qr *q, const char *type, const uint8_t *msg, size_t len, int scale)
{
    int32_t r;
    if (q == NULL || type == NULL || msg == NULL || len == 0 ||
        (scale != 1 && scale != 2)) {
        return DS_ERR_ARG;
    }
    memset(q, 0, sizeof *q);
    q->type = type;
    if (scale == 2) {
        r = choose(q, msg, len, 3);
        if (r == 0) {
            q->scale = 2;
            q->max_version = 3;
            return 2;
        }
    }
    r = choose(q, msg, len, 7);
    if (r != 0) {
        q->type = NULL;
        return DS_ERR_LIMIT;
    }
    q->scale = 1;
    q->max_version = 7;
    return 1;
}

int32_t ds_qr_next(ds_qr *q)
{
    int32_t size;
    if (q == NULL) {
        return DS_ERR_ARG;
    }
    if (q->type == NULL) {
        return DS_ERR_STATE;
    }
    size = make_part(q, (q->e.seq_num >= last_part(&q->e) ? 0 : q->e.seq_num) + 1,
                     q->max_version);
    if (size >= 0) {
        q->shown++;
    }
    return size;
}

void ds_qr_draw(const ds_qr *q, uint8_t fb[8][128])
{
    int page, x, y, size, box, off;
    for (page = 0; page < 8; page++) {
        memset(fb[page], 0, 64);
    }
    if (q->type == NULL || q->shown == 0) {
        return;
    }
    size = qrcodegen_getSize(q->qr);
    box = (size + 2) * q->scale;
    off = (64 - box) / 2;
    for (y = 0; y < box; y++) {
        for (x = 0; x < box; x++) {
            int mx = x / q->scale - 1, my = y / q->scale - 1;
            int light = mx < 0 || my < 0 || mx >= size || my >= size ||
                        !qrcodegen_getModule(q->qr, mx, my);
            if (light) {
                fb[(off + y) / 8][off + x] |= (uint8_t)(1u << ((off + y) % 8));
            }
        }
    }
}
