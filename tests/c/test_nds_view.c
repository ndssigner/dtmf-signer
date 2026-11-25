#include <stdint.h>
#include <string.h>

#include "ds_btn.h"
#include "ds_qr.h"
#include "nds_view.h"
#include "test.h"

/* A ds_qr is about 3 KB: static, never on the stack. */
static ds_qr q;
static uint16_t out[NDS_W * NDS_H];
static uint8_t fb[8][128];
static uint8_t msg[400];

#define ON 0xFFFFu
#define OFF 0x0000u
#define PX(x, y) out[(y) * NDS_W + (x)]

static void test_scale_corners(void)
{
    int x, y;
    memset(fb, 0, sizeof fb);
    fb[0][0] |= 1;          /* pixel (0,0) */
    fb[7][127] |= 0x80;     /* pixel (127,63) */
    nds_view_scale(fb, out, ON, OFF);
    for (y = 32; y <= 33; y++) {
        for (x = 0; x <= 1; x++) {
            CHECK_EQ_INT(PX(x, y), ON);
        }
    }
    for (y = 158; y <= 159; y++) {
        for (x = 254; x <= 255; x++) {
            CHECK_EQ_INT(PX(x, y), ON);
        }
    }
    CHECK_EQ_INT(PX(0, 0), OFF);
    CHECK_EQ_INT(PX(0, 191), OFF);
    CHECK_EQ_INT(PX(2, 32), OFF);
    CHECK_EQ_INT(PX(253, 159), OFF);
}

static void check_qr(size_t len, int scale_in, int size_want, int s_want)
{
    int s, size, ox, oy, i;
    memset(&q, 0, sizeof q);
    for (i = 0; i < (int)len; i++) {
        msg[i] = (uint8_t)(i * 31 + 7);
    }
    CHECK(ds_qr_start(&q, "crypto-psbt", msg, len, scale_in) > 0);
    size = ds_qr_next(&q);
    CHECK_EQ_INT(size, size_want);
    s = nds_view_qr(&q, out, 0x1234u, 0xFFFFu);
    CHECK_EQ_INT(s, s_want);
    if (s <= 0) {
        return;
    }
    ox = (NDS_W - (size + 8) * s) / 2 + 4 * s;
    oy = (NDS_H - (size + 8) * s) / 2 + 4 * s;
    CHECK_EQ_INT(PX(ox, oy), 0x1234u);
    CHECK_EQ_INT(PX(ox + s - 1, oy + s - 1), 0x1234u);
    CHECK_EQ_INT(PX(ox - 1, oy), 0xFFFFu);
    CHECK_EQ_INT(PX(ox, oy - 1), 0xFFFFu);
    CHECK_EQ_INT(PX(0, 0), 0xFFFFu);
    CHECK_EQ_INT(PX(NDS_W - 1, NDS_H - 1), 0xFFFFu);
}

static void test_qr_v1_and_v7(void)
{
    check_qr(1, 2, 21, 6);
    check_qr(400, 1, 45, 3);
}

static void test_qr_none(void)
{
    size_t i;
    memset(&q, 0, sizeof q);
    CHECK_EQ_INT(nds_view_qr(&q, out, 0x1234u, 0xFFFFu), 0);
    for (i = 0; i < NDS_W * NDS_H; i++) {
        if (out[i] != 0xFFFFu) {
            CHECK(0);
            break;
        }
    }
}

static void test_button_priority(void)
{
    CHECK_EQ_INT(nds_view_button(NDS_KEY_A | NDS_KEY_UP), DS_BTN_OK);
    CHECK_EQ_INT(nds_view_button(NDS_KEY_B), DS_BTN_LEFT);
    CHECK_EQ_INT(nds_view_button(NDS_KEY_LEFT | NDS_KEY_DOWN), DS_BTN_LEFT);
    CHECK_EQ_INT(nds_view_button(NDS_KEY_UP | NDS_KEY_DOWN), DS_BTN_UP);
    CHECK_EQ_INT(nds_view_button(NDS_KEY_DOWN | NDS_KEY_RIGHT), DS_BTN_DOWN);
    CHECK_EQ_INT(nds_view_button(NDS_KEY_RIGHT), DS_BTN_RIGHT);
    CHECK_EQ_INT(nds_view_button(0x400), DS_BTN_NONE);
    CHECK_EQ_INT(nds_view_button(0), DS_BTN_NONE);
}

int main(void)
{
    test_scale_corners();
    test_qr_v1_and_v7();
    test_qr_none();
    test_button_priority();
    if (ds_failures) {
        printf("test_nds_view: %d failure(s)\n", ds_failures);
        return 1;
    }
    printf("test_nds_view: ok\n");
    return 0;
}
