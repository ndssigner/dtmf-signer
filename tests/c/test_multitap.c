#include <stdint.h>
#include <string.h>

#include "ds_multitap.h"
#include "test.h"

/* Feeds one key at t and checks the return code. */
static int press(ds_multitap *m, char key, uint32_t t)
{
    return ds_multitap_key(m, key, t);
}

static void test_mt_example(void)
{
    ds_multitap m;
    ds_multitap_init(&m);

    CHECK_EQ_INT(press(&m, '2', 0), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, '2');
    CHECK_EQ_INT(press(&m, '2', 100), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, 'A');
    CHECK_EQ_INT(press(&m, '2', 200), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, 'B');

    /* Not yet: 1499 ms after the last press. */
    CHECK_EQ_INT(ds_multitap_tick(&m, 200 + 1499), DS_MT_NONE);
    CHECK_EQ_INT(ds_multitap_tick(&m, 200 + 1500), DS_MT_COMMIT);
    CHECK_EQ_STR(m.text, "B");
    CHECK_EQ_INT(m.len, 1);
    CHECK_EQ_INT(m.has_pending, 0);

    CHECK_EQ_INT(press(&m, '3', 2000), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, '3');
    CHECK_EQ_INT(press(&m, '#', 2100), DS_MT_COMMIT);
    CHECK_EQ_STR(m.text, "B3");
    CHECK_EQ_INT(m.has_pending, 0);

    CHECK_EQ_INT(press(&m, '5', 2200), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, '5');
    CHECK_EQ_INT(press(&m, '*', 2300), DS_MT_BACK);
    CHECK_EQ_STR(m.text, "B3");
    CHECK_EQ_INT(m.has_pending, 0);

    CHECK_EQ_INT(press(&m, '#', 2400), DS_MT_DONE);
    CHECK_EQ_STR(m.text, "B3");
}

static void test_mt_cycle_wraps(void)
{
    ds_multitap m;
    uint32_t t = 0;
    const char want[] = { '7', 'P', 'Q', 'R', 'S' };
    int i;

    ds_multitap_init(&m);
    for (i = 0; i < 5; i++) {
        CHECK_EQ_INT(press(&m, '7', t), DS_MT_SHOW);
        CHECK_EQ_INT(m.pending, want[i]);
        t += 100;
    }
    CHECK_EQ_INT(press(&m, '7', t), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, '7');
    t += 100;
    CHECK_EQ_INT(press(&m, '7', t), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, 'P');
    CHECK_EQ_INT(m.len, 0);
}

static void test_mt_zero_and_one(void)
{
    ds_multitap m;
    ds_multitap_init(&m);

    CHECK_EQ_INT(press(&m, '0', 0), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, '0');
    CHECK_EQ_INT(press(&m, '0', 100), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, '0');
    CHECK_EQ_INT(press(&m, '1', 200), DS_MT_COMMIT);
    CHECK_EQ_STR(m.text, "0");
    CHECK_EQ_INT(press(&m, '1', 300), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, '1');
    CHECK_EQ_INT(press(&m, '1', 400), DS_MT_SHOW);
    CHECK_EQ_INT(m.pending, '1');
}

static void test_mt_other_key_commits_first(void)
{
    ds_multitap m;
    ds_multitap_init(&m);

    press(&m, '2', 0);
    press(&m, '2', 100);          /* pending 'A' */
    CHECK_EQ_INT(press(&m, '4', 200), DS_MT_COMMIT);
    CHECK_EQ_STR(m.text, "A");
    CHECK_EQ_INT(m.pending, '4');
    CHECK_EQ_INT(m.has_pending, 1);
}

static void test_mt_timeout_before_key(void)
{
    ds_multitap m;
    ds_multitap_init(&m);

    press(&m, '2', 0);            /* pending '2' */
    /* The same key 2 s later starts a new character, not the next letter. */
    CHECK_EQ_INT(press(&m, '2', 2000), DS_MT_COMMIT);
    CHECK_EQ_STR(m.text, "2");
    CHECK_EQ_INT(m.pending, '2');
}

static void test_mt_wrap_safe_time(void)
{
    ds_multitap m;
    ds_multitap_init(&m);

    /* The clock wraps between the press and the tick. */
    press(&m, '3', 0xFFFFFF00u);
    CHECK_EQ_INT(ds_multitap_tick(&m, 0xFFFFFF00u + 1499u), DS_MT_NONE);
    CHECK_EQ_INT(ds_multitap_tick(&m, 0xFFFFFF00u + 1500u), DS_MT_COMMIT);
    CHECK_EQ_STR(m.text, "3");
}

static void test_mt_star_and_hash(void)
{
    ds_multitap m;
    ds_multitap_init(&m);

    CHECK_EQ_INT(press(&m, '*', 0), DS_MT_CANCEL);
    CHECK_EQ_INT(press(&m, '#', 0), DS_MT_DONE);

    press(&m, '2', 0);
    CHECK_EQ_INT(press(&m, '*', 100), DS_MT_BACK);
    CHECK_EQ_INT(m.has_pending, 0);
    CHECK_EQ_STR(m.text, "");

    press(&m, '2', 200);
    press(&m, '#', 300);          /* commits 'A' */
    CHECK_EQ_STR(m.text, "2");
    CHECK_EQ_INT(press(&m, '*', 400), DS_MT_BACK);
    CHECK_EQ_STR(m.text, "");
    CHECK_EQ_INT(press(&m, '*', 500), DS_MT_CANCEL);
}

static void test_mt_ignores_other_keys(void)
{
    ds_multitap m;
    ds_multitap_init(&m);

    press(&m, '2', 0);
    CHECK_EQ_INT(press(&m, 'A', 100), DS_MT_NONE);
    CHECK_EQ_INT(press(&m, 'D', 200), DS_MT_NONE);
    CHECK_EQ_INT(m.pending, '2');
    CHECK_EQ_INT(m.has_pending, 1);
    CHECK_EQ_INT(m.len, 0);
}

static void test_mt_limit(void)
{
    ds_multitap m;
    uint32_t t = 0;
    int i;

    ds_multitap_init(&m);
    for (i = 0; i < 70; i++) {
        press(&m, '2', t);        /* pending '2' ... */
        press(&m, '#', t + 1);    /* ... committed by '#' */
        t += 2000;
        t += 10;
    }
    CHECK_EQ_INT(m.len, 63);
    CHECK_EQ_INT(strlen(m.text), 63);
    CHECK_EQ_INT(m.text[63], 0);
    CHECK_EQ_INT(press(&m, '5', t), DS_MT_NONE);
    CHECK_EQ_INT(m.has_pending, 0);
}

static void test_mt_wipe(void)
{
    ds_multitap m;
    const unsigned char *p = (const unsigned char *)&m;
    size_t i, bad = 0;

    ds_multitap_init(&m);
    press(&m, '2', 0);
    press(&m, '2', 10);
    press(&m, '#', 20);
    press(&m, '9', 30);
    ds_multitap_wipe(&m);
    for (i = 0; i < sizeof m; i++) {
        if (p[i] != 0) {
            bad++;
        }
    }
    CHECK_EQ_INT(bad, 0);
}

TEST_MAIN(test_mt_example, test_mt_cycle_wraps, test_mt_zero_and_one,
          test_mt_other_key_commits_first, test_mt_timeout_before_key,
          test_mt_wrap_safe_time, test_mt_star_and_hash,
          test_mt_ignores_other_keys, test_mt_limit, test_mt_wipe)
