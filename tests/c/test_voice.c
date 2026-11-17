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
#include "tones.h"
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

/* Times a seed was decrypted or a wallet derived inside ds_voice_line_in
 * (seen as the session, the seed group or the fingerprint changing there):
 * that work belongs to ds_voice_tick, never to the audio path. */
static int heavy_in_line;

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
    {
        int st = t->v.s.state, held = t->v.rx.has_group;
        uint32_t fp = t->v.seed_fp;
        ds_voice_line_in(&t->v, in, CHUNK);
        if (t->v.s.state != st || (held && !t->v.rx.has_group) || t->v.seed_fp != fp)
            heavy_in_line++;
    }
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

/* ---- loading the seed ---- */

static const uint8_t seed_6a[16] = {0x6a, 0xa7, 0x4b, 0xc0, 0x73, 0xa6, 0x71, 0x04,
                                    0x57, 0x0c, 0xca, 0x7a, 0x05, 0x83, 0x9c, 0xa5};
static const uint8_t zero16[16];
static char seed_tones[UT_GROUP + 1];
static int16_t seed_audio[TONES_RATE * 60];

/* Audio played into the handset (or typed), with the line mixed in. */
static void play(line_t *t, const int16_t *x, size_t n)
{
    static int16_t c[CHUNK];
    for (size_t i = 0; i < n; i += CHUNK) {
        size_t k = n - i < CHUNK ? n - i : CHUNK;
        memset(c, 0, sizeof c);
        memcpy(c, x + i, k * sizeof x[0]);
        step(t, c);
    }
}

/* A wallet playing tones next to the handset. */
static void play_tones(line_t *t, const char *tones)
{
    play(t, seed_audio, tones_render(tones, seed_audio, sizeof seed_audio / sizeof seed_audio[0]));
}

/* The key that types c by multi-tap, and how many presses. */
static char mt_key(char c, int *presses)
{
    static const char *cyc[10] = {"0", "1", "2ABC", "3DEF", "4GHI",
                                  "5JKL", "6MNO", "7PQRS", "8TUV", "9WXYZ"};
    for (int k = 0; k < 10; k++) {
        const char *p = strchr(cyc[k], c);
        if (p != NULL) {
            *presses = (int)(p - cyc[k]) + 1;
            return (char)('0' + k);
        }
    }
    *presses = 0;
    return 0;
}

/* Types text by multi-tap (waiting out the same key twice), without the
 * final '#'. */
static void type_mt(line_t *t, const char *text)
{
    char prev = 0;
    for (const char *p = text; *p; p++) {
        int n;
        char k = mt_key(*p, &n);
        if (k == prev)
            run_ms(t, 1600);
        for (int i = 0; i < n; i++)
            press(t, k);
        prev = k;
    }
}

/* The PIN, then the timeout commits the last character and # ends it. */
static void type_pin(line_t *t, const char *pin)
{
    type_mt(t, pin);
    run_ms(t, 1600);
    press(t, '#');
}

static void type_keys(line_t *t, const char *keys)
{
    for (const char *p = keys; *p; p++)
        press(t, *p);
}

static int all_zero(const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
        if (b[i])
            return 0;
    return 1;
}

/* No line of the log from `from` holds a piece (4 characters or more) of secret. */
static int log_free_of(const line_t *t, const char *secret, size_t from)
{
    size_t n = strlen(secret);
    char piece[8];
    for (size_t i = 0; i + 4 <= n; i++) {
        memcpy(piece, secret + i, 4);
        piece[4] = '\0';
        if (strstr(t->log + from, piece) != NULL)
            return 0;
    }
    return 1;
}

/* The menu, then 1 (load seed): the seed_how question asked. */
static void to_seed_how(line_t *t)
{
    to_menu(t);
    press(t, '1');
    run_quiet(t);
}

/* By the handset: the warning, the wallet plays the frame, the PIN is asked. */
static void air_seed_to_pin(line_t *t, const uint8_t *seed, const char *pin)
{
    CHECK(tones_seed_ur(seed, 16, pin, seed_tones, sizeof seed_tones) > 0);
    to_seed_how(t);
    press(t, '1');
    run_quiet(t);
    play_tones(t, seed_tones);
    run_ms(t, 500);
}

