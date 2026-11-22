/* The Pico voice firmware's line I/O helpers: the hook read from the line's
 * voltage and the output queue the PWM interrupt plays. Pure C, no SDK. */
#include <stdint.h>

#include "line_io.h"
#include "test.h"

#define OFF 1000u /* off-hook below this reading */
#define ON 1500u  /* on-hook above this one */

/* Feeds k readings of raw; returns how many averages completed. */
static int feed(ds_hook *h, uint16_t raw, int k)
{
    int done = 0;

    for (int i = 0; i < k; i++) {
        done += ds_hook_feed(h, raw, OFF, ON);
    }
    return done;
}

static void test_hook_starts_on_hook(void)
{
    ds_hook h;

    ds_hook_init(&h);
    CHECK_EQ_INT(h.off_hook, 0);
}

static void test_hook_averages_a_millisecond(void)
{
    ds_hook h;

    ds_hook_init(&h);
    CHECK_EQ_INT(feed(&h, 500, DS_HOOK_AVERAGE - 1), 0);
    CHECK_EQ_INT(h.off_hook, 0); /* not decided until the average is complete */
    CHECK_EQ_INT(feed(&h, 500, 1), 1);
    CHECK_EQ_INT(h.off_hook, 1);
    CHECK_EQ_INT(feed(&h, 500, 3 * DS_HOOK_AVERAGE), 3);
}

static void test_hook_hysteresis(void)
{
    ds_hook h;

    ds_hook_init(&h);
    feed(&h, 1200, DS_HOOK_AVERAGE); /* between the two: stays on-hook */
    CHECK_EQ_INT(h.off_hook, 0);
    feed(&h, OFF - 1, DS_HOOK_AVERAGE);
    CHECK_EQ_INT(h.off_hook, 1);
    feed(&h, 1200, DS_HOOK_AVERAGE); /* between: stays off-hook */
    CHECK_EQ_INT(h.off_hook, 1);
    feed(&h, ON, DS_HOOK_AVERAGE); /* at the threshold: not over it */
    CHECK_EQ_INT(h.off_hook, 1);
    feed(&h, ON + 1, DS_HOOK_AVERAGE);
    CHECK_EQ_INT(h.off_hook, 0);
}

/* A tone on the line rides on the voltage: the average sees its middle, so
 * a large one does not flip the hook. */
static void test_hook_ignores_a_tone(void)
{
    ds_hook h;

    ds_hook_init(&h);
    feed(&h, 600, DS_HOOK_AVERAGE);
    CHECK_EQ_INT(h.off_hook, 1);
    for (uint32_t i = 0; i < 40 * DS_HOOK_AVERAGE; i++) {
        ds_hook_feed(&h, (i & 1) ? 100 : 1100, OFF, ON); /* swings past ON's half */
        CHECK_EQ_INT(h.off_hook, 1);
    }
}

static void test_out_queue_in_order(void)
{
    int16_t buf[8];
    int16_t x[5] = {1, -2, 3, -4, 5};
    ds_out o;

    ds_out_init(&o, buf, 8);
    CHECK_EQ_INT(ds_out_level(&o), 0);
    CHECK_EQ_INT(ds_out_room(&o, 6), 6);
    ds_out_push(&o, x, 5);
    CHECK_EQ_INT(ds_out_level(&o), 5);
    CHECK_EQ_INT(ds_out_room(&o, 6), 1);
    CHECK_EQ_INT(ds_out_room(&o, 4), 0); /* already past the lead */
    for (int i = 0; i < 5; i++) {
        CHECK_EQ_INT(ds_out_pop(&o), x[i]);
    }
    CHECK_EQ_INT(o.underruns, 0);
}

static void test_out_wraps(void)
{
    int16_t buf[8];
    ds_out o;

    ds_out_init(&o, buf, 8);
    for (int16_t i = 0; i < 100; i++) {
        int16_t a[3] = {i, (int16_t)(i + 1000), (int16_t)(-i)};

        ds_out_push(&o, a, 3);
        CHECK_EQ_INT(ds_out_pop(&o), i);
        CHECK_EQ_INT(ds_out_pop(&o), i + 1000);
        CHECK_EQ_INT(ds_out_pop(&o), -i);
    }
    CHECK_EQ_INT(ds_out_level(&o), 0);
}

/* Never more than the buffer holds: room is capped by it. */
static void test_out_room_is_capped(void)
{
    int16_t buf[8];
    ds_out o;

    ds_out_init(&o, buf, 8);
    CHECK_EQ_INT(ds_out_room(&o, 100), 8);
}

/* Nothing waiting (the main loop is busy): silence, counted. */
static void test_out_underrun_is_silence(void)
{
    int16_t buf[8];
    int16_t x[1] = {7};
    ds_out o;

    ds_out_init(&o, buf, 8);
    CHECK_EQ_INT(ds_out_pop(&o), 0);
    CHECK_EQ_INT(ds_out_pop(&o), 0);
    CHECK_EQ_INT(o.underruns, 2);
    ds_out_push(&o, x, 1);
    CHECK_EQ_INT(ds_out_pop(&o), 7);
    CHECK_EQ_INT(o.underruns, 2);
}

static void test_pwm_level(void)
{
    CHECK_EQ_INT(ds_out_pwm(0, 1023), 512);
    CHECK_EQ_INT(ds_out_pwm(-32768, 1023), 0);
    CHECK_EQ_INT(ds_out_pwm(32767, 1023), 1023);
    CHECK_EQ_INT(ds_out_pwm(16384, 1023), 768);
    CHECK_EQ_INT(ds_out_pwm(0, 255), 128);
    CHECK_EQ_INT(ds_out_pwm(32767, 255), 255);
}

TEST_MAIN(test_hook_starts_on_hook, test_hook_averages_a_millisecond, test_hook_hysteresis,
          test_hook_ignores_a_tone, test_out_queue_in_order, test_out_wraps,
          test_out_room_is_capped, test_out_underrun_is_silence, test_pwm_level)
