/* The exchange: hook, language list, greeting, the empty menu, About,
 * barge-in, its own voice on the line, and a hang-up that keeps the session.
 * Keys are telephone keys (100 ms tone, 100 ms gap) and, as on the hardware,
 * what the signer says is mixed back into what it hears. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ds_base64.h"
#include "ds_core.h"
#include "ds_pack.h"
#include "ds_prompts.h"
#include "ds_say.h"
#include "ds_voice.h"
#include "psbt_vectors.h"
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
    char log[1 << 18]; /* echo lines, each ending in '\n' */
    size_t log_len;
    int16_t out[OUT_MAX];
    size_t out_len;
} line_t;

static line_t L;

/* Times heavy work was done inside ds_voice_line_in: a seed decrypted or a
 * wallet derived, a frame decoded (a seed group or a PSBT part taken), a
 * PSBT reviewed or signed, a part made into tones (seen as the session, the
 * receiver, the fingerprint, the signed PSBT or the parts changing there):
 * that work belongs to ds_voice_tick, never to the audio path. */
static int heavy_in_line;

/* What the signer plays into the handset, heard by a wallet: when tapping,
 * every sample of line_out goes into this listener. */
static ut_listener tap;
static int tapping;

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
    if (tapping)
        ut_listener_push(&tap, o, CHUNK);
    {
        int st = t->v.s.state, held = t->v.rx.has_group;
        uint32_t fp = t->v.seed_fp, parts = t->v.rx.parts, made = t->v.out_made;
        size_t out = t->v.cbor_len;
        ds_voice_line_in(&t->v, in, CHUNK);
        int now = t->v.s.state;
        /* a cancel (back to READY) or a wipe (0) is no work; loading,
         * deriving, reviewing, signing are */
        int worked = now != st && (now == DS_S_SEED || now == DS_S_REVIEW ||
                                   now == DS_S_CONFIRMED || (st == DS_S_SEED && now == DS_S_READY));
        if (worked || held != t->v.rx.has_group || t->v.seed_fp != fp ||
            t->v.rx.parts > parts || t->v.out_made > made || t->v.cbor_len > out)
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

/* Until the signer is quiet (at most 60 s: About spells a long version). */
static void run_quiet(line_t *t)
{
    for (int i = 0; i < 6000 && ds_voice_speaking(&t->v); i++)
        step(t, NULL);
}

/* A telephone key: 100 ms of tone, 200 ms of gap (a person typing: the
 * next key begins 300 ms after this one). */
static void press(line_t *t, char k)
{
    static int16_t buf[4800];
    char keys[2] = {k, 0};
    ut_synth s;
    memset(buf, 0, sizeof buf);
    ut_synth_init(&s, keys, 16000, 100, 200, 0);
    ut_synth_read(&s, buf, 4800);
    for (int i = 0; i < 30; i++)
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
    static int16_t buf[4800];
    ut_synth s;
    memset(buf, 0, sizeof buf);
    ut_synth_init(&s, "2", 16000, 100, 200, 0);
    ut_synth_read(&s, buf, 4800);
    size_t chosen_at = 0;
    for (int i = 0; i < 30; i++) {
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
    static int16_t buf[4800];
    ut_synth syn;
    memset(buf, 0, sizeof buf);
    ut_synth_init(&syn, "#", 16000, 100, 200, 0);
    ut_synth_read(&syn, buf, 4800);
    int seen = 0;
    for (int i = 0; i < 30; i++) {
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
    static int16_t buf[4800];
    ut_synth syn;
    heavy_in_line = 0;
    air_seed_to_pin(&L, seed_6a, "ABC123DEF456");
    run_quiet(&L);
    type_mt(&L, "ABC123DEF456");
    run_ms(&L, 1600);
    memset(buf, 0, sizeof buf);
    ut_synth_init(&syn, "#", 16000, 100, 200, 0);
    ut_synth_read(&syn, buf, 4800);
    int i = 0;
    for (; i < 30 && L.v.job == 0; i++) { /* line_in only, until the job is asked */
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
    for (; i < 30; i++)
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

/* ---- signing: the review, the hook flash, the signature out ---- */

static const psbt_vector *vector(const char *name)
{
    for (size_t i = 0; i < sizeof psbt_vectors / sizeof psbt_vectors[0]; i++)
        if (strcmp(psbt_vectors[i].name, name) == 0)
            return &psbt_vectors[i];
    CHECK(0);
    return &psbt_vectors[0];
}

/* Until the line `want` appears in the log at or after `from` (at most ms). */
static int run_until(line_t *t, const char *want, size_t from, uint32_t ms)
{
    for (uint32_t i = 0; i < ms / 10; i++) {
        if (HAS(t, want, from))
            return 1;
        step(t, NULL);
    }
    return HAS(t, want, from);
}

/* The lines `want` (NULL-terminated) one right after the other, from `from`. */
static int lines_in_a_row(const line_t *t, size_t from, const char *const *want)
{
    long at = find_line(t, want[0], from);
    while (at >= 0) {
        const char *p = t->log + at;
        int ok = 1;
        for (int i = 0; want[i] != NULL; i++) {
            size_t n = strlen(want[i]);
            if (strncmp(p, want[i], n) != 0 || p[n] != '\n') {
                ok = 0;
                break;
            }
            p += n + 1;
        }
        if (ok)
            return 1;
        at = find_line(t, want[0], (size_t)at + 1);
    }
    return 0;
}

/* The lines `want` in this order (others may come between), from `from`. */
static int lines_in_order(const line_t *t, size_t from, const char *const *want)
{
    long at = (long)from;
    for (int i = 0; want[i] != NULL; i++) {
        at = find_line(t, want[i], (size_t)at);
        if (at < 0) {
            printf("missing in order: %s\n", want[i]);
            return 0;
        }
        at += (long)strlen(want[i]) + 1;
    }
    return 1;
}

/* The seed 00..00 typed on the keypad (no PIN), on mainnet or testnet, up to
 * the menu with a seed. */
static void to_ready(line_t *t, int testnet)
{
    CHECK(tones_seed_keypad(zero16, 16, NULL, seed_tones, sizeof seed_tones) > 0);
    to_menu(t);
    if (testnet) {
        press(t, '2');
        run_quiet(t);
    }
    press(t, '1');
    run_quiet(t);
    press(t, '2');
    run_ms(t, 500);
    type_keys(t, seed_tones);
    run_ms(t, 300);
    run_quiet(t);
    press(t, '#'); /* no PIN */
    run_ms(t, 500);
    run_quiet(t);
    press(t, '#'); /* the fingerprint is right */
    run_quiet(t);
    CHECK_EQ_INT(t->v.s.state, DS_S_READY);
    CHECK(HAS(t, "state menu-ready", 0));
}

static ut_fountain_encoder psbt_enc;
static uint8_t psbt_cbor[4096];
static char part_tones[UT_MAX_FRAME + 1];

/* Sign (1), then the wallet plays the PSBT's parts next to the handset until
 * the signer stops listening. */
static void send_psbt_bytes(line_t *t, const uint8_t *psbt, size_t psbt_len)
{
    int32_t n = ut_psbt_to_cbor(psbt, psbt_len, psbt_cbor, sizeof psbt_cbor);
    CHECK(n > 0);
    CHECK(ut_fountain_encoder_init(&psbt_enc, psbt_cbor, (size_t)n, 100) >= 0);
    size_t mark = t->log_len;
    press(t, '1');
    run_quiet(t);
    CHECK(HAS(t, "state psbt-listen", mark));
    CHECK(HAS(t, "say psbt_listening", mark));
    CHECK(HAS(t, "ear listen", mark));
    for (uint32_t i = 0; i < 4 * psbt_enc.seq_len && !HAS(t, "state review", mark) && !HAS(t, "ear fail", mark);
         i++) {
        CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
        play_tones(t, part_tones);
        run_ms(t, 400);
    }
    run_ms(t, 100);
}

static void send_psbt(line_t *t, const psbt_vector *pv)
{
    send_psbt_bytes(t, pv->psbt, pv->psbt_len);
}

/* Through every item of the review to the confirmation step. */
static void review_to_confirm(line_t *t)
{
    size_t mark = t->log_len;
    for (int i = 0; i < 40 && !HAS(t, "state confirm", mark); i++) {
        run_quiet(t);
        press(t, '#');
    }
    run_quiet(t);
    CHECK(HAS(t, "say sign_hook", 0));
}

/* On-hook for ms, then off-hook again. */
static void on_hook(line_t *t, uint32_t ms)
{
    hook(t, 0);
    run_ms(t, ms);
    hook(t, 1);
    run_ms(t, 300);
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = strlen(hex) / 2;
    if (n > cap)
        return -1;
    for (size_t i = 0; i < n; i++) {
        unsigned b;
        if (sscanf(hex + 2 * i, "%2x", &b) != 1)
            return -1;
        out[i] = (uint8_t)b;
    }
    return (int)n;
}

static int contains(const uint8_t *hay, size_t n, const uint8_t *needle, size_t k)
{
    for (size_t i = 0; i + k <= n; i++)
        if (memcmp(hay + i, needle, k) == 0)
            return 1;
    return 0;
}

/* The signed PSBT the echo gave (begin, chunks, end), from `from`: its length
 * or -1. */
static int32_t echo_signed(const line_t *t, size_t from, uint8_t *buf, size_t cap)
{
    unsigned id = 0, bytes = 0, end = 0;
    size_t len = 0;
    int begun = 0;
    const char *p = t->log + from;
    while (*p) {
        const char *e = strchr(p, '\n');
        char l[DS_VOICE_ECHO_MAX];
        size_t n = (size_t)(e - p);
        if (n >= sizeof l)
            return -1;
        memcpy(l, p, n);
        l[n] = '\0';
        if (sscanf(l, "signed-psbt-begin %u %u", &id, &bytes) == 2) {
            begun = 1;
            len = 0;
        } else if (begun && strncmp(l, "signed-psbt ", 12) == 0) {
            CHECK(strlen(l + 12) <= 64);
            int32_t k = ds_base64_decode(l + 12, strlen(l + 12), buf + len, cap - len);
            if (k <= 0)
                return -1;
            len += (size_t)k;
        } else if (begun && sscanf(l, "signed-psbt-end %u", &end) == 1) {
            return end == id && len == bytes ? (int32_t)len : -1;
        }
        p = e + 1;
    }
    return -1;
}

static uint8_t fountain_work[UT_FOUNTAIN_WORK(DS_VOICE_OUT_MAX)];
static ut_fountain_decoder out_dec;

/* # plays what goes out; a wallet next to the handset decodes it. Returns
 * the message (and its type) or NULL. */
static const uint8_t *hear_tones(line_t *t, const char **type, size_t *len)
{
    static char grp[UT_GROUP + 1];
    static char ur[UT_MAX_UR + 1];
    const uint8_t *msg = NULL;
    int32_t r = 0, n;
    CHECK(ut_fountain_decoder_init(&out_dec, fountain_work, sizeof fountain_work) >= 0);
    ut_listener_init(&tap, 16000);
    tapping = 1;
    size_t mark = t->log_len;
    press(t, '#');
    CHECK(run_until(t, "say again_hint", mark, 600000));
    run_quiet(t);
    ut_listener_flush(&tap);
    tapping = 0;
    while ((n = ut_listener_group(&tap, grp, sizeof grp)) > 0) {
        if (ut_frame_to_ur(grp, ur, sizeof ur, NULL) < 0)
            continue;
        r = ut_fountain_receive(&out_dec, ur);
    }
    CHECK_EQ_INT(r, 1);
    if (r == 1 && ut_fountain_result(&out_dec, type, &msg, len) >= 0)
        return msg;
    return NULL;
}


/* A wallet looping the PSBT's parts at a pace: cable (40/20/400 ms) or air
 * (80/80/600 ms, ur-tones SPEC). */
static void play_part_paced(line_t *t, const char *tones, int air)
{
    static int16_t a[TONES_RATE * 120];
    ut_synth syn;
    if (air)
        ut_synth_init(&syn, tones, TONES_RATE, 80, 80, 600);
    else
        ut_synth_init(&syn, tones, TONES_RATE, 40, 20, 400);
    size_t n = ut_synth_read(&syn, a, sizeof a / sizeof a[0]);
    CHECK(n < sizeof a / sizeof a[0]);
    play(t, a, n);
    run_ms(t, air ? 600 : 400);
}

/* The wallet keeps looping (at either pace) all through the review and the
 * menu with a seed: nothing is a key. A person's key after a second of
 * quiet is. */
static void looping_wallet_at(int air)
{
    const psbt_vector *pv = vector("one input with change");
    to_ready(&L, 0);
    send_psbt(&L, pv);
    long rv = find_line(&L, "state review", 0);
    CHECK(rv >= 0);
    uint32_t k = ds_voice_keys(&L.v);
    for (int i = 0; i < 2; i++) {
        CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
        play_part_paced(&L, part_tones, air);
    }
    CHECK(rv >= 0 && strstr(L.log + rv + 13, "state ") == NULL);
    CHECK_EQ_INT(L.v.item, 0);
    CHECK(L.v.item_heard); /* item 0 said in full, never cut */
    CHECK(HAS(&L, "chars pe9ug", (size_t)(rv < 0 ? 0 : rv)));
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
    CHECK_EQ_INT(ds_voice_keys(&L.v), k);
    CHECK(L.v.psbt != NULL);
    CHECK(!HAS(&L, "say cancelled", 0));
    CHECK(!HAS(&L, "say wiped", 0));
    CHECK_EQ_INT(L.v.s.w.wallet_fp, 0x73c5da0au);
    /* a second of quiet, then a person's # */
    run_quiet(&L);
    run_ms(&L, 1100);
    size_t mark = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK_EQ_INT(L.v.item, 1);
    CHECK(HAS(&L, "say change", mark));
    /* * cancels: the menu with a seed, while the wallet loops on */
    press(&L, '*');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-ready", mark));
    mark = L.log_len;
    k = ds_voice_keys(&L.v);
    for (int i = 0; i < 2; i++) {
        CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
        play_part_paced(&L, part_tones, air);
    }
    CHECK(strstr(L.log + mark, "state ") == NULL); /* no wipe, no new sign */
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    CHECK_EQ_INT(ds_voice_keys(&L.v), k);
    CHECK(!HAS(&L, "say wiped", 0));
    CHECK_EQ_INT(L.v.s.w.wallet_fp, 0x73c5da0au);
}

static void test_looping_wallet_cable_pace(void)
{
    looping_wallet_at(0);
}

static void test_looping_wallet_air_pace(void)
{
    looping_wallet_at(1);
}

/* Once the PSBT is in, no key counts until the line has had no tone for a
 * second: lone 80 ms tones 800 ms apart (a wallet still playing, its other
 * tones lost) pass the length and rhythm rules, not the gate. */
static void test_gate_after_reception(void)
{
    static int16_t a[TONES_RATE];
    ut_synth syn;
    to_ready(&L, 0);
    send_psbt(&L, vector("one input with change"));
    CHECK(HAS(&L, "state review", 0));
    uint32_t k = ds_voice_keys(&L.v);
    for (int i = 0; i < 10; i++) {
        memset(a, 0, sizeof a);
        ut_synth_init(&syn, "#", TONES_RATE, 80, 720, 0);
        size_t n = ut_synth_read(&syn, a, TONES_RATE * 8 / 10);
        play(&L, a, n);
    }
    CHECK_EQ_INT(ds_voice_keys(&L.v), k);
    CHECK_EQ_INT(L.v.item, 0);
    run_quiet(&L);
    run_ms(&L, 1100);
    press(&L, '#');
    run_quiet(&L);
    CHECK_EQ_INT(L.v.item, 1);
}

/* Normal typing: a key 300 ms after a different one counts. */
static void test_typing_pace_counts(void)
{
    to_ready(&L, 0);
    size_t mark = L.log_len;
    press(&L, '9'); /* settings */
    press(&L, '2'); /* 300 ms later: always bitcoin */
    run_quiet(&L);
    CHECK(HAS(&L, "state settings", mark));
    CHECK(HAS(&L, "say unit_btc", mark));
    CHECK_EQ_INT(L.v.unit, DS_VOICE_UNIT_BTC);
}

/* A wallet that keeps looping the PSBT's parts once the signer has it: its
 * 40 ms data tones (never the same tone twice in a row) are no keys, in the
 * review or anywhere. Only a real key, held, after the item was said. */
static void test_looping_wallet_drives_nothing(void)
{
    const psbt_vector *pv = vector("one input with change");
    to_ready(&L, 0);
    send_psbt(&L, pv);
    long rv = find_line(&L, "state review", 0);
    CHECK(rv >= 0);
    uint32_t k = ds_voice_keys(&L.v);
    for (int i = 0; i < 3; i++) { /* three more parts: about a minute */
        CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
        play_tones(&L, part_tones);
        run_ms(&L, 400);
    }
    CHECK(rv >= 0 && strstr(L.log + rv + 13, "state ") == NULL);
    CHECK_EQ_INT(L.v.item, 0);
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
    CHECK_EQ_INT(ds_voice_keys(&L.v), k);
    CHECK(L.v.psbt != NULL);
    CHECK(!HAS(&L, "say cancelled", 0));
    CHECK(!HAS(&L, "say wiped", 0));
    char fp[9];
    snprintf(fp, sizeof fp, "%08x", (unsigned)L.v.s.w.wallet_fp);
    CHECK_EQ_STR(fp, "73c5da0a");
    /* then a real # once the item has been said: the next item */
    run_quiet(&L);
    size_t mark = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK_EQ_INT(L.v.item, 1);
    CHECK(HAS(&L, "say change", mark));
}

/* # while an item is still being said does not skip it; once it has been
 * said in full, # goes on. */
static void test_hash_waits_for_the_item(void)
{
    to_ready(&L, 0);
    size_t mark = L.log_len;
    send_psbt(&L, vector("one input with change"));
    CHECK(run_until(&L, "say sending", mark, 5000));
    run_ms(&L, 300);
    CHECK(ds_voice_speaking(&L.v));
    press(&L, '#');
    CHECK_EQ_INT(L.v.item, 0);
    run_quiet(&L);
    CHECK_EQ_INT(L.v.item, 0);
    CHECK(HAS(&L, "chars pe9ug", mark)); /* the item went on to its end */
    press(&L, '#');
    run_ms(&L, 300);
    CHECK_EQ_INT(L.v.item, 1);
    /* the fee likewise: # during it does not reach the confirmation */
    run_quiet(&L);
    press(&L, '#');
    run_ms(&L, 300);
    CHECK_EQ_INT(L.v.item, 2);
    press(&L, '#');
    CHECK(!HAS(&L, "state confirm", mark));
    run_quiet(&L);
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state confirm", mark));
}

/* The core's input warnings, after the fee: inputs not ours. */
static void test_warns_foreign_inputs(void)
{
    to_ready(&L, 0);
    size_t mark = L.log_len;
    send_psbt(&L, vector("foreign input is not signed"));
    run_quiet(&L);
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
    for (uint32_t i = 0; i <= L.v.s.r.n_out; i++) {
        CHECK(!HAS(&L, "say warn_inputs_unsigned", mark));
        press(&L, '#');
        run_quiet(&L);
    }
    static const char *const want[] = {"ear warn", "say warn_inputs_unsigned", "num 1", NULL};
    CHECK(lines_in_a_row(&L, mark, want));
    CHECK(!HAS(&L, "say warn_input_no_witness", mark));
    CHECK(!HAS(&L, "state confirm", mark));
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state confirm", mark));
    on_hook(&L, 500); /* and it signs the input of ours */
    run_quiet(&L);
    CHECK(HAS(&L, "state hold-near", mark));
}

/* An input of ours without witness_utxo: built from the vector with two
 * non_witness_utxo inputs by dropping the first one's witness_utxo (as
 * test_psbt does). */
static void test_warns_input_without_witness(void)
{
    static uint8_t buf[4096];
    const psbt_vector *pv = vector("two inputs with non_witness_utxo");
    size_t len = pv->psbt_len, at = 0;
    memcpy(buf, pv->psbt, len);
    for (size_t k = 5; k + 3 + 0x1f <= len; k++) {
        if (buf[k] == 1 && buf[k + 1] == 1 && buf[k + 2] == 0x1f && buf[k + 11] == 0x16 &&
            buf[k + 12] == 0 && buf[k + 13] == 0x14) {
            at = k + 3;
            break;
        }
    }
    CHECK(at != 0);
    size_t start = at - 3, end = at + 0x1f;
    memmove(buf + start, buf + end, len - end);
    len -= end - start;
    to_ready(&L, 0);
    size_t mark = L.log_len;
    send_psbt_bytes(&L, buf, len);
    run_quiet(&L);
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
    CHECK_EQ_INT(L.v.s.r.in[0].no_witness, 1);
    review_to_confirm(&L);
    static const char *const want[] = {"ear warn", "say warn_input_no_witness", NULL};
    CHECK(lines_in_a_row(&L, mark, want));
    CHECK(!HAS(&L, "say warn_inputs_unsigned", mark));
    long w = find_line(&L, "say warn_input_no_witness", mark);
    CHECK(w > find_line(&L, "say fee", mark));
    CHECK(find_line(&L, "state confirm", mark) > w);
}

static void test_review_speaks_outputs(void)
{
    heavy_in_line = 0;
    to_ready(&L, 0);
    size_t mark = L.log_len;
    send_psbt(&L, vector("one input with change"));
    run_quiet(&L);
    CHECK(HAS(&L, "ear ok", mark));
    CHECK(HAS(&L, "state review", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
    static const char *const out1[] = {"say sending", "num 60000", "say sats", "say to",
                                       "chars bc1qk", "chars pe9ug", NULL};
    CHECK(lines_in_a_row(&L, mark, out1));
    CHECK(!HAS(&L, "say change", mark));
    press(&L, '#');
    run_quiet(&L);
    static const char *const out2[] = {"say change", "num 39000", "say sats", NULL};
    CHECK(lines_in_a_row(&L, mark, out2));
    CHECK(!HAS(&L, "say fee", mark));
    press(&L, '#');
    run_quiet(&L);
    static const char *const fee[] = {"say fee", "num 1000", "say sats", NULL};
    CHECK(lines_in_a_row(&L, mark, fee));
    CHECK(!HAS(&L, "say fee_high", mark));
    CHECK(!HAS(&L, "say sign_hook", mark));
    static const char *const order[] = {"say sending", "say change", "say fee", NULL};
    CHECK(lines_in_order(&L, mark, order));
    /* 0 repeats the item */
    size_t m2 = L.log_len;
    press(&L, '0');
    run_quiet(&L);
    CHECK(lines_in_a_row(&L, m2, fee));
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state confirm", mark));
    CHECK(HAS(&L, "say sign_hook", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW); /* nothing confirmed by keys */
    /* * cancels: back to the menu, nothing signed, the PSBT gone */
    m2 = L.log_len;
    press(&L, '*');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-ready", m2));
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
    CHECK(L.v.psbt == NULL);
    CHECK(strstr(L.log, "signed-psbt") == NULL);
    CHECK_EQ_INT(heavy_in_line, 0);
}

static void test_five_reads_whole_address(void)
{
    to_ready(&L, 0);
    send_psbt(&L, vector("one input with change"));
    run_quiet(&L);
    size_t mark = L.log_len;
    press(&L, '5');
    run_quiet(&L);
    CHECK(HAS(&L, "chars bc1qkd33yck74lg0kaq4tdcmu3hk4yruhjayxpe9ug", mark));
    CHECK(!HAS(&L, "chars bc1qk", mark));
    /* 5 on the change (no address to read): nothing */
    press(&L, '#');
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '5');
    run_quiet(&L);
    CHECK(strstr(L.log + mark, "chars ") == NULL);
}

static void test_amount_units(void)
{
    to_ready(&L, 0);
    CHECK_EQ_INT(L.v.unit, DS_VOICE_UNIT_AUTO);
    size_t mark = L.log_len;
    CHECK_EQ_INT(ds_voice_say_amount(&L.v, 150000000), DS_OK);
    run_quiet(&L);
    static const char *const a1[] = {"num 1", "say point", "num 5", "say bitcoin", NULL};
    CHECK(lines_in_a_row(&L, mark, a1));
    mark = L.log_len;
    CHECK_EQ_INT(ds_voice_say_amount(&L.v, 51720), DS_OK);
    run_quiet(&L);
    static const char *const a2[] = {"num 51720", "say sats", NULL};
    CHECK(lines_in_a_row(&L, mark, a2));
    mark = L.log_len;
    CHECK_EQ_INT(ds_voice_say_amount(&L.v, 100000000), DS_OK);
    run_quiet(&L);
    static const char *const a3[] = {"num 1", "say bitcoin", NULL};
    CHECK(lines_in_a_row(&L, mark, a3));
    CHECK(!HAS(&L, "say point", mark));
    /* settings (9): always bitcoin */
    mark = L.log_len;
    press(&L, '9');
    run_quiet(&L);
    CHECK(HAS(&L, "state settings", mark));
    CHECK(HAS(&L, "say settings_menu", mark));
    CHECK(HAS(&L, "say unit_auto", mark));
    mark = L.log_len;
    press(&L, '2');
    run_quiet(&L);
    CHECK(HAS(&L, "say unit_btc", mark));
    CHECK_EQ_INT(L.v.unit, DS_VOICE_UNIT_BTC);
    mark = L.log_len;
    CHECK_EQ_INT(ds_voice_say_amount(&L.v, 51720), DS_OK);
    run_quiet(&L);
    static const char *const b1[] = {"num 0", "say point", "num 0", "num 0", "num 0",
                                     "num 5", "num 1", "num 7", "num 2", "say bitcoin",
                                     NULL};
    CHECK(lines_in_a_row(&L, mark, b1));
    /* always sats */
    press(&L, '3');
    run_quiet(&L);
    CHECK_EQ_INT(L.v.unit, DS_VOICE_UNIT_SATS);
    mark = L.log_len;
    CHECK_EQ_INT(ds_voice_say_amount(&L.v, 150000000), DS_OK);
    run_quiet(&L);
    static const char *const c1[] = {"num 150000000", "say sats", NULL};
    CHECK(lines_in_a_row(&L, mark, c1));
    /* About from the settings, and back; * back to the menu */
    mark = L.log_len;
    press(&L, '9');
    run_quiet(&L);
    CHECK(HAS(&L, "state about", mark));
    press(&L, '#');
    run_ms(&L, 100);
    CHECK(HAS(&L, "state settings", mark));
    press(&L, '*');
    run_ms(&L, 100);
    CHECK(find_line(&L, "state menu-ready", mark) > find_line(&L, "state about", mark));
    /* it lasts until the power is cut, a wipe included */
    run_quiet(&L);
    press(&L, '3');
    run_quiet(&L);
    CHECK_EQ_INT(L.v.unit, DS_VOICE_UNIT_SATS);
    ds_voice_init(&L.v, 16000, NULL, 0);
    CHECK_EQ_INT(L.v.unit, DS_VOICE_UNIT_AUTO);
}

static void test_hook_flash_signs(void)
{
    static uint8_t signed_psbt[DS_VOICE_OUT_MAX], sig[80];
    const psbt_vector *pv = vector("one input with change");
    heavy_in_line = 0;
    to_ready(&L, 0);
    send_psbt(&L, pv);
    review_to_confirm(&L);
    size_t mark = L.log_len;
    on_hook(&L, 500);
    run_quiet(&L);
    CHECK(HAS(&L, "hook-flash", mark));
    CHECK(HAS(&L, "ear ok", mark));
    CHECK(HAS(&L, "state hold-near", mark));
    CHECK(HAS(&L, "say signed_hold_near", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_READY); /* signing consumed the confirmation */
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
    int32_t n = echo_signed(&L, mark, signed_psbt, sizeof signed_psbt);
    CHECK(n > (int32_t)pv->psbt_len);
    int k = hex_to_bytes(pv->signatures[0].sig_hex, sig, sizeof sig);
    CHECK(k > 60);
    CHECK(n > 0 && k > 0 && contains(signed_psbt, (size_t)n, sig, (size_t)k));
    /* # plays it: a wallet decodes the same bytes */
    const char *type = NULL;
    size_t len = 0;
    size_t m2 = L.log_len;
    const uint8_t *msg = hear_tones(&L, &type, &len);
    CHECK(msg != NULL && type != NULL && strcmp(type, "crypto-psbt") == 0);
    const uint8_t *p = NULL;
    size_t plen = 0;
    CHECK(msg != NULL && ut_cbor_to_psbt(msg, len, &p, &plen) >= 0);
    CHECK(p != NULL && n > 0 && plen == (size_t)n && memcmp(p, signed_psbt, plen) == 0);
    /* 2 rounds of 100-byte parts, its own tones never taken as keys */
    CHECK_EQ_INT(count_lines(&L, "state tones", m2), 1);
    long first = find_line(&L, "state tones", m2), hint = find_line(&L, "say again_hint", m2);
    CHECK(first >= 0 && hint > first);
    {
        uint32_t parts = (uint32_t)((len + 99) / 100);
        int frames = 0;
        for (const char *q = L.log + first; q < L.log + hint; q = strchr(q, '\n') + 1)
            if (strncmp(q, "tones ", 6) == 0)
                frames++;
        CHECK_EQ_INT(frames, 2 * parts);
        char between[1 << 14];
        size_t bl = (size_t)(hint - first) < sizeof between - 1 ? (size_t)(hint - first)
                                                                 : sizeof between - 1;
        memcpy(between, L.log + first + 12, bl > 12 ? bl - 12 : 0);
        between[bl > 12 ? bl - 12 : 0] = '\0';
        CHECK(strstr(between, "state ") == NULL);
    }
    /* # back to the menu, with the redial note once; the signature zeroed */
    m2 = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    long rn = find_line(&L, "say redial_note", m2);
    CHECK(rn >= 0 && HAS(&L, "state menu-ready", (size_t)rn));
    CHECK(all_zero(L.v.out, sizeof L.v.out));
    CHECK_EQ_INT(L.v.cbor_len, 0);
    CHECK_EQ_INT(heavy_in_line, 0);
}

static void test_hook_timing_boundaries(void)
{
    const psbt_vector *pv = vector("one input with change");
    heavy_in_line = 0;
    to_ready(&L, 0);
    send_psbt(&L, pv);
    review_to_confirm(&L);
    /* 30 ms: nothing */
    size_t mark = L.log_len;
    on_hook(&L, 30);
    run_quiet(&L);
    CHECK(!HAS(&L, "hook-flash", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
    CHECK(strstr(L.log + mark, "state ") == NULL);
    /* 200 ms: signs */
    on_hook(&L, 200);
    run_quiet(&L);
    CHECK(strstr(L.log + mark, "signed-psbt-begin 1 ") != NULL);
    CHECK(HAS(&L, "state hold-near", mark));
    /* * leaves it; again with 1500 ms: signs */
    press(&L, '*');
    run_quiet(&L);
    CHECK(all_zero(L.v.out, sizeof L.v.out));
    send_psbt(&L, pv);
    review_to_confirm(&L);
    mark = L.log_len;
    on_hook(&L, 1500);
    run_quiet(&L);
    CHECK(strstr(L.log + mark, "signed-psbt-begin 2 ") != NULL);
    CHECK(HAS(&L, "state hold-near", mark));
    CHECK(!HAS(&L, "state idle", mark));
    press(&L, '*');
    run_quiet(&L);
    /* 1600 ms: a hang-up; nothing signed; resume asked */
    send_psbt(&L, pv);
    review_to_confirm(&L);
    mark = L.log_len;
    hook(&L, 0);
    run_ms(&L, 1600);
    hook(&L, 1);
    run_ms(&L, 300);
    CHECK(HAS(&L, "state idle", mark));
    CHECK(!HAS(&L, "hook-flash", mark));
    CHECK(strstr(L.log + mark, "signed-psbt") == NULL);
    CHECK_EQ_INT(L.v.s.state, DS_S_READY); /* the review cancelled */
    CHECK(all_zero(L.v.out, sizeof L.v.out));
    CHECK(HAS(&L, "state resume-ask", mark));
    CHECK_EQ_INT(L.v.sig_id, 2);
    /* # resumes: the PSBT must be played again */
    run_quiet(&L);
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state psbt-listen", mark));
    CHECK_EQ_INT(heavy_in_line, 0);
}

static void test_key_does_not_confirm(void)
{
    to_ready(&L, 0);
    send_psbt(&L, vector("one input with change"));
    review_to_confirm(&L);
    size_t mark = L.log_len;
    type_keys(&L, "1234567890#5#");
    run_ms(&L, 2000);
    run_quiet(&L);
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
    CHECK(strstr(L.log + mark, "state ") == NULL);
    CHECK(strstr(L.log + mark, "signed-psbt") == NULL);
    CHECK(L.v.job == 0);
    /* nor in the review before the confirmation step: a flash there is no confirmation */
    press(&L, '*');
    run_quiet(&L);
    send_psbt(&L, vector("one input with change"));
    run_quiet(&L);
    mark = L.log_len;
    on_hook(&L, 500);
    run_quiet(&L);
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
    CHECK(strstr(L.log + mark, "signed-psbt") == NULL);
}

static void test_hang_up_during_reception_restarts(void)
{
    const psbt_vector *pv = vector("one input with change");
    to_ready(&L, 0);
    int32_t n = ut_psbt_to_cbor(pv->psbt, pv->psbt_len, psbt_cbor, sizeof psbt_cbor);
    CHECK(ut_fountain_encoder_init(&psbt_enc, psbt_cbor, (size_t)n, 100) >= 0);
    press(&L, '1');
    run_quiet(&L);
    CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
    size_t na = tones_render(part_tones, seed_audio, sizeof seed_audio / sizeof seed_audio[0]);
    play(&L, seed_audio, na / 2); /* half a part */
    CHECK(L.v.rx.keys > 0);
    size_t mark = L.log_len;
    hook(&L, 0);
    run_ms(&L, 2000);
    CHECK(HAS(&L, "state idle", mark));
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state psbt-listen", mark));
    CHECK(HAS(&L, "say psbt_listening", mark));
    CHECK_EQ_INT(L.v.rx.mode, DS_RX_PSBT);
    CHECK_EQ_INT(L.v.rx.keys, 0);
    CHECK_EQ_INT(L.v.rx.parts, 0);
    /* the whole PSBT now: reviewed */
    mark = L.log_len;
    CHECK(ut_fountain_encoder_init(&psbt_enc, psbt_cbor, (size_t)n, 100) >= 0);
    for (uint32_t i = 0; i < 4 * psbt_enc.seq_len && !HAS(&L, "state review", mark); i++) {
        CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
        play_tones(&L, part_tones);
        run_ms(&L, 400);
    }
    CHECK(HAS(&L, "state review", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
}

static void test_wrong_network_rejected(void)
{
    to_ready(&L, 1); /* a testnet signer */
    size_t mark = L.log_len;
    send_psbt(&L, vector("one input with change")); /* a mainnet PSBT */
    run_quiet(&L);
    static const char *const want[] = {"ear fail", "say error_network", NULL};
    CHECK(lines_in_a_row(&L, mark, want));
    CHECK(HAS(&L, "state menu-ready", mark));
    CHECK(!HAS(&L, "state review", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
}

static void test_account_tones_decode(void)
{
    static uint8_t want[512];
    char desc[256];
    to_ready(&L, 0);
    int32_t wn = ds_wallet_account_cbor(&L.v.s.w, want, sizeof want);
    CHECK(wn > 0);
    CHECK(ds_wallet_descriptor(&L.v.s.w, desc, sizeof desc) > 0);
    size_t mark = L.log_len;
    press(&L, '2');
    run_quiet(&L);
    CHECK(HAS(&L, desc, mark)); /* the descriptor, whole, in the echo */
    CHECK(strncmp(desc, "wpkh([73c5da0a/84", 17) == 0);
    CHECK(HAS(&L, "state hold-near", mark));
    CHECK(HAS(&L, "say account_hold_near", mark));
    const char *type = NULL;
    size_t len = 0;
    const uint8_t *msg = hear_tones(&L, &type, &len);
    CHECK(msg != NULL && type != NULL && strcmp(type, "crypto-account") == 0);
    CHECK(msg != NULL && wn > 0 && len == (size_t)wn && memcmp(msg, want, len) == 0);
    /* 0 plays it again */
    size_t m2 = L.log_len;
    press(&L, '0');
    run_ms(&L, 3000);
    CHECK(HAS(&L, "state tones", m2));
    hook(&L, 0); /* a hang-up while it plays: resumed, it waits for # again */
    run_ms(&L, 2000);
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    m2 = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state hold-near", m2));
    CHECK(HAS(&L, "say account_hold_near", m2));
    /* the redial note once per call: this call has not had it */
    msg = hear_tones(&L, &type, &len);
    CHECK(msg != NULL);
    m2 = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "say redial_note", m2));
    press(&L, '2');
    run_quiet(&L);
    msg = hear_tones(&L, &type, &len);
    m2 = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(!HAS(&L, "say redial_note", m2));
    CHECK(HAS(&L, "state menu-ready", m2));
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
          test_frame_tones_not_pin_keys, test_reboot_wipes_everything,
          test_review_speaks_outputs, test_five_reads_whole_address,
          test_amount_units, test_hook_flash_signs, test_hook_timing_boundaries,
          test_key_does_not_confirm, test_hang_up_during_reception_restarts,
          test_wrong_network_rejected, test_account_tones_decode,
          test_looping_wallet_drives_nothing, test_hash_waits_for_the_item,
          test_warns_foreign_inputs, test_warns_input_without_witness,
          test_looping_wallet_cable_pace, test_looping_wallet_air_pace,
          test_typing_pace_counts, test_gate_after_reception)
