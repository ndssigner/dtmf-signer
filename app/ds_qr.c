#include "ds_qr.h"

#include <string.h>

/* Characters of an alphanumeric QR code at error correction L, by version. */
static const uint16_t capacity[8] = {0, 25, 47, 77, 114, 154, 195, 224};

#define MARGIN 2 /* characters the sequence number may still grow */

static void upper(char *s)
{
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char)(*s - 'a' + 'A');
        }
    }
}

/* Tries the largest fragment (200 down) whose first part fits the version.
 * Returns 0 and leaves the encoder ready, or DS_ERR_LIMIT. */
static int32_t choose(ds_qr *q, const uint8_t *msg, size_t len, int version)
{
    size_t f;
    for (f = UT_MAX_FRAGMENT; f >= 1; f--) {
        int32_t n;
        if (ut_fountain_encoder_init(&q->e, msg, len, f) < 0 ||
            q->e.seq_len > UT_MAX_PARTS) {
            return DS_ERR_LIMIT; /* smaller fragments only need more parts */
        }
        n = ut_fountain_next_ur(&q->e, q->type, q->ur, sizeof q->ur);
        if (n >= 0 && (size_t)n + MARGIN <= capacity[version]) {
            return ut_fountain_encoder_init(&q->e, msg, len, f) < 0 ? DS_ERR_LIMIT : 0;
        }
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
    if (q == NULL) {
        return DS_ERR_ARG;
    }
    if (q->type == NULL) {
        return DS_ERR_STATE;
    }
    if (ut_fountain_next_ur(&q->e, q->type, q->ur, sizeof q->ur) < 0) {
        return DS_ERR_LIMIT;
    }
    upper(q->ur);
    if (!qrcodegen_encodeText(q->ur, q->tmp, q->qr, qrcodegen_Ecc_LOW, 1,
                              q->max_version, qrcodegen_Mask_AUTO, false)) {
        return DS_ERR_LIMIT;
    }
    q->shown++;
    return qrcodegen_getSize(q->qr);
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
