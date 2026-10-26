#include <stdint.h>

#include "ds_btn.h"
#include "test.h"

/* Holds class cls from t to t + ms (1 ms steps); returns the number of events
 * and keeps the last one in *btn / *kind. */
static int hold(ds_btn *b, int cls, uint32_t *t, uint32_t ms, int *btn, int *kind)
{
    uint32_t end = *t + ms;
    int n = 0;
    while (*t < end) {
        int k = 0;
        int r = ds_btn_feed(b, cls, *t, &k);
        if (r != DS_BTN_NONE) {
            n++;
            *btn = r;
            *kind = k;
        }
        (*t)++;
    }
    return n;
}

static void test_btn_debounce(void)
{
    ds_btn b;
    uint32_t t = 0;
    int btn = 0, kind = 0, n;

    ds_btn_init(&b);
    n = hold(&b, DS_BTN_OK, &t, 20, &btn, &kind);
    n += hold(&b, DS_BTN_NONE, &t, 100, &btn, &kind);
    CHECK_EQ_INT(n, 0);

    n = hold(&b, DS_BTN_OK, &t, 40, &btn, &kind);
    CHECK_EQ_INT(n, 0);
    n = hold(&b, DS_BTN_NONE, &t, 100, &btn, &kind);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(btn, DS_BTN_OK);
    CHECK_EQ_INT(kind, DS_BTN_PRESS);
    /* Nothing more afterwards. */
    CHECK_EQ_INT(hold(&b, DS_BTN_NONE, &t, 200, &btn, &kind), 0);

    /* A bounce of 10 ms in a held button changes nothing. */
    n = hold(&b, DS_BTN_UP, &t, 100, &btn, &kind);
    n += hold(&b, DS_BTN_NONE, &t, 10, &btn, &kind);
    n += hold(&b, DS_BTN_UP, &t, 100, &btn, &kind);
    CHECK_EQ_INT(n, 0);
    n = hold(&b, DS_BTN_NONE, &t, 100, &btn, &kind);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(btn, DS_BTN_UP);
    CHECK_EQ_INT(kind, DS_BTN_PRESS);
}

static void test_btn_ramp(void)
{
    ds_btn b;
    uint32_t t = 0;
    int btn = 0, kind = 0, n, c;

    ds_btn_init(&b);
    CHECK_EQ_INT(hold(&b, DS_BTN_RIGHT, &t, 200, &btn, &kind), 0);
    /* Releasing RIGHT crosses the voltages of the other buttons, 5 ms each. */
    n = 0;
    for (c = DS_BTN_DOWN; c >= DS_BTN_LEFT; c--) {
        n += hold(&b, c, &t, 5, &btn, &kind);
    }
    CHECK_EQ_INT(n, 0);
    n = hold(&b, DS_BTN_NONE, &t, 100, &btn, &kind);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(btn, DS_BTN_RIGHT);
    CHECK_EQ_INT(kind, DS_BTN_PRESS);
    CHECK_EQ_INT(hold(&b, DS_BTN_NONE, &t, 500, &btn, &kind), 0);
}

static void test_btn_long_wipe(void)
{
    ds_btn b;
    uint32_t t = 0;
    int btn = 0, kind = 0, n;

    ds_btn_init(&b);
    /* OK held 1600 ms: one LONG, and releasing gives no PRESS. */
    n = hold(&b, DS_BTN_OK, &t, 1600, &btn, &kind);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(btn, DS_BTN_OK);
    CHECK_EQ_INT(kind, DS_BTN_LONG);
    CHECK_EQ_INT(hold(&b, DS_BTN_NONE, &t, 200, &btn, &kind), 0);

    /* LEFT held 3100 ms: LONG at 1500, WIPE at 3000, nothing on release. */
    {
        uint32_t t0 = t;
        int k = 0, r;
        int longs = 0, wipes = 0;
        uint32_t t_long = 0, t_wipe = 0;
        while (t < t0 + 3100) {
            r = ds_btn_feed(&b, DS_BTN_LEFT, t, &k);
            if (r != DS_BTN_NONE) {
                CHECK_EQ_INT(r, DS_BTN_LEFT);
                if (k == DS_BTN_LONG) {
                    longs++;
                    t_long = t - t0;
                } else if (k == DS_BTN_WIPE) {
                    wipes++;
                    t_wipe = t - t0;
                } else {
                    CHECK(0);
                }
            }
            t++;
        }
        CHECK_EQ_INT(longs, 1);
        CHECK_EQ_INT(wipes, 1);
        CHECK(t_long >= 1500 && t_long < 1510);
        CHECK(t_wipe >= 3000 && t_wipe < 3010);
        CHECK_EQ_INT(hold(&b, DS_BTN_NONE, &t, 200, &btn, &kind), 0);
    }

    /* OK never reaches WIPE, however long it is held. */
    n = hold(&b, DS_BTN_OK, &t, 4000, &btn, &kind);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(kind, DS_BTN_LONG);
}

static void test_btn_change_while_held(void)
{
    ds_btn b;
    uint32_t t = 0;
    int btn = 0, kind = 0, n;

    ds_btn_init(&b);
    hold(&b, DS_BTN_UP, &t, 100, &btn, &kind);
    /* Straight to DOWN: the release of UP is a PRESS of UP. */
    n = hold(&b, DS_BTN_DOWN, &t, 100, &btn, &kind);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(btn, DS_BTN_UP);
    CHECK_EQ_INT(kind, DS_BTN_PRESS);
    n = hold(&b, DS_BTN_NONE, &t, 100, &btn, &kind);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(btn, DS_BTN_DOWN);
}

TEST_MAIN(test_btn_debounce, test_btn_ramp, test_btn_long_wipe,
          test_btn_change_while_held)
