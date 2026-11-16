/* The exchange: hook, language list, greeting, the empty menu, About,
 * barge-in, its own voice on the line, and a hang-up that keeps the session.
 * Keys are telephone keys (100 ms tone, 100 ms gap) and, as on the hardware,
 * what the signer says is mixed back into what it hears. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ds_core.h"
#include "ds_pack.h"
#include "ds_prompts.h"
#include "ds_say.h"
#include "ds_voice.h"
#include "test.h"
#include "ur_tones.h"
#include "voice_test_packs.h"

#define CHUNK 160 /* 10 ms */
#define OUT_MAX (16000 * 60)

/* One telephone line: the signer, the clock, the echo it gave and all it said. */
typedef struct {
    ds_voice v;
    uint32_t now;
    char log[1 << 16]; /* echo lines, each ending in '\n' */
    size_t log_len;
    int16_t out[OUT_MAX];
    size_t out_len;
} line_t;

static line_t L;

static void take_echo(line_t *t)
{
    char l[DS_VOICE_ECHO_MAX];
    int32_t n;
    while ((n = ds_voice_take_echo(&t->v, l, sizeof l)) > 0) {
        if (t->log_len + (size_t)n + 2 < sizeof t->log) {
            memcpy(t->log + t->log_len, l, (size_t)n);
            t->log_len += (size_t)n;
            t->log[t->log_len++] = '\n';
            t->log[t->log_len] = '\0';
        }
    }
}

static void power_up(line_t *t, const uint8_t *region, uint32_t len)
{
    memset(t, 0, sizeof *t);
    ds_voice_init(&t->v, 16000, region, len);
    t->now = 1000;
    ds_voice_tick(&t->v, t->now);
    take_echo(t);
}

/* 10 ms of line: what the signer says goes out and comes back in, mixed with
 * key (may be NULL), clipped to int16. */
