#include "nds_view.h"

#include "ds_btn.h"
#include "qrcodegen.h"

void nds_view_scale(const uint8_t fb[8][128], uint16_t *out, uint16_t on, uint16_t off)
{
    int x, y;
    for (y = 0; y < NDS_H; y++) {
        uint16_t *row = out + (size_t)y * NDS_W;
        if (y < 32 || y >= 160) {
            for (x = 0; x < NDS_W; x++) {
                row[x] = off;
            }
        } else {
            int sy = (y - 32) >> 1;
            for (x = 0; x < NDS_W; x++) {
                row[x] = ((fb[sy >> 3][x >> 1] >> (sy & 7)) & 1) ? on : off;
            }
        }
    }
}

int nds_view_qr(const ds_qr *q, uint16_t *out, uint16_t dark, uint16_t light)
{
    int size, s, ox, oy, x, y;
    for (x = 0; x < NDS_W * NDS_H; x++) {
        out[x] = light;
    }
    if (q->type == NULL) {
        return 0;
    }
    size = qrcodegen_getSize(q->qr);
    s = NDS_H / (size + 8);
    if (s < 1) {
        return 0;
    }
    ox = (NDS_W - (size + 8) * s) / 2 + 4 * s;
    oy = (NDS_H - (size + 8) * s) / 2 + 4 * s;
    for (y = 0; y < size * s; y++) {
        for (x = 0; x < size * s; x++) {
            if (qrcodegen_getModule(q->qr, x / s, y / s)) {
                out[(size_t)(oy + y) * NDS_W + ox + x] = dark;
            }
        }
    }
    return s;
}

int nds_view_button(uint32_t held)
{
    if (held & NDS_KEY_A) {
        return DS_BTN_OK;
    }
    if (held & (NDS_KEY_B | NDS_KEY_LEFT)) {
        return DS_BTN_LEFT;
    }
    if (held & NDS_KEY_UP) {
        return DS_BTN_UP;
    }
    if (held & NDS_KEY_DOWN) {
        return DS_BTN_DOWN;
    }
    if (held & NDS_KEY_RIGHT) {
        return DS_BTN_RIGHT;
    }
    return DS_BTN_NONE;
}

int nds_view_touch(int tx, int ty, int *x, int *y)
{
    if (tx < 0 || tx >= NDS_W || ty < 32 || ty >= 32 + 2 * 64) {
        return 0;
    }
    *x = tx / 2;
    *y = (ty - 32) / 2;
    return 1;
}