static void test_air_seed_with_long_pin(void)
{
    char fp[24];
    air_seed_to_pin(&L, seed_6a, "ABC123DEF456");
    CHECK(HAS(&L, "state seed-how", 0));
    CHECK(HAS(&L, "say seed_how", 0));
    long w = find_line(&L, "say air_warning", 0);
    CHECK(w >= 0 && HAS(&L, "ear listen", (size_t)w));
    long p = find_line(&L, "state pin", 0);
    CHECK(p > w);
    CHECK(p >= 0 && HAS(&L, "say pin_ask", (size_t)p));
    CHECK(p >= 0 && HAS(&L, "say pin_warning", (size_t)p));
    run_quiet(&L);
    size_t mark = L.log_len;
    type_pin(&L, "ABC123DEF456");
    run_ms(&L, 500); /* the work runs from a tick, then ear ok, then the prompt */
    /* one per press (A twice, B three times, ...), one NEXT per character */
    CHECK_EQ_INT(count_lines(&L, "say pin-char", mark), 24);
    CHECK_EQ_INT(count_lines(&L, "ear next", mark), 13); /* and one for # */
    long f = find_line(&L, "fingerprint 8b218e81", mark);
    CHECK(f >= 0);
    CHECK(f >= 0 && HAS(&L, "state fingerprint", mark));
    CHECK(HAS(&L, "ear ok", mark));
    long fi = find_line(&L, "say fingerprint_is", mark);
    CHECK(fi >= 0 && HAS(&L, "chars 8b218e81", (size_t)fi));
    CHECK(fi >= 0 && HAS(&L, "say if_correct_hash", (size_t)fi));
    CHECK_EQ_INT(L.v.s.state, DS_S_SEED);
    CHECK(all_zero(&L.v.mt, sizeof L.v.mt));
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-ready", mark));
    CHECK(HAS(&L, "say menu_ready", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    snprintf(fp, sizeof fp, "%08x", (unsigned)L.v.s.w.wallet_fp);
    CHECK_EQ_STR(fp, "8b218e81");
}

static void test_air_seed_short_pin_refused(void)
{
    air_seed_to_pin(&L, seed_6a, "1234");
    run_quiet(&L);
    size_t mark = L.log_len;
    type_pin(&L, "1234");
    run_quiet(&L);
    long e = find_line(&L, "say error_generic", mark);
    CHECK(e >= 0);
    CHECK(e >= 0 && HAS(&L, "state seed-how", (size_t)e));
    CHECK(!HAS(&L, "state fingerprint", mark));
    CHECK(strstr(L.log + mark, "fingerprint ") == NULL);
    CHECK(L.v.s.state != DS_S_SEED && L.v.s.state != DS_S_READY);
    CHECK(all_zero(L.v.s.entropy, sizeof L.v.s.entropy));
    CHECK(all_zero(&L.v.mt, sizeof L.v.mt));
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
}

/* By the keypad: the digits, then the PIN (or none). */
static void keypad_seed(line_t *t, const char *pin)
{
    CHECK(tones_seed_keypad(zero16, 16, pin, seed_tones, sizeof seed_tones) > 0);
    to_seed_how(t);
    press(t, '2');
    run_ms(t, 500); /* the prompt is playing: the digits barge in */
    type_keys(t, seed_tones);
    run_ms(t, 300);
    CHECK(HAS(t, "say keypad_ask", 0));
    CHECK(HAS(t, "state keypad-pin", 0));
    CHECK(HAS(t, "say keypad_pin_ask", 0));
    run_quiet(t);
    if (pin != NULL)
        type_pin(t, pin);
    else
        press(t, '#');
    run_ms(t, 500);
}

static void test_keypad_seed_no_pin(void)
{
    keypad_seed(&L, NULL);
    CHECK(HAS(&L, "state seed-keypad", 0));
    CHECK(HAS(&L, "fingerprint 73c5da0a", 0));
    CHECK(HAS(&L, "chars 73c5da0a", 0));
    CHECK_EQ_INT(L.v.s.state, DS_S_SEED);
    CHECK(log_free_of(&L, seed_tones + 1, 0)); /* no run of the seed's digits */
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
    /* * at the fingerprint wipes */
    run_quiet(&L);
    size_t mark = L.log_len;
    press(&L, '*');
    run_quiet(&L);
    long w = find_line(&L, "say wiped", mark);
    CHECK(w >= 0 && HAS(&L, "state menu-empty", (size_t)w));
    CHECK_EQ_INT(L.v.s.state, 0);
    CHECK(all_zero(&L.v.s, sizeof L.v.s));
}

static void test_keypad_seed_with_pin(void)
{
    keypad_seed(&L, "TEST7");
    CHECK(HAS(&L, "fingerprint 73c5da0a", 0)); /* the PIN decrypted it */
    CHECK_EQ_INT(L.v.s.state, DS_S_SEED);
    CHECK(log_free_of(&L, seed_tones + 1, 0));
    CHECK(all_zero(&L.v.mt, sizeof L.v.mt));
}

static void test_pin_chars_never_in_echo(void)
{
    static const char pin[] = "ABC123DEF456";
    air_seed_to_pin(&L, seed_6a, pin);
    run_quiet(&L);
    size_t mark = L.log_len;
    press(&L, '7');
    press(&L, '*'); /* drops it: back */
    type_pin(&L, pin);
    run_ms(&L, 100);
    CHECK(HAS(&L, "ear back", mark));
    CHECK(HAS(&L, "fingerprint 8b218e81", mark));
    CHECK(log_free_of(&L, pin, 0));
    /* while the PIN is typed, nothing but these lines */
    long end = find_line(&L, "state fingerprint", mark);
    CHECK(end > 0);
    for (const char *p = L.log + mark; end > 0 && p < L.log + end;) {
        const char *e = strchr(p, '\n');
        int ok = (e - p == 12 && memcmp(p, "say pin-char", 12) == 0) ||
                 (e - p == 8 && memcmp(p, "ear next", 8) == 0) ||
                 (e - p == 8 && memcmp(p, "ear back", 8) == 0) ||
                 (e - p == 6 && memcmp(p, "ear ok", 6) == 0);
        if (!ok)
            printf("unexpected while typing the PIN: %.*s\n", (int)(e - p), p);
        CHECK(ok);
        p = e + 1;
    }
}

/* A hang-up mid-PIN wipes what was typed and the seed held; resuming starts
 * that input again. */
static void test_hang_up_mid_pin_wipes(void)
{
    static ut_listener fresh;
    air_seed_to_pin(&L, seed_6a, "ABC123DEF456");
    run_quiet(&L);
    type_mt(&L, "ABC1");
    CHECK(L.v.mt.len > 0);
    size_t mark = L.log_len;
    hook(&L, 0);
    run_ms(&L, 2000);
    CHECK(HAS(&L, "state idle", mark));
    CHECK(all_zero(&L.v.mt, sizeof L.v.mt));
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
    memset(&fresh, 0, sizeof fresh);
    ut_listener_init(&fresh, 16000);
    CHECK(memcmp(&L.v.l, &fresh, sizeof fresh) == 0);
    /* what the voice was saying (a PIN character's clip) is gone too */
    CHECK(all_zero(L.v.say.q, sizeof L.v.say.q));
    CHECK(L.v.say.adpcm == NULL);
    CHECK(all_zero(L.v.say.blk, sizeof L.v.say.blk));
    CHECK(all_zero(&L.v.say.st, sizeof L.v.say.st));
    CHECK_EQ_INT(L.v.say.s0, 0);
    hook(&L, 1);
    run_ms(&L, 100);
    CHECK(HAS(&L, "state resume-ask", mark));
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state seed-air", mark)); /* the seed must be played again */
    CHECK(HAS(&L, "ear listen", mark));
    play_tones(&L, seed_tones);
    run_ms(&L, 500);
    CHECK(HAS(&L, "state pin", mark));
    run_quiet(&L);
    type_pin(&L, "ABC123DEF456");
    run_ms(&L, 100);
    CHECK(HAS(&L, "fingerprint 8b218e81", mark));

    /* mid keypad entry too */
    CHECK(tones_seed_keypad(zero16, 16, NULL, seed_tones, sizeof seed_tones) > 0);
    to_seed_how(&L);
    press(&L, '2');
    run_quiet(&L);
    char part[12];
    memcpy(part, seed_tones, 11);
    part[11] = '\0';
    type_keys(&L, part);
    mark = L.log_len;
    hook(&L, 0);
    run_ms(&L, 2000);
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
    CHECK(memcmp(&L.v.l, &fresh, sizeof fresh) == 0);
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state seed-keypad", mark));
    (void)tones_next_part;
}

/* A key held long (ms of tone, then 100 ms of gap). */
static void press_long(line_t *t, char k, uint32_t ms)
{
    static int16_t buf[16000];
    char keys[2] = {k, 0};
    size_t n = (size_t)(ms + 100) * 16;
    ut_synth s;
    memset(buf, 0, sizeof buf);
    ut_synth_init(&s, keys, 16000, ms, 100, 0);
    ut_synth_read(&s, buf, n);
    play(t, buf, n);
}

/* The key that chose the keypad, held long, is not the seed's first digit:
 * no fail sound, the prompt goes on. */
static void test_keypad_choice_key_not_data(void)
{
    to_seed_how(&L);
    size_t mark = L.log_len;
    press_long(&L, '2', 400);
    run_ms(&L, 400);
    CHECK(HAS(&L, "state seed-keypad", mark));
    CHECK(!HAS(&L, "ear fail", mark));
    CHECK(ds_voice_speaking(&L.v)); /* keypad_ask not cut */
    CHECK_EQ_INT(L.v.rx.keys, 0);
}

/* Half-duplex: a seed played while the warning is said is not heard. */
static void test_air_listens_only_when_quiet(void)
{
    CHECK(tones_seed_ur(seed_6a, 16, "ABC123DEF456", seed_tones, sizeof seed_tones) > 0);
    to_seed_how(&L);
    size_t mark = L.log_len;
    press(&L, '1');
    run_ms(&L, 500);
    CHECK(ds_voice_speaking(&L.v));
    play_tones(&L, seed_tones);
    run_ms(&L, 500);
    CHECK(!HAS(&L, "state pin", mark));
    /* played again once it is quiet, it is */
    run_quiet(&L);
    play_tones(&L, seed_tones);
    run_ms(&L, 500);
    CHECK(HAS(&L, "state pin", mark));
}

/* No key of a PIN left in the listener: its groups, its live view, its
 * segmenter (the detections waiting, the current, previous and last tone). */
static int listener_holds(const ut_listener *l, const char *keys)
{
    const ut_keys *k = &l->keys;
    for (const char *c = keys; *c; c++) {
        for (uint32_t i = 0; i < 3; i++)
            if (k->look[i].key == *c)
                return 1;
        if (k->cur_key == *c || k->last_key == *c || k->prev_key == *c ||
            k->live_key == *c)
            return 1;
        if (memchr(k->current, *c, sizeof k->current) ||
            memchr(k->ready, *c, sizeof k->ready) || memchr(k->live, *c, sizeof k->live))
            return 1;
    }
    return 0;
}

static void test_pin_keys_forgotten(void)
{
    air_seed_to_pin(&L, seed_6a, "ABC123DEF456");
    run_quiet(&L);
    type_mt(&L, "ABC123DEF456");
    press(&L, '#'); /* commits the 6 */
    /* # again ends it: from the moment the PIN is taken, no digit is left */
    static int16_t buf[3200];
    ut_synth syn;
    memset(buf, 0, sizeof buf);
    ut_synth_init(&syn, "#", 16000, 100, 100, 0);
    ut_synth_read(&syn, buf, 3200);
    int seen = 0;
    for (int i = 0; i < 20; i++) {
        step(&L, buf + i * CHUNK);
        if (HAS(&L, "state fingerprint", 0)) {
            seen = 1;
            CHECK(!listener_holds(&L.v.l, "0123456789"));
        }
    }
    CHECK(seen);
    CHECK(all_zero(&L.v.mt, sizeof L.v.mt));
}

/* Said once per power-up: not on a second entry, not after a resume or a
 * wipe. */
static void test_air_warning_once(void)
{
    to_seed_how(&L);
    press(&L, '1');
    run_quiet(&L);
    hook(&L, 0); /* hang up while listening */
    run_ms(&L, 2000);
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    press(&L, '#'); /* resume: seed-air again */
    run_quiet(&L);
    hook(&L, 0);
    run_ms(&L, 2000);
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    press(&L, '*'); /* wipe */
    run_quiet(&L);
    press(&L, '1');
    run_quiet(&L);
    press(&L, '1');
    run_quiet(&L);
    CHECK_EQ_INT(count_lines(&L, "state seed-air", 0), 3);
    CHECK_EQ_INT(count_lines(&L, "ear listen", 0), 3);
    CHECK_EQ_INT(count_lines(&L, "say air_warning", 0), 1);
}

/* The decrypt and the derivation run from ds_voice_tick, a tick after the
 * key that asked for them, never inside ds_voice_line_in. */
static void test_heavy_work_in_tick(void)
{
    static int16_t buf[3200];
    ut_synth syn;
    heavy_in_line = 0;
    air_seed_to_pin(&L, seed_6a, "ABC123DEF456");
    run_quiet(&L);
    type_mt(&L, "ABC123DEF456");
    run_ms(&L, 1600);
    memset(buf, 0, sizeof buf);
    ut_synth_init(&syn, "#", 16000, 100, 100, 0);
    ut_synth_read(&syn, buf, 3200);
    int i = 0;
    for (; i < 20 && L.v.job == 0; i++) { /* line_in only, until the job is asked */
        int16_t o[CHUNK];
        ds_voice_line_out(&L.v, o, CHUNK);
        ds_voice_line_in(&L.v, buf + i * CHUNK, CHUNK);
    }
    CHECK(L.v.job != 0);
    CHECK_EQ_INT(L.v.s.state, 0); /* nothing loaded yet */
    CHECK(L.v.rx.has_group);
    L.now += 10;
    ds_voice_tick(&L.v, L.now); /* the tick that sees it: not yet */
    CHECK_EQ_INT(L.v.s.state, 0);
    L.now += 10;
    ds_voice_tick(&L.v, L.now);
    CHECK_EQ_INT(L.v.s.state, DS_S_SEED);
    CHECK_EQ_INT(L.v.job, 0);
    {
        /* the PIN taken, the listener is wiped whole: groups, live view,
         * segmenter, detector */
        static ut_listener fresh;
        memset(&fresh, 0, sizeof fresh);
        ut_listener_init(&fresh, 16000);
        CHECK(memcmp(&L.v.l, &fresh, sizeof fresh) == 0);
    }
    take_echo(&L);
    for (; i < 20; i++)
        step(&L, buf + i * CHUNK);
    run_quiet(&L);
    CHECK(HAS(&L, "fingerprint 8b218e81", 0));
    press(&L, '#'); /* the derivation, also from a tick */
    run_quiet(&L);
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    CHECK_EQ_INT(heavy_in_line, 0);
    /* and by the keypad */
    keypad_seed(&L, "TEST7");
    CHECK_EQ_INT(L.v.s.state, DS_S_SEED);
    CHECK_EQ_INT(heavy_in_line, 0);
}

/* A wallet still looping the seed frame during the PIN step: its 40 ms tones
 * are not keys (not even its '#'); a real key press still is. */
static void test_frame_tones_not_pin_keys(void)
{
    air_seed_to_pin(&L, seed_6a, "ABC123DEF456");
    run_quiet(&L);
    size_t mark = L.log_len;
    play_tones(&L, seed_tones);
    run_ms(&L, 2000);
    CHECK(!HAS(&L, "say pin-char", mark));
    CHECK(strstr(L.log + mark, "state ") == NULL);
    CHECK_EQ_INT(L.v.mt.len, 0);
    CHECK(!L.v.mt.has_pending);
    CHECK_EQ_INT(L.v.job, 0);
    press(&L, '5');
    CHECK(HAS(&L, "say pin-char", mark));
    CHECK(L.v.mt.has_pending && L.v.mt.pending == '5');
}

static void test_reboot_wipes_everything(void)
{
    keypad_seed(&L, NULL);
    CHECK_EQ_INT(L.v.s.state, DS_S_SEED);
    ds_voice_init(&L.v, 16000, NULL, 0);
    CHECK_EQ_INT(L.v.s.state, 0);
    CHECK(all_zero(&L.v.s, sizeof L.v.s));
    CHECK(all_zero(&L.v.mt, sizeof L.v.mt));
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
    CHECK_EQ_INT(L.v.seed_fp, 0);
}

TEST_MAIN(test_one_language_no_list, test_greets_on_off_hook,
          test_languages_listed_and_chosen, test_about_says_fingerprints,
          test_hash_opens_menu, test_two_toggles_network, test_zero_repeats,
          test_key_during_prompt_barges_in, test_own_voice_is_not_a_key,
          test_hang_up_keeps_session, test_air_seed_with_long_pin,
          test_air_seed_short_pin_refused, test_keypad_seed_no_pin,
          test_keypad_seed_with_pin, test_pin_chars_never_in_echo,
          test_hang_up_mid_pin_wipes, test_keypad_choice_key_not_data,
          test_air_listens_only_when_quiet, test_pin_keys_forgotten,
          test_air_warning_once, test_heavy_work_in_tick,
          test_frame_tones_not_pin_keys, test_reboot_wipes_everything)