static void step(line_t *t, const int16_t *key)
{
    int16_t o[CHUNK], in[CHUNK];
    ds_voice_line_out(&t->v, o, CHUNK);
    for (int i = 0; i < CHUNK; i++) {
        int32_t s = o[i] + (key ? key[i] : 0);
        in[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    if (t->out_len + CHUNK <= OUT_MAX) {
        memcpy(t->out + t->out_len, o, sizeof o);
        t->out_len += CHUNK;
    }
    ds_voice_line_in(&t->v, in, CHUNK);
    t->now += 10;
    ds_voice_tick(&t->v, t->now);
    take_echo(t);
}

static void run_ms(line_t *t, uint32_t ms)
{
    for (uint32_t i = 0; i < ms / 10; i++)
        step(t, NULL);
}

/* Until the signer is quiet (at most 30 s). */
static void run_quiet(line_t *t)
{
    for (int i = 0; i < 3000 && ds_voice_speaking(&t->v); i++)
        step(t, NULL);
}

/* A telephone key: 100 ms of tone, 100 ms of gap. */
static void press(line_t *t, char k)
{
    static int16_t buf[3200];
    char keys[2] = {k, 0};
    ut_synth s;
    memset(buf, 0, sizeof buf);
    ut_synth_init(&s, keys, 16000, 100, 100, 0);
    ut_synth_read(&s, buf, 3200);
    for (int i = 0; i < 20; i++)
        step(t, buf + i * CHUNK);
}

static void hook(line_t *t, int off)
{
    ds_voice_hook(&t->v, off, t->now);
    take_echo(t);
}

/* Where the line `want` is in the log at or after `from`, or -1. */
static long find_line(const line_t *t, const char *want, size_t from)
{
    size_t n = strlen(want);
    const char *p = t->log + from;
    while (*p) {
        const char *e = strchr(p, '\n');
        if ((size_t)(e - p) == n && memcmp(p, want, n) == 0)
            return (long)(p - t->log);
        p = e + 1;
    }
    return -1;
}

static int count_lines(const line_t *t, const char *want, size_t from)
{
    int c = 0;
    long at = (long)from;
    while ((at = find_line(t, want, (size_t)at)) >= 0) {
        c++;
        at += (long)strlen(want) + 1;
    }
    return c;
}

#define HAS(t, s, from) (find_line((t), (s), (from)) >= 0)

/* Samples of clip id in pack p, alone, at 16 kHz. */
static size_t render_clip(const ds_pack *p, uint16_t id, int16_t *out, size_t cap)
{
    static ds_say s;
    size_t n = 0;
    ds_say_init(&s, p);
    ds_say_clip(&s, id);
    while (ds_say_busy(&s) && n < cap)
        n += ds_say_read(&s, out + n, 1);
    return n;
}

/* Off-hook with one language, through the greeting to the menu. */
static void to_menu(line_t *t)
{
    power_up(t, NULL, 0);
    hook(t, 1);
    run_ms(t, 100);
    run_quiet(t);
    press(t, '#');
    run_quiet(t);
}

static void test_one_language_no_list(void)
{
    power_up(&L, NULL, 0);
    CHECK(HAS(&L, "state idle", 0));
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    CHECK(!HAS(&L, "say lang_press", 0));
    CHECK(!HAS(&L, "state lang", 0));
    CHECK(HAS(&L, "state greet", 0));
}

static void test_greets_on_off_hook(void)
{
    power_up(&L, NULL, 0);
    run_ms(&L, 500);
    CHECK(!HAS(&L, "say greeting", 0)); /* on-hook: nothing */
    hook(&L, 1);
    run_ms(&L, 100);
    long g = find_line(&L, "say greeting", 0);
    CHECK(g >= 0);
    CHECK(g >= 0 && HAS(&L, "say flashed", (size_t)g));
    run_quiet(&L);
    CHECK_EQ_INT(ds_voice_keys(&L.v), 0);
}

static void test_languages_listed_and_chosen(void)
{
    static int16_t want[16000 * 10];
    ds_pack es;
    CHECK_EQ_INT(ds_pack_open(&es, es_bin, ES_BIN_LEN), DS_OK);
    power_up(&L, es_bin, ES_BIN_LEN);
    hook(&L, 1);
    run_ms(&L, 100);
    CHECK(HAS(&L, "state lang", 0));
    CHECK(!HAS(&L, "say greeting", 0));
    run_quiet(&L);
    CHECK_EQ_INT(count_lines(&L, "say lang_press", 0), 2);
    long c1 = find_line(&L, "say c_1", 0), c2 = find_line(&L, "say c_2", 0);
    CHECK(c1 >= 0 && c2 > c1);
    size_t mark = L.log_len;
    /* step by step to know which sample follows the choice */
    static int16_t buf[3200];
    ut_synth s;
    memset(buf, 0, sizeof buf);
    ut_synth_init(&s, "2", 16000, 100, 100, 0);
    ut_synth_read(&s, buf, 3200);
    size_t chosen_at = 0;
    for (int i = 0; i < 20; i++) {
        step(&L, buf + i * CHUNK);
        if (!chosen_at && HAS(&L, "lang es", mark))
            chosen_at = L.out_len;
    }
    CHECK(chosen_at > 0);
    long g = find_line(&L, "say greeting", mark);
    CHECK(g >= 0 && find_line(&L, "lang es", mark) < g);
    run_quiet(&L);
    /* what follows the choice is the Spanish greeting */
    size_t w = render_clip(&es, DS_P_GREETING, want, sizeof want / sizeof want[0]);
    CHECK(w > 1000 && chosen_at + w <= L.out_len);
    CHECK(chosen_at + w <= L.out_len &&
          memcmp(L.out + chosen_at, want, w * sizeof want[0]) == 0);
    CHECK(L.v.say.pack == &L.v.packs.pack[1]);
    /* and it stays Spanish after # */
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-empty", mark));
    CHECK(L.v.say.pack == &L.v.packs.pack[1]);
}

static void test_about_says_fingerprints(void)
{
    char want[16];
    power_up(&L, es_bin, ES_BIN_LEN);
    hook(&L, 1);
    run_ms(&L, 100);
    press(&L, '1'); /* English, barging into the list */
    run_quiet(&L);
    CHECK(HAS(&L, "lang en", 0));
    press(&L, '#');
    run_quiet(&L);
    size_t mark = L.log_len;
    press(&L, '9');
    run_quiet(&L);
    CHECK(HAS(&L, "state about", mark));
    snprintf(want, sizeof want, "chars %02x%02x%02x%02x", es_bin_fp[0], es_bin_fp[1],
             es_bin_fp[2], es_bin_fp[3]);
    long es_at = find_line(&L, "chars es", mark);
    CHECK(HAS(&L, "chars en", mark));
    CHECK(es_at >= 0 && HAS(&L, want, (size_t)es_at));
    /* # back to the menu */
    mark = L.log_len;
    press(&L, '#');
    run_ms(&L, 100);
    CHECK(HAS(&L, "state menu-empty", mark));
}

static void test_hash_opens_menu(void)
{
    power_up(&L, NULL, 0);
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    size_t mark = L.log_len;
    press(&L, '#');
    long st = find_line(&L, "state menu-empty", mark);
    CHECK(st >= 0);
    long m = find_line(&L, "say menu_empty", mark);
    CHECK(m > st);
    long n = find_line(&L, "say network_now_main", mark);
    CHECK(n > m);
    CHECK(find_line(&L, "say about", mark) > n);
    CHECK_EQ_INT(ds_voice_keys(&L.v), 1);
}

static void test_two_toggles_network(void)
{
    to_menu(&L);
    size_t mark = L.log_len;
    press(&L, '2');
    run_quiet(&L);
    CHECK(HAS(&L, "say network_now_test", mark));
    CHECK(!HAS(&L, "say menu_empty", mark));
    mark = L.log_len;
    press(&L, '2');
    run_quiet(&L);
    CHECK(HAS(&L, "say network_now_main", mark));
    CHECK(!HAS(&L, "say network_now_test", mark));
}

static void test_zero_repeats(void)
{
    to_menu(&L);
    size_t mark = L.log_len;
    press(&L, '0');
    run_quiet(&L);
    CHECK(HAS(&L, "say menu_empty", mark));
    CHECK(HAS(&L, "say about", mark));
    CHECK(!HAS(&L, "state menu-empty", mark)); /* the same state */
}

static void test_key_during_prompt_barges_in(void)
{
    static int16_t net[16000 * 10];
    ds_pack en;
    CHECK_EQ_INT(ds_pack_open(&en, ds_pack_en, ds_pack_en_len), DS_OK);
    size_t lnet = render_clip(&en, DS_P_NETWORK_NOW_TEST, net, sizeof net / sizeof net[0]);
    power_up(&L, NULL, 0);
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    press(&L, '#');
    run_ms(&L, 300); /* the menu prompt is still playing */
    CHECK(ds_voice_speaking(&L.v));
    size_t mark = L.log_len;
    press(&L, '2');
    CHECK(HAS(&L, "say network_now_test", mark));
    size_t at = L.out_len;
    run_quiet(&L);
    size_t rest = L.out_len - at;
    /* only the network sentence (begun during the key) is left, not the menu */
    CHECK(rest <= lnet && rest + 16000 / 2 >= lnet);
    CHECK(!HAS(&L, "say about", mark));
}

static void test_own_voice_is_not_a_key(void)
{
    power_up(&L, es_bin, ES_BIN_LEN);
    hook(&L, 1);
    run_ms(&L, 30000);
    CHECK_EQ_INT(ds_voice_keys(&L.v), 0);
    CHECK_EQ_INT(count_lines(&L, "state lang", 0), 1);
    CHECK(!HAS(&L, "state greet", 0));
    CHECK(count_lines(&L, "say lang_press", 0) >= 6); /* repeated, so it spoke */
    to_menu(&L);
    size_t mark = L.log_len;
    uint32_t k = ds_voice_keys(&L.v);
    run_ms(&L, 30000);
    CHECK_EQ_INT(ds_voice_keys(&L.v), k);
    CHECK(strstr(L.log + mark, "state ") == NULL);
    CHECK(count_lines(&L, "say menu_empty", mark) >= 2);
}

static void test_hang_up_keeps_session(void)
{
    to_menu(&L);
    press(&L, '2'); /* testnet: part of the session */
    run_quiet(&L);
    /* a short on-hook is no hang-up */
    size_t mark = L.log_len;
    hook(&L, 0);
    run_ms(&L, 500);
    hook(&L, 1);
    run_ms(&L, 300);
    CHECK(!HAS(&L, "state idle", mark));
    CHECK(!HAS(&L, "say resume_ask", mark));
    /* 2 s on-hook: hang-up; off-hook asks to resume */
    hook(&L, 0);
    run_ms(&L, 2000);
    CHECK(HAS(&L, "state idle", mark));
    CHECK(!ds_voice_speaking(&L.v));
    hook(&L, 1);
    run_ms(&L, 100);
    CHECK(HAS(&L, "state resume-ask", mark));
    CHECK(HAS(&L, "say resume_ask", mark));
    CHECK(!HAS(&L, "say greeting", mark));
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-empty", mark));
    CHECK(HAS(&L, "say menu_empty", mark));
    CHECK(HAS(&L, "say network_now_test", mark)); /* still testnet */
    /* again, and * wipes */
    hook(&L, 0);
    run_ms(&L, 2000);
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '*');
    run_quiet(&L);
    long w = find_line(&L, "say wiped", mark);
    CHECK(w >= 0);
    CHECK(find_line(&L, "state menu-empty", mark) > w);
    CHECK(HAS(&L, "say network_now_main", mark)); /* wiped: back to mainnet */
    /* power-off zeroes everything: no resume */
    power_up(&L, NULL, 0);
    hook(&L, 1);
    run_ms(&L, 100);
    CHECK(!HAS(&L, "say resume_ask", 0));
    CHECK(HAS(&L, "say greeting", 0));
}

TEST_MAIN(test_one_language_no_list, test_greets_on_off_hook,
          test_languages_listed_and_chosen, test_about_says_fingerprints,
          test_hash_opens_menu, test_two_toggles_network, test_zero_repeats,
          test_key_during_prompt_barges_in, test_own_voice_is_not_a_key,
          test_hang_up_keeps_session)
