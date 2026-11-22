/* The exchange: hook, language list, greeting, the empty menu, About,
 * barge-in, its own voice on the line, and a hang-up that keeps the session.
 * Keys are telephone keys (100 ms tone, 100 ms gap) and, as on the hardware,
 * what the signer says is mixed back into what it hears. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ds_base64.h"
#include "ds_core.h"
#include "ds_crc32.h"
#include "ds_pack.h"
#include "ds_prompts.h"
#include "ds_say.h"
#include "ds_speex.h"
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

/* A key at a pace: tone_ms of tone, gap_ms of silence. */
static void press_paced(line_t *t, char k, uint32_t tone_ms, uint32_t gap_ms)
{
    static int16_t buf[16000];
    char keys[2] = {k, 0};
    size_t n = (size_t)(tone_ms + gap_ms) * 16;
    ut_synth s;
    memset(buf, 0, sizeof buf);
    ut_synth_init(&s, keys, 16000, tone_ms, gap_ms, 0);
    ut_synth_read(&s, buf, n);
    for (size_t i = 0; i + CHUNK <= n; i += CHUNK)
        step(t, buf + i);
}

/* A fast press (100 ms tone, 100 ms gap): the same key again, multi-tap. */
static void press_fast(line_t *t, char k)
{
    press_paced(t, k, 100, 100);
}

static void hook(line_t *t, int off)
{
    ds_voice_hook(&t->v, off, t->now);
    take_echo(t);
}

/* On-hook for ms, then off-hook again (a hook flash when 200 to 1500). */
static void on_hook(line_t *t, uint32_t ms)
{
    hook(t, 0);
    run_ms(t, ms);
    hook(t, 1);
    run_ms(t, 300);
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

/* Off-hook with one language: the greeting, then the menu. */
static void to_menu(line_t *t)
{
    power_up(t, NULL, 0);
    hook(t, 1);
    run_ms(t, 100);
    run_quiet(t);
}

static int lines_in_a_row(const line_t *t, size_t from, const char *const *want);

static void test_one_language_no_list(void)
{
    power_up(&L, NULL, 0);
    CHECK(HAS(&L, "state idle", 0));
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    CHECK(!HAS(&L, "say lang_press", 0));
    CHECK(!HAS(&L, "state lang", 0));
    CHECK(HAS(&L, "state menu-empty", 0));
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
    CHECK(g >= 0 && HAS(&L, "state menu-empty", (size_t)g));
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
    /* and the menu, in Spanish */
    CHECK(HAS(&L, "state menu-empty", mark));
    CHECK(HAS(&L, "say choose_option", mark));
    CHECK(L.v.say.pack == &L.v.packs.pack[1]);
}

static void test_about_says_fingerprints(void)
{
    char want[32];
    power_up(&L, es_bin, ES_BIN_LEN);
    hook(&L, 1);
    run_ms(&L, 100);
    press(&L, '1'); /* English, barging into the list */
    run_quiet(&L);
    CHECK(HAS(&L, "lang en", 0));
    size_t mark = L.log_len;
    press(&L, '9');
    run_quiet(&L);
    CHECK(HAS(&L, "state about", mark));
    /* 8 bytes of each pack's SHA-256, 16 characters */
    memcpy(want, "chars ", 6);
    for (int i = 0; i < 8; i++)
        snprintf(want + 6 + 2 * i, 3, "%02x", es_bin_fp[i]);
    long es_at = find_line(&L, "chars es", mark);
    CHECK(HAS(&L, "chars en", mark));
    /* each datum after the data motif: the version, each language, each fingerprint */
    {
        static const char *const ver[] = {"say about_is", "ear data", NULL};
        static const char *const lang[] = {"say about_voice", "ear data", "chars es", "ear data", NULL};
        CHECK(lines_in_a_row(&L, mark, ver));
        CHECK(lines_in_a_row(&L, mark, lang));
        const char *const fpw[] = {"ear data", want, NULL};
        CHECK(lines_in_a_row(&L, mark, fpw));
    }
    CHECK(es_at >= 0 && HAS(&L, want, (size_t)es_at));
    /* # back to the menu */
    mark = L.log_len;
    press(&L, '#');
    run_ms(&L, 100);
    CHECK(HAS(&L, "state menu-empty", mark));
}

/* A key the menu has no option for (#) is heard and does nothing. */
static void test_hash_in_menu_does_nothing(void)
{
    to_menu(&L);
    size_t mark = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK_EQ_INT(ds_voice_keys(&L.v), 1);
    CHECK(L.log_len == mark);
}

static void test_two_toggles_network(void)
{
    to_menu(&L);
    size_t mark = L.log_len;
    press(&L, '2');
    run_quiet(&L);
    /* the new network, then the option as it now is */
    static const char *const to_test[] = {"say net_test_say", "ear option", "say opt_net_main", NULL};
    CHECK(lines_in_a_row(&L, mark, to_test));
    CHECK(!HAS(&L, "say choose_option", mark));
    CHECK(L.v.testnet);
    mark = L.log_len;
    press(&L, '2');
    run_quiet(&L);
    static const char *const to_main[] = {"say net_main_say", "ear option", "say opt_net_test", NULL};
    CHECK(lines_in_a_row(&L, mark, to_main));
    CHECK(!HAS(&L, "say net_test_say", mark));
    CHECK(!L.v.testnet);
}

static void test_zero_repeats(void)
{
    to_menu(&L);
    size_t mark = L.log_len;
    press(&L, '0');
    run_quiet(&L);
    CHECK(HAS(&L, "say choose_option", mark));
    CHECK(HAS(&L, "say opt_about", mark));
    CHECK(!HAS(&L, "state menu-empty", mark)); /* the same state */
}

static void test_key_during_prompt_barges_in(void)
{
    static int16_t net[16000 * 10];
    ds_pack en;
    CHECK_EQ_INT(ds_pack_open(&en, ds_pack_en, ds_pack_en_len), DS_OK);
    size_t lnet = render_clip(&en, DS_P_NET_TEST_SAY, net, sizeof net / sizeof net[0]);
    size_t lopt = render_clip(&en, DS_P_OPT_NET_MAIN, net, sizeof net / sizeof net[0]);
    const uint16_t *hz, *ms;
    uint8_t n;
    uint32_t motif = 0;
    CHECK_EQ_INT(ds_say_motif(DS_EAR_OPTION, &hz, &ms, &n), DS_OK);
    for (uint8_t i = 0; i < n; i++)
        motif += ms[i];
    /* what answers the key: the new network, a pause, the option as it is now */
    size_t answer = lnet + (300 + motif) * 16 + lopt;
    power_up(&L, NULL, 0);
    hook(&L, 1);
    for (int i = 0; i < 500 && !HAS(&L, "say choose_option", 0); i++)
        step(&L, NULL);
    CHECK(HAS(&L, "say choose_option", 0));
    run_ms(&L, 300); /* the menu prompt is still playing */
    CHECK(ds_voice_speaking(&L.v));
    size_t mark = L.log_len;
    press(&L, '2');
    CHECK(HAS(&L, "say net_test_say", mark));
    size_t at = L.out_len;
    run_quiet(&L);
    size_t rest = L.out_len - at;
    /* only the answer (begun during the key) is left, not the menu */
    CHECK(rest <= answer && rest + 16000 / 2 >= answer);
    CHECK(!HAS(&L, "say opt_about", mark));
}

static void test_own_voice_is_not_a_key(void)
{
    power_up(&L, es_bin, ES_BIN_LEN);
    hook(&L, 1);
    run_ms(&L, 30000);
    CHECK_EQ_INT(ds_voice_keys(&L.v), 0);
    CHECK_EQ_INT(count_lines(&L, "state lang", 0), 1);
    CHECK(!HAS(&L, "say greeting", 0));
    CHECK_EQ_INT(count_lines(&L, "say lang_press", 0), 4); /* the list, then the help */
    to_menu(&L);
    size_t mark = L.log_len;
    uint32_t k = ds_voice_keys(&L.v);
    run_ms(&L, 30000);
    CHECK_EQ_INT(ds_voice_keys(&L.v), k);
    CHECK(strstr(L.log + mark, "state ") == NULL);
    CHECK_EQ_INT(count_lines(&L, "say choose_option_long", mark), 1); /* the help spoke */
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
    CHECK(HAS(&L, "say choose_option", mark));
    CHECK(HAS(&L, "say opt_net_main", mark)); /* still testnet */
    /* again, and * wipes */
    hook(&L, 0);
    run_ms(&L, 2000);
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '*'); /* asks to wipe: no key wipes, the hook does */
    run_quiet(&L);
    CHECK(HAS(&L, "state wipe-confirm", mark));
    CHECK(HAS(&L, "say wipe_hook", mark));
    CHECK(!HAS(&L, "say wiped", mark));
    press(&L, '#'); /* back to the question */
    run_quiet(&L);
    CHECK(find_line(&L, "state resume-ask", mark) > find_line(&L, "state wipe-confirm", mark));
    press(&L, '*');
    run_quiet(&L);
    on_hook(&L, 500);
    run_quiet(&L);
    long w = find_line(&L, "say wiped", mark);
    CHECK(w >= 0);
    CHECK(find_line(&L, "state menu-empty", mark) > w);
    CHECK(HAS(&L, "say opt_net_test", (size_t)(w < 0 ? 0 : w))); /* wiped: back to mainnet */
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
        for (int i = 0; i < n; i++) {
            if (i + 1 < n)
                press_fast(t, k); /* the same key again: fast */
            else
                press(t, k);
        }
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
    CHECK(w >= 0 && HAS(&L, "ear receive", (size_t)w));
    long p = find_line(&L, "state pin", 0);
    CHECK(p > w);
    CHECK(p >= 0 && HAS(&L, "say pin_ask", (size_t)p));
    CHECK(p >= 0 && HAS(&L, "say pin_warning", (size_t)p));
    run_quiet(&L);
    size_t mark = L.log_len;
    type_pin(&L, "ABC123DEF456");
    run_ms(&L, 500); /* the work runs from a tick, then ear ok, then the prompt */
    run_quiet(&L);
    /* one per press (A twice, B three times, ...), one NEXT per character */
    CHECK_EQ_INT(count_lines(&L, "say pin-char", mark), 24);
    CHECK_EQ_INT(count_lines(&L, "ear option", mark), 13); /* and one for # */
    long f = find_line(&L, "fingerprint 8b218e81", mark);
    CHECK(f >= 0);
    CHECK(f >= 0 && HAS(&L, "state fingerprint", mark));
    CHECK(HAS(&L, "ear ok", mark));
    long fi = find_line(&L, "say fingerprint_is", mark);
    CHECK(fi >= 0 && HAS(&L, "chars 8b218e81", (size_t)fi));
    static const char *const fpd[] = {"say fingerprint_is", "ear data", "chars 8b218e81", NULL};
    CHECK(lines_in_a_row(&L, mark, fpd));
    CHECK(fi >= 0 && HAS(&L, "say if_correct_hash", (size_t)fi));
    CHECK_EQ_INT(L.v.s.state, DS_S_SEED);
    CHECK(all_zero(&L.v.mt, sizeof L.v.mt));
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-ready", mark));
    CHECK(HAS(&L, "say opt_sign", mark));
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
    long e = find_line(&L, "say seed_refused", mark); /* by air: 12 characters or more */
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
    run_quiet(t);
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
    /* * at the fingerprint asks to wipe; # goes back; * and a flash wipe */
    run_quiet(&L);
    size_t mark = L.log_len;
    press(&L, '*');
    run_quiet(&L);
    CHECK(HAS(&L, "say wipe_hook", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_SEED);
    press(&L, '#');
    run_quiet(&L);
    CHECK(find_line(&L, "state fingerprint", mark) > find_line(&L, "state wipe-confirm", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_SEED);
    press(&L, '*');
    run_quiet(&L);
    on_hook(&L, 500);
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
    CHECK(HAS(&L, "ear close", mark));
    CHECK(HAS(&L, "fingerprint 8b218e81", mark));
    CHECK(log_free_of(&L, pin, 0));
    /* while the PIN is typed, nothing but these lines */
    long end = find_line(&L, "state fingerprint", mark);
    CHECK(end > 0);
    for (const char *p = L.log + mark; end > 0 && p < L.log + end;) {
        const char *e = strchr(p, '\n');
        int ok = (e - p == 12 && memcmp(p, "say pin-char", 12) == 0) ||
                 (e - p == 10 && memcmp(p, "ear option", 10) == 0) ||
                 (e - p == 9 && memcmp(p, "ear close", 9) == 0) ||
                 (e - p == 6 && memcmp(p, "ear ok", 6) == 0) ||
                 (e - p == 15 && memcmp(p, "say seed_loaded", 15) == 0) ||
                 (e - p == 16 && memcmp(p, "say net_main_say", 16) == 0);
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
    CHECK(L.v.say.frames == NULL);
    CHECK(all_zero(L.v.say.blk, sizeof L.v.say.blk));
    CHECK(all_zero(&L.v.say.dec, sizeof L.v.say.dec));
    CHECK_EQ_INT(L.v.say.s0, 0);
    hook(&L, 1);
    run_ms(&L, 100);
    CHECK(HAS(&L, "state resume-ask", mark));
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state seed-how", mark)); /* the question again, not the reception */
    CHECK(!HAS(&L, "state seed-air", mark));
    press(&L, '1'); /* the seed must be played again */
    run_quiet(&L);
    CHECK(HAS(&L, "state seed-air", mark));
    CHECK(HAS(&L, "ear receive", mark));
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
    CHECK(HAS(&L, "state seed-how", mark));
    CHECK(!HAS(&L, "state seed-keypad", mark));
    press(&L, '2');
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
    size_t mark = L.log_len;
    press(&L, '#'); /* resume: a reception is not resumed, its question is */
    run_quiet(&L);
    CHECK(HAS(&L, "state seed-how", mark));
    CHECK(!HAS(&L, "state seed-air", mark));
    press(&L, '1'); /* seed-air again */
    run_quiet(&L);
    hook(&L, 0);
    run_ms(&L, 2000);
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    press(&L, '*'); /* wipe, confirmed by the hook */
    run_quiet(&L);
    on_hook(&L, 500);
    run_quiet(&L);
    press(&L, '1');
    run_quiet(&L);
    press(&L, '1');
    run_quiet(&L);
    CHECK_EQ_INT(count_lines(&L, "state seed-air", 0), 3);
    CHECK_EQ_INT(count_lines(&L, "ear receive", 0), 3);
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
    CHECK(HAS(t, "ear receive", mark));
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
static int16_t paced_audio[TONES_RATE * 150];

/* A part's tones at tone/gap ms, then pause_ms of silence: its audio. */
static size_t render_paced(const char *tones, uint32_t tone, uint32_t gap, uint32_t pause)
{
    ut_synth syn;
    ut_synth_init(&syn, tones, TONES_RATE, tone, gap, 0);
    size_t n = ut_synth_read(&syn, paced_audio, sizeof paced_audio / sizeof paced_audio[0]);
    size_t p = (size_t)pause * 16;
    CHECK(n + p < sizeof paced_audio / sizeof paced_audio[0]);
    memset(paced_audio + n, 0, p * sizeof paced_audio[0]);
    return n + p;
}

static void play_part_paced(line_t *t, const char *tones, int air)
{
    size_t n = air ? render_paced(tones, 80, 80, 600) : render_paced(tones, 40, 20, 400);
    play(t, paced_audio, n);
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
    CHECK(!HAS(&L, "say psbt_cancelled", 0));
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

/* Multi-tap of the same key at three paces: every tap counts (2, A, B). */
static void test_multitap_same_key_paces(void)
{
    static const uint32_t pace[3][2] = {{100, 100}, {80, 140}, {100, 150}};
    air_seed_to_pin(&L, seed_6a, "ABC123DEF456");
    run_quiet(&L);
    size_t mark = L.log_len;
    for (int p = 0; p < 3; p++) {
        for (int i = 0; i < 3; i++)
            press_paced(&L, '2', pace[p][0], pace[p][1]);
        CHECK(L.v.mt.has_pending && L.v.mt.pending == 'B'); /* 2, A, B */
        run_ms(&L, 1600); /* committed */
    }
    CHECK_EQ_INT(count_lines(&L, "say pin-char", mark), 9);
    CHECK_EQ_INT(L.v.mt.len, 3);
    CHECK(L.v.mt.len == 3 && memcmp(L.v.mt.text, "BBB", 3) == 0);
}

/* 3 in the menu asks for a hook flash; no key wipes; * goes back. */
static void test_wipe_needs_hook(void)
{
    to_ready(&L, 0);
    size_t mark = L.log_len;
    press(&L, '3');
    run_quiet(&L);
    CHECK(HAS(&L, "state wipe-confirm", mark));
    CHECK(HAS(&L, "say wipe_hook", mark));
    type_keys(&L, "1234567890");
    press(&L, '3');
    run_quiet(&L);
    CHECK(!HAS(&L, "say wiped", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    press(&L, '*');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-ready", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    /* a flash in the menu is no wipe either */
    on_hook(&L, 500);
    run_quiet(&L);
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    /* 3, then a flash: wiped */
    mark = L.log_len;
    press(&L, '3');
    run_quiet(&L);
    on_hook(&L, 500);
    run_quiet(&L);
    long w = find_line(&L, "say wiped", mark);
    CHECK(w >= 0 && HAS(&L, "state menu-empty", (size_t)w));
    CHECK_EQ_INT(L.v.s.state, 0);
    CHECK(all_zero(&L.v.s, sizeof L.v.s));
}

/* A wallet looping slowly (200 ms tones, 150 ms silences, 600 ms between
 * parts: legal, and slow enough to look like typing) in the menu with a
 * seed: whatever keys it presses, it never wipes. */
static void test_slow_wallet_never_wipes(void)
{
    const psbt_vector *pv = vector("one input with change");
    int32_t n = ut_psbt_to_cbor(pv->psbt, pv->psbt_len, psbt_cbor, sizeof psbt_cbor);
    CHECK(ut_fountain_encoder_init(&psbt_enc, psbt_cbor, (size_t)n, 100) >= 0);
    to_ready(&L, 0);
    for (int i = 0; i < 2; i++) {
        CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
        play(&L, paced_audio, render_paced(part_tones, 200, 150, 600));
    }
    run_quiet(&L);
    CHECK(!HAS(&L, "say wiped", 0));
    CHECK(L.v.s.state >= DS_S_READY);
    CHECK_EQ_INT(L.v.s.w.wallet_fp, 0x73c5da0au);
    CHECK(strstr(L.log, "signed-psbt") == NULL);
}

/* The same slow wallet (1200 ms between parts) looping through a review:
 * the confirmation is never reached unless every item was said in full,
 * and nothing is signed. */
static void test_slow_wallet_review_needs_every_item(void)
{
    to_ready(&L, 0);
    send_psbt(&L, vector("one input with change"));
    CHECK(HAS(&L, "state review", 0));
    size_t scan = L.log_len;
    for (int i = 0; i < 2; i++) {
        CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
        size_t n = render_paced(part_tones, 200, 150, 1200);
        for (size_t at = 0; at < n; at += CHUNK) {
            step(&L, paced_audio + at);
            long c = find_line(&L, "state confirm", scan);
            if (c >= 0) {
                CHECK(L.v.items_heard >= L.v.s.r.n_out + 1);
                scan = (size_t)c + 14;
            }
            if (L.log_len > scan + 4096)
                scan = L.log_len - 4096 > scan ? L.log_len - 4096 : scan;
        }
    }
    CHECK(strstr(L.log, "signed-psbt") == NULL);
    CHECK(!HAS(&L, "say wiped", 0));
    CHECK_EQ_INT(L.v.s.w.wallet_fp, 0x73c5da0au);
}

/* A slow wallet looping near the handset after a hang-up (at "resume_ask")
 * and at a seed's fingerprint: whatever keys it presses, it never wipes. */
static void test_slow_wallet_never_wipes_resume_or_fingerprint(void)
{
    const psbt_vector *pv = vector("one input with change");
    int32_t n = ut_psbt_to_cbor(pv->psbt, pv->psbt_len, psbt_cbor, sizeof psbt_cbor);
    CHECK(ut_fountain_encoder_init(&psbt_enc, psbt_cbor, (size_t)n, 100) >= 0);
    to_ready(&L, 0);
    hook(&L, 0);
    run_ms(&L, 2000);
    hook(&L, 1);
    run_ms(&L, 100);
    CHECK(HAS(&L, "state resume-ask", 0));
    for (int i = 0; i < 2; i++) {
        CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
        play(&L, paced_audio, render_paced(part_tones, 200, 150, 600));
    }
    run_quiet(&L);
    CHECK(!HAS(&L, "say wiped", 0));
    CHECK(L.v.s.state >= DS_S_READY);
    CHECK_EQ_INT(L.v.s.w.wallet_fp, 0x73c5da0au);
    /* at the fingerprint */
    keypad_seed(&L, NULL);
    run_quiet(&L);
    CHECK(HAS(&L, "state fingerprint", 0));
    size_t mark = L.log_len;
    for (int i = 0; i < 2; i++) {
        CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
        play(&L, paced_audio, render_paced(part_tones, 200, 150, 600));
    }
    run_quiet(&L);
    CHECK(!HAS(&L, "say wiped", mark));
    CHECK(L.v.s.state >= DS_S_SEED);
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
    CHECK(!HAS(&L, "say psbt_cancelled", 0));
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
    CHECK(run_until(&L, "say sending", mark, 15000));
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
    static const char *const want[] = {"ear notice", "say warn_inputs_unsigned", "num 1", NULL};
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
    static const char *const want[] = {"ear notice", "say warn_input_no_witness", NULL};
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
    static const char *const out1[] = {"say sending", "ear data", "num 60000", "say sats",
                                       "say address_starts", "ear data", "chars bc1qk",
                                       "say address_ends", "ear data", "chars pe9ug", NULL};
    CHECK(lines_in_a_row(&L, mark, out1));
    CHECK(!HAS(&L, "say change", mark));
    press(&L, '#');
    run_quiet(&L);
    static const char *const out2[] = {"say change", "ear data", "num 39000", "say sats", NULL};
    CHECK(lines_in_a_row(&L, mark, out2));
    CHECK(!HAS(&L, "say fee", mark));
    press(&L, '#');
    run_quiet(&L);
    static const char *const fee[] = {"say fee", "ear data", "num 1000", "say sats", NULL};
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

/* The longest address there is (P2TR, 62 characters, main and test) is
 * said whole, with its lead-in and `ear data` right before the first
 * character: the lead-in is queued apart so the address fits the queue. */
static void whole_p2tr(int testnet, const char *vec, const char *addr)
{
    char chars[96];
    to_ready(&L, testnet);
    send_psbt(&L, vector(vec));
    run_quiet(&L);
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
    size_t mark = L.log_len;
    press(&L, '5');
    run_quiet(&L);
    snprintf(chars, sizeof chars, "chars %s", addr);
    CHECK(strlen(addr) == 62);
    const char *const whole[] = {"say address_full_groups", "ear data", chars, NULL};
    CHECK(lines_in_a_row(&L, mark, whole));
    CHECK(!HAS(&L, "say error_generic", mark));
    CHECK(!HAS(&L, "ear fail", mark));
}

static void test_whole_address_p2tr(void)
{
    whole_p2tr(0, "outputs to P2TR, P2SH and OP_RETURN",
               "bc1pv7fc2z39g8vyxq95z4xhzvzgj3hhmgdk6qeq0997rvg7e28rc5qsgny8xu");
    whole_p2tr(1, "testnet output to P2TR",
               "tb1pv7fc2z39g8vyxq95z4xhzvzgj3hhmgdk6qeq0997rvg7e28rc5qslmjgun");
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
    CHECK(HAS(&L, "say settings", mark));
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
    /* after the settings are said: a key in the middle of a prompt is the
     * barge-in tests' matter, and where it lands in the speech decides
     * whether it is heard (test_key_during_prompt_barges_in) */
    run_quiet(&L);
    press(&L, '*');
    run_ms(&L, 100);
    CHECK(find_line(&L, "state menu-ready", mark) > find_line(&L, "state about", mark));
    /* it lasts until the power is cut, a wipe included */
    run_quiet(&L);
    press(&L, '3');
    run_quiet(&L);
    on_hook(&L, 500);
    run_quiet(&L);
    CHECK(HAS(&L, "say wiped", mark));
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
    CHECK(HAS(&L, "say signed", mark));
    CHECK(HAS(&L, "say hold_near", mark));
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
    /* # back to the menu; the signature zeroed. The redial note was said
     * once in this call, when the seed had been typed: not again */
    m2 = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-ready", m2));
    CHECK(!HAS(&L, "say redial_note", m2));
    CHECK_EQ_INT(count_lines(&L, "say redial_note", 0), 1);
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
    /* 30 ms, 100 ms: nothing */
    size_t mark = L.log_len;
    on_hook(&L, 30);
    run_quiet(&L);
    on_hook(&L, 100);
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
    /* # resumes at the menu: the review is gone, 1 to play the PSBT again */
    run_quiet(&L);
    size_t m2 = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-ready", m2));
    CHECK(!HAS(&L, "state psbt-listen", m2));
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
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

/* A reception started by mistake can only be left by hanging up: resuming
 * goes back to the menu, never into the reception again. */
static void test_hang_up_during_reception_back_to_menu(void)
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
    size_t m2 = L.log_len;
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-ready", m2));
    CHECK(!HAS(&L, "state psbt-listen", m2));
    CHECK(all_zero(&L.v.rx, sizeof L.v.rx));
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    CHECK(strstr(L.log + mark, "signed-psbt") == NULL);
    press(&L, '1'); /* to sign, the PSBT from the start */
    run_quiet(&L);
    CHECK(HAS(&L, "state psbt-listen", m2));
    CHECK(HAS(&L, "say psbt_listening", m2));
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
    CHECK(HAS(&L, "say account_ready", mark));
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
    CHECK(HAS(&L, "say account_ready", m2));
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

/* The port may call ds_voice_hook only at each change, with no tick while
 * the handset is down: the on-hook is still measured from its edge, so a
 * long one is a hang-up and a short one a flash. */
static void test_hook_seen_at_its_edges_only(void)
{
    to_ready(&L, 0);
    send_psbt(&L, vector("one input with change"));
    review_to_confirm(&L);
    size_t mark = L.log_len;
    uint32_t t0 = L.now;
    ds_voice_hook(&L.v, 0, t0);
    ds_voice_hook(&L.v, 1, t0 + 1600); /* no call, no tick in between */
    L.now = t0 + 1600;
    take_echo(&L);
    run_ms(&L, 300);
    CHECK(!HAS(&L, "hook-flash", mark));
    CHECK(HAS(&L, "state idle", mark));
    CHECK(HAS(&L, "state resume-ask", mark));
    CHECK(strstr(L.log + mark, "signed-psbt") == NULL);
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    /* and a short one, the same way, is a flash: it signs */
    run_quiet(&L);
    press(&L, '#');
    run_quiet(&L);
    send_psbt(&L, vector("one input with change"));
    review_to_confirm(&L);
    mark = L.log_len;
    t0 = L.now;
    ds_voice_hook(&L.v, 0, t0);
    ds_voice_hook(&L.v, 1, t0 + 500);
    L.now = t0 + 500;
    take_echo(&L);
    run_ms(&L, 300);
    run_quiet(&L);
    CHECK(HAS(&L, "hook-flash", mark));
    CHECK(strstr(L.log + mark, "signed-psbt-begin") != NULL);
}

/* Up to the confirmation step, without waiting for its prompt. */
static void review_to_confirm_now(line_t *t)
{
    size_t mark = t->log_len;
    for (int i = 0; i < 40 && !HAS(t, "state confirm", mark); i++) {
        run_quiet(t);
        press(t, '#');
    }
    CHECK(HAS(t, "state confirm", mark));
    CHECK(ds_voice_speaking(&t->v)); /* "to sign, hang up..." still being said */
}

/* A flash counts only once the step's prompt has been said in full: begun
 * while "to sign..." or "to wipe..." is still being said, it does nothing. */
static void test_flash_during_prompt_ignored(void)
{
    to_ready(&L, 0);
    send_psbt(&L, vector("one input with change"));
    review_to_confirm_now(&L);
    size_t mark = L.log_len;
    on_hook(&L, 500);
    CHECK(HAS(&L, "hook-flash", mark));
    run_quiet(&L);
    CHECK(strstr(L.log + mark, "signed-psbt") == NULL);
    CHECK_EQ_INT(L.v.s.state, DS_S_REVIEW);
    CHECK(!HAS(&L, "state hold-near", mark));
    /* once it has been said, a flash signs */
    mark = L.log_len;
    on_hook(&L, 500);
    run_quiet(&L);
    CHECK(strstr(L.log + mark, "signed-psbt-begin") != NULL);
    press(&L, '*');
    run_quiet(&L);
    /* the wipe the same way */
    mark = L.log_len;
    press(&L, '3');
    CHECK(HAS(&L, "state wipe-confirm", mark));
    CHECK(ds_voice_speaking(&L.v));
    on_hook(&L, 500);
    run_quiet(&L);
    CHECK(!HAS(&L, "say wiped", mark));
    CHECK_EQ_INT(L.v.s.state, DS_S_READY);
    on_hook(&L, 500);
    run_quiet(&L);
    CHECK(HAS(&L, "say wiped", mark));
    CHECK_EQ_INT(L.v.s.state, 0);
}

/* A review item that cannot be queued whole (here a pack whose grammar
 * cannot say the amount) is not said in part: the generic error, and the
 * item is not heard, so # does not move on. */
static void test_review_fails_closed(void)
{
    to_ready(&L, 0);
    uint8_t nscales = L.v.packs.pack[0].nscales;
    L.v.packs.pack[0].nscales = 0; /* 60000 cannot be said now */
    size_t mark = L.log_len;
    send_psbt(&L, vector("one input with change"));
    run_quiet(&L);
    CHECK(HAS(&L, "state review", mark));
    long e = find_line(&L, "say error_generic", mark);
    CHECK(e >= 0);
    CHECK(!HAS(&L, "chars pe9ug", mark));
    CHECK(!L.v.item_heard);
    press(&L, '#');
    run_quiet(&L);
    CHECK_EQ_INT(L.v.item, 0);
    CHECK(!HAS(&L, "say change", mark));
    /* said whole again: # moves on */
    L.v.packs.pack[0].nscales = nscales;
    press(&L, '0');
    run_quiet(&L);
    CHECK(HAS(&L, "chars pe9ug", mark));
    press(&L, '#');
    run_quiet(&L);
    CHECK_EQ_INT(L.v.item, 1);
    CHECK(HAS(&L, "say change", mark));
    CHECK(strstr(L.log + mark, "signed-psbt") == NULL);
}

/* The review names its keys after its first item, once, and as the help
 * (their longer version) after DS_VOICE_HELP_MS of silence, once per item. */
static void test_review_names_keys(void)
{
    to_ready(&L, 0);
    size_t mark = L.log_len;
    send_psbt(&L, vector("one input with change"));
    run_quiet(&L);
    static const char *const first[] = {"chars pe9ug", "say review_keys", NULL};
    CHECK(lines_in_a_row(&L, mark, first));
    CHECK_EQ_INT(count_lines(&L, "say review_keys", mark), 1);
    press(&L, '#');
    run_quiet(&L);
    CHECK(HAS(&L, "say change", mark));
    CHECK_EQ_INT(count_lines(&L, "say review_keys", mark), 1);
    size_t m2 = L.log_len;
    run_ms(&L, DS_VOICE_HELP_MS + 500);
    run_quiet(&L);
    CHECK_EQ_INT(count_lines(&L, "say review_keys_long", m2), 1);
    CHECK(!HAS(&L, "say change", m2)); /* the hint, not the item again */
    CHECK_EQ_INT(L.v.item, 1);
    size_t m3 = L.log_len;
    run_ms(&L, 3 * DS_VOICE_HELP_MS);
    CHECK(L.log_len == m3); /* once */
    press(&L, '0');
    run_quiet(&L);
    CHECK(HAS(&L, "say change", m2));
}

/* ---- the help that grows longer by itself ---- */

/* Until the line `want` is in the log at or after from, at most ms. */
static int run_until_line(line_t *t, const char *want, size_t from, uint32_t ms)
{
    for (uint32_t i = 0; i < ms / 10 && !HAS(t, want, from); i++)
        step(t, NULL);
    return HAS(t, want, from);
}

/* After the short prompt ends, nothing for 7.9 s; at 8.0 s the longer
 * version, once; then a minute of silence. */
static void test_help_once_after_8s(void)
{
    to_menu(&L);
    CHECK(!ds_voice_speaking(&L.v));
    size_t mark = L.log_len;
    run_ms(&L, DS_VOICE_HELP_MS - 100);
    CHECK(strstr(L.log + mark, "say ") == NULL);
    CHECK(!ds_voice_speaking(&L.v));
    run_ms(&L, 100);
    CHECK(HAS(&L, "say choose_option_long", mark));
    CHECK(!HAS(&L, "say choose_option", mark)); /* in place of the short one */
    run_quiet(&L);
    CHECK(HAS(&L, "say opt_about_long", mark)); /* the rest of the prompt too */
    CHECK(HAS(&L, "say opt_net_test_long", mark));
    CHECK(strstr(L.log + mark, "state ") == NULL);
    run_quiet(&L);
    mark = L.log_len;
    run_ms(&L, 60000);
    CHECK(L.log_len == mark); /* nothing said, nothing at all */
    CHECK(!ds_voice_speaking(&L.v));
}

/* 0 says what was last heard: the short version before the help, the
 * longer one after it. */
static void test_zero_repeats_last_heard(void)
{
    to_menu(&L);
    size_t mark = L.log_len;
    press(&L, '0');
    run_quiet(&L);
    CHECK(HAS(&L, "say choose_option", mark));
    CHECK(!HAS(&L, "say choose_option_long", mark));
    /* the 0 restarted the wait: the help comes 8 s after it */
    mark = L.log_len;
    CHECK(run_until_line(&L, "say choose_option_long", mark, DS_VOICE_HELP_MS + 200));
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '0');
    run_quiet(&L);
    CHECK(HAS(&L, "say choose_option_long", mark));
    CHECK(!HAS(&L, "say choose_option", mark));
    /* the help was given in this state: no second one */
    mark = L.log_len;
    run_ms(&L, 2 * DS_VOICE_HELP_MS);
    CHECK(L.log_len == mark);
    /* a new state, a new request: short again, its own help */
    press(&L, '1');
    run_quiet(&L);
    CHECK(HAS(&L, "say seed_how", mark));
    CHECK(!HAS(&L, "say seed_how_long", mark));
    CHECK(run_until_line(&L, "say seed_how_long", mark, DS_VOICE_HELP_MS + 200));
}

/* A state without a longer version says its prompt once more as the help. */
static void test_help_without_long_says_short(void)
{
    to_seed_how(&L);
    press(&L, '2'); /* the keypad: "*", the digits, "#" */
    run_quiet(&L);
    press(&L, '*');
    run_quiet(&L);
    size_t mark = L.log_len;
    /* an input: no help (keys there are data) */
    run_ms(&L, DS_VOICE_HELP_MS + 500);
    CHECK(strstr(L.log + mark, "say ") == NULL);
    /* About has no longer version: its prompt again, once */
    to_menu(&L);
    press(&L, '9');
    run_quiet(&L);
    mark = L.log_len;
    run_ms(&L, DS_VOICE_HELP_MS + 200);
    CHECK(L.log_len > mark); /* begun */
    run_quiet(&L);
    CHECK(HAS(&L, "chars en", mark)); /* the whole of it, again */
    mark = L.log_len;
    run_ms(&L, 30000);
    CHECK(L.log_len == mark);
}

/* A key, step by step (100 ms tone, 200 ms gap): the output length when it
 * was taken (counted), or 0 if it never was. */
static size_t press_taken_at(line_t *t, char k)
{
    static int16_t buf[4800];
    char keys[2] = {k, 0};
    size_t at = 0;
    uint32_t n = ds_voice_keys(&t->v);
    ut_synth s;
    memset(buf, 0, sizeof buf);
    ut_synth_init(&s, keys, 16000, 100, 200, 0);
    ut_synth_read(&s, buf, 4800);
    for (int i = 0; i < 30; i++) {
        step(t, buf + i * CHUNK);
        if (at == 0 && ds_voice_keys(&t->v) != n)
            at = t->out_len;
    }
    return at;
}

/* A key the state acts on, pressed during a motif or the longer version,
 * stops it at once (in the 10 ms it is taken) and acts. */
static void test_key_cuts_motif_and_long(void)
{
    static int16_t clipbuf[16000 * 10];
    ds_pack en;
    CHECK_EQ_INT(ds_pack_open(&en, ds_pack_en, ds_pack_en_len), DS_OK);
    size_t lwipe = render_clip(&en, DS_P_WIPE_HOOK, clipbuf, sizeof clipbuf / sizeof clipbuf[0]);
    size_t llong = render_clip(&en, DS_P_CHOOSE_OPTION_LONG, clipbuf, sizeof clipbuf / sizeof clipbuf[0]);
    const uint16_t *hz, *ms;
    uint8_t n;
    uint32_t notice = 0;
    CHECK_EQ_INT(ds_say_motif(DS_EAR_NOTICE, &hz, &ms, &n), DS_OK);
    for (uint8_t i = 0; i < n; i++)
        notice += ms[i];

    /* how long the seed's question is, said alone */
    to_menu(&L);
    size_t at = press_taken_at(&L, '1');
    CHECK(at > 0);
    run_quiet(&L);
    size_t lseed = L.out_len - at;

    /* the longer version, cut by 1 */
    to_menu(&L);
    size_t mark = L.log_len;
    CHECK(run_until_line(&L, "say choose_option_long", mark, DS_VOICE_HELP_MS + 200));
    size_t from = L.out_len;
    run_ms(&L, 300);
    mark = L.log_len;
    at = press_taken_at(&L, '1');
    CHECK(at > 0);
    CHECK(at - from < (size_t)notice * 16 + llong); /* taken while it was still being said */
    CHECK(HAS(&L, "state seed-how", mark));
    run_quiet(&L);
    size_t rest = L.out_len - at;
    CHECK(rest <= lseed + CHUNK); /* only the new prompt: nothing of the longer one */
    CHECK(!HAS(&L, "say opt_load_seed_long", mark));

    /* the "ok" motif after the seed is loaded, cut by * (ask to wipe) */
    uint32_t motif = 0;
    CHECK_EQ_INT(ds_say_motif(DS_EAR_OK, &hz, &ms, &n), DS_OK);
    for (uint8_t i = 0; i < n; i++)
        motif += ms[i];
    CHECK(tones_seed_keypad(zero16, 16, NULL, seed_tones, sizeof seed_tones) > 0);
    to_seed_how(&L);
    press(&L, '2');
    run_ms(&L, 500);
    type_keys(&L, seed_tones);
    run_ms(&L, 300);
    run_quiet(&L);
    mark = L.log_len;
    press_paced(&L, '#', 100, 0); /* no PIN */
    CHECK(run_until_line(&L, "ear ok", mark, 1000));
    from = L.out_len;
    run_ms(&L, 50); /* the listener starts afresh with the seed loaded */
    at = press_taken_at(&L, '*');
    CHECK(at > 0);
    CHECK(at - from < (size_t)motif * 16); /* taken during the motif */
    CHECK(HAS(&L, "state wipe-confirm", mark));
    run_quiet(&L);
    rest = L.out_len - at;
    CHECK(rest <= lwipe + CHUNK); /* the motif stopped, the fingerprint never came */
    CHECK(!HAS(&L, "say fingerprint_is", mark));
}

/* ---- barge-in: a key pressed while the signer speaks ---- */

/* Off-hook until the menu is being said: in English (the built-in voice)
 * or, with es, in Spanish chosen from the list. */
static void to_menu_speaking(line_t *t, int es)
{
    if (!es) {
        power_up(t, NULL, 0);
        hook(t, 1);
    } else {
        power_up(t, es_bin, ES_BIN_LEN);
        hook(t, 1);
        run_ms(t, 100);
        press(t, '2'); /* Spanish, barging into the list */
    }
    CHECK(run_until_line(t, "state menu-empty", 0, 2000));
    CHECK(ds_voice_speaking(&t->v));
}

/* The line as it is now, to try one press after another from the same point. */
static struct {
    ds_voice v;
    uint32_t now;
    size_t log_len, out_len;
} snap;

static void save_line(const line_t *t)
{
    memcpy(&snap.v, &t->v, sizeof snap.v);
    snap.now = t->now;
    snap.log_len = t->log_len;
    snap.out_len = t->out_len;
}

static void restore_line(line_t *t)
{
    memcpy(&t->v, &snap.v, sizeof t->v);
    t->now = snap.now;
    t->log_len = snap.log_len;
    t->log[t->log_len] = '\0';
    t->out_len = snap.out_len;
}

/* What a few tones did to the line: keys counted, slices the detector
 * heard the first tone as itself, whether data was seen, whether the
 * signer fell quiet. */
typedef struct {
    uint32_t keys, heard;
    int data, quiet;
} tried_t;

/* tones (tone_ms each, gap_ms apart) begun pad_ms into the next 10 ms,
 * then silence, 600 ms in all. */
static tried_t try_tones(line_t *t, const char *tones, uint32_t tone_ms, uint32_t gap_ms,
                         unsigned pad_ms)
{
    static int16_t buf[16000];
    tried_t r = {0, 0, 0, 0};
    uint32_t k = ds_voice_keys(&t->v);
    ut_synth s;
    memset(buf, 0, sizeof buf);
    ut_synth_init(&s, tones, 16000, tone_ms, gap_ms, 0);
    ut_synth_read(&s, buf + pad_ms * 16, 8000);
    for (int i = 0; i < 60; i++) {
        step(t, buf + i * CHUNK);
        r.heard += t->v.l.keys.live_key == tones[0];
        r.data |= t->v.data;
        if (i < 10) /* within 100 ms of the first tone */
            r.quiet |= !ds_voice_speaking(&t->v);
    }
    r.keys = ds_voice_keys(&t->v) - k;
    return r;
}

/* Every key, begun at every 10 ms of the greeting and the menu (and at
 * every millisecond within them, in turn), in both voices. A key mixed
 * with speech can read for 20 ms as a key sharing one of its frequencies;
 * with the real key after it, that is two tones begun within 250 ms, data,
 * and the key is lost; or the speech covers it below the 70 ms a key
 * needs. Without the barge-in mute: 124 of 26592 lost (39 as data), and 18
 * of 6648 held 150 ms. Stopping the speech at the onset leaves 42 (16 as
 * data) and 6. The rest is the price of the data rule, which stays as it
 * is (test_machine_tones_over_speech_are_no_keys): ceilings, not zero. */
static void test_barge_in_loses_few_keys(void)
{
    static const char keys[] = "0123456789*#";
    unsigned presses = 0, lost = 0, data = 0, twice = 0, short_heard = 0, long_presses = 0,
             long_lost = 0;
    for (int es = 0; es < 2; es++) {
        to_menu_speaking(&L, es);
        for (unsigned j = 0; ds_voice_speaking(&L.v); j++) {
            if (L.v.deaf) { /* the key that chose Spanish, still going */
                step(&L, NULL);
                continue;
            }
            save_line(&L);
            for (unsigned i = 0; keys[i] != '\0'; i++) {
                char one[2] = {keys[i], 0};
                unsigned pad = (j * 3 + i * 7) % 10;
                tried_t r = try_tones(&L, one, 100, 500, pad);
                presses++;
                data += r.data != 0;
                twice += r.keys > 1;
                if (r.keys == 0 && r.heard < 7)
                    short_heard++;
                else if (r.keys == 0)
                    lost++;
                restore_line(&L);
                if (j % 4 == 0) {
                    r = try_tones(&L, one, 150, 450, pad);
                    long_presses++;
                    long_lost += r.keys != 1;
                    restore_line(&L);
                }
            }
            step(&L, NULL);
        }
    }
    printf("barge-in: %u keys of 100 ms while speaking: %u lost, %u heard under 70 ms; "
           "%u of 150 ms: %u lost\n",
           presses, lost, short_heard, long_presses, long_lost);
    CHECK(presses > 4000);
    CHECK(data <= 20);
    CHECK(lost + short_heard <= 50); /* under 0.2 % */
    CHECK_EQ_INT(twice, 0);
    CHECK(long_presses > 4000);
    CHECK(long_lost <= 8);
}

/* A tone begun while the signer speaks (heard twice in a row, 20 ms, as
 * the firmware's detector reports it) stops the speech within one 10 ms
 * slice, before the key is known. */
static void test_barge_in_hushes_on_onset(void)
{
    static const char keys[] = "0123456789*#";
    static int16_t buf[16000];
    for (int es = 0; es < 2; es++) {
        to_menu_speaking(&L, es);
        run_ms(&L, 400);
        CHECK(ds_voice_speaking(&L.v));
        save_line(&L);
        for (unsigned i = 0; keys[i] != '\0'; i++) {
            char one[2] = {keys[i], 0};
            ut_synth s;
            memset(buf, 0, sizeof buf);
            ut_synth_init(&s, one, 16000, 100, 200, 0);
            ut_synth_read(&s, buf, 4800);
            int onset = -1, quiet = -1;
            for (int k = 0; k < 30; k++) {
                step(&L, buf + k * CHUNK);
                if (onset < 0 && L.v.l.keys.live_key != 0 && L.v.l.keys.live_run >= 2)
                    onset = k;
                if (quiet < 0 && !ds_say_busy(&L.v.say) && !ds_voice_speaking(&L.v))
                    quiet = k;
            }
            CHECK(onset >= 0);
            CHECK(quiet >= 0 && quiet <= onset + 1);
            restore_line(&L);
        }
    }
}

/* Machine tones over the signer's voice are no keys, as without it: pairs
 * of different short tones (40 to 60 ms, 20 to 50 ms apart: a wallet's
 * data) begun at every millisecond of a 10 ms slice, over the menu in
 * both voices and over a review item, never make a key. The first stops
 * the speech; if it ends within the settle, its onset still counts, at the
 * hush, and the second is data. */
static void machine_pairs_over(line_t *t, unsigned *trials, unsigned *keyed)
{
    static const char *const pairs[] = {"14", "58", "#0", "*9", "27", "36", "A1", "12", "47", NULL};
    static const unsigned tone_ms[] = {40, 50, 60};
    static const unsigned gap_ms[] = {20, 30, 40, 50};
    save_line(t);
    for (int p = 0; pairs[p] != NULL; p++)
        for (unsigned a = 0; a < 3; a++)
            for (unsigned b = 0; b < 4; b++)
                for (unsigned pad = 0; pad < 10; pad++) {
                    CHECK(ds_voice_speaking(&t->v));
                    size_t mark = t->log_len;
                    tried_t r = try_tones(t, pairs[p], tone_ms[a], gap_ms[b], pad);
                    (*trials)++;
                    if (r.keys != 0) {
                        (*keyed)++;
                        printf("  '%s' %u ms, %u ms apart, +%u ms: a key [%.60s]\n", pairs[p],
                               tone_ms[a], gap_ms[b], pad, t->log + mark);
                    }
                    restore_line(t);
                }
}

static void test_machine_tones_over_speech_are_no_keys(void)
{
    unsigned trials = 0, keyed = 0;
    for (int es = 0; es < 2; es++) {
        to_menu_speaking(&L, es);
        run_ms(&L, 1600); /* past the multi-tap of the key that chose Spanish */
        machine_pairs_over(&L, &trials, &keyed);
    }
    to_ready(&L, 0);
    send_psbt(&L, vector("one input with change"));
    run_ms(&L, 1500); /* the gate after the reception */
    CHECK(!L.v.gate && L.v.s.state == DS_S_REVIEW);
    machine_pairs_over(&L, &trials, &keyed);
    CHECK(trials >= 3000);
    CHECK_EQ_INT(keyed, 0);
}

/* A tone that turns out to be no key (30 ms: begun, never long enough)
 * stops the speech only for a moment: what was being said then goes on
 * from where it stopped, whole, and nothing else happens. */
static void test_barge_in_no_key_lets_it_go_on(void)
{
    static int16_t ref[OUT_MAX / 2];
    static char ref_log[1 << 14];
    for (int es = 0; es < 2; es++) {
        to_menu_speaking(&L, es);
        run_ms(&L, 1600);
        save_line(&L);
        size_t from = L.out_len, mark = L.log_len;
        run_quiet(&L);
        size_t n_ref = L.out_len - from;
        CHECK(n_ref <= sizeof ref / sizeof ref[0]);
        memcpy(ref, L.out + from, n_ref * sizeof ref[0]);
        snprintf(ref_log, sizeof ref_log, "%s", L.log + mark);
        restore_line(&L);
        uint32_t k = ds_voice_keys(&L.v);
        tried_t r = try_tones(&L, "5", 30, 570, 0);
        run_quiet(&L);
        size_t n = L.out_len - from;
        CHECK(r.quiet);
        CHECK_EQ_INT(ds_voice_keys(&L.v), k);
        CHECK(!L.v.data);
        CHECK_EQ_STR(L.log + mark, ref_log); /* the same words, in the same order */
        /* a pause of 50 to 300 ms, the rest sample for sample */
        CHECK(n >= n_ref + 50 * 16 && n <= n_ref + 300 * 16);
        CHECK(n >= n_ref && memcmp(L.out + L.out_len - (n_ref - 16000), ref + 16000,
                                   (n_ref - 16000) * sizeof ref[0]) == 0);
    }
}

/* The confirmation likewise: a tone during "to sign, hang up..." that is
 * no key leaves it said in full, and then a flash signs. */
static void test_barge_in_no_key_keeps_the_confirmation(void)
{
    to_ready(&L, 0);
    send_psbt(&L, vector("one input with change"));
    review_to_confirm_now(&L);
    run_ms(&L, 300);
    CHECK(ds_voice_speaking(&L.v));
    tried_t r = try_tones(&L, "7", 30, 570, 0);
    CHECK(r.quiet);
    CHECK_EQ_INT(r.keys, 0);
    run_quiet(&L);
    size_t mark = L.log_len;
    on_hook(&L, 500);
    run_quiet(&L);
    CHECK(HAS(&L, "state hold-near", mark));
    CHECK(HAS(&L, "say signed", mark));
}

/* The site's simulator feeds the line in blocks of 16 ms, not 10, and
 * there the signer's own voice can read as a tone's onset for a moment (as
 * a few clips can at some alignments with the detector): it pauses, and
 * goes on. The list, the greeting, the menu and About, in either voice,
 * are said to their end. */
static void test_own_voice_said_whole_in_16ms_blocks(void)
{
    enum { B = 256 };
    for (int es = 0; es < 2; es++) {
        int16_t o[B], kb[B];
        static int16_t keybuf[16000];
        power_up(&L, es_bin, ES_BIN_LEN);
        hook(&L, 1);
        const char *keys = es ? "29" : "19";
        size_t mark = 0;
        for (int i = 0; i < 2; i++) {
            ut_synth s;
            char one[2] = {keys[i], 0};
            memset(keybuf, 0, sizeof keybuf);
            ut_synth_init(&s, one, 16000, 100, 200, 0);
            ut_synth_read(&s, keybuf, 4800);
            mark = L.log_len;
            /* 300 ms into what it says, the key; then until it is quiet */
            for (uint32_t ms = 0, at = 0; ms < 120000; ms += B / 16) {
                ds_voice_line_out(&L.v, o, B);
                memset(kb, 0, sizeof kb);
                if (ms >= 300 && at < 4800) {
                    memcpy(kb, keybuf + at, sizeof kb);
                    at += B;
                }
                for (int k = 0; k < B; k++) {
                    int32_t x = o[k] + kb[k];
                    o[k] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
                }
                ds_voice_line_in(&L.v, o, B);
                L.now += B / 16;
                ds_voice_tick(&L.v, L.now);
                take_echo(&L);
                if (at >= 4800 && !ds_voice_speaking(&L.v) && !L.v.muted)
                    break; /* quiet, and not just paused */
            }
        }
        CHECK(HAS(&L, "state about", mark));
        CHECK(HAS(&L, "say opt_back", mark)); /* About said to its end */
    }
}

/* After a hush the data rule is whole: two different tones 60 ms apart
 * (the first one stopping the speech) are data, never a key; what was
 * being said then goes on, as it did over data before. */
static void test_data_rule_still_holds_after_hush(void)
{
    static const char *const pairs[] = {"14", "58", "#0", "*9", "27", "36", "A1", NULL};
    for (int es = 0; es < 2; es++) {
        to_menu_speaking(&L, es);
        run_ms(&L, 1600); /* past the multi-tap of the key that chose Spanish */
        save_line(&L);
        for (int p = 0; pairs[p] != NULL; p++) {
            for (unsigned pad = 0; pad < 10; pad += 3) {
                size_t mark = L.log_len;
                CHECK(ds_voice_speaking(&L.v));
                tried_t r = try_tones(&L, pairs[p], 100, 60, pad);
                CHECK_EQ_INT(r.keys, 0);
                CHECK(r.data);
                CHECK(strstr(L.log + mark, "state ") == NULL);
                CHECK(r.quiet);                 /* hushed */
                CHECK(ds_voice_speaking(&L.v)); /* and back */
                restore_line(&L);
            }
        }
    }
}

/* While a seed or a PSBT is being received, no help: the reception has its
 * own waits. */
static void test_no_help_while_receiving(void)
{
    to_seed_how(&L);
    press(&L, '1');
    run_quiet(&L);
    CHECK(HAS(&L, "state seed-air", 0));
    size_t mark = L.log_len;
    run_ms(&L, DS_VOICE_HELP_MS + 2000);
    CHECK(L.log_len == mark);
    to_ready(&L, 0);
    press(&L, '1');
    run_quiet(&L);
    CHECK(HAS(&L, "state psbt-listen", 0));
    mark = L.log_len;
    run_ms(&L, DS_VOICE_HELP_MS + 2000);
    CHECK(L.log_len == mark);
}

/* By the keypad: the speakerphone warning the first time (once per
 * power-up), and the redial note after the fingerprint, once the seed and
 * its PIN were typed (once per call). */
static void test_keypad_path_warns(void)
{
    keypad_seed(&L, NULL);
    long k = find_line(&L, "state seed-keypad", 0);
    long w = find_line(&L, "say pin_warning", 0);
    CHECK(k >= 0 && w > k);
    CHECK(w >= 0 && find_line(&L, "say keypad_ask", (size_t)w) >= 0);
    run_quiet(&L);
    long f = find_line(&L, "say if_correct_hash", 0);
    long r = find_line(&L, "say redial_note", 0);
    CHECK(f >= 0 && r > f);
    CHECK_EQ_INT(count_lines(&L, "say redial_note", 0), 1);
    press(&L, '0'); /* the fingerprint again: not the note */
    run_quiet(&L);
    CHECK_EQ_INT(count_lines(&L, "say redial_note", 0), 1);
    /* wiped and typed again in the same power-up and call: neither again */
    press(&L, '*');
    run_quiet(&L);
    on_hook(&L, 500);
    run_quiet(&L);
    CHECK_EQ_INT(L.v.s.state, 0);
    size_t mark = L.log_len;
    press(&L, '1');
    run_quiet(&L);
    press(&L, '2');
    run_ms(&L, 500);
    type_keys(&L, seed_tones);
    run_ms(&L, 300);
    run_quiet(&L);
    press(&L, '#');
    run_ms(&L, 500);
    run_quiet(&L);
    CHECK(HAS(&L, "state fingerprint", mark));
    CHECK(!HAS(&L, "say pin_warning", mark));
    CHECK(!HAS(&L, "say redial_note", mark));
    /* a new call: the redial note again, the warning not */
    hook(&L, 0);
    run_ms(&L, 2000);
    size_t call = L.log_len;
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    press(&L, '#'); /* resume: the fingerprint */
    run_quiet(&L);
    press(&L, '*');
    run_quiet(&L);
    on_hook(&L, 500);
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '1');
    run_quiet(&L);
    press(&L, '2');
    run_ms(&L, 500);
    type_keys(&L, seed_tones);
    run_ms(&L, 300);
    run_quiet(&L);
    press(&L, '#');
    run_ms(&L, 500);
    run_quiet(&L);
    CHECK_EQ_INT(count_lines(&L, "say redial_note", call), 1);
    CHECK(!HAS(&L, "say pin_warning", call));
    CHECK(HAS(&L, "state fingerprint", mark));
}

/* ---- the script: where you are, then what you can do ---- */

/* Off-hook: the name, then the menu at once (no question, no # to go on). */
static void test_boot_has_no_flashed_question(void)
{
    power_up(&L, NULL, 0);
    hook(&L, 1);
    run_ms(&L, 100);
    run_quiet(&L);
    CHECK(!HAS(&L, "say flashed", 0));
    static const char *const boot[] = {"say greeting", "state menu-empty", "ear notice",
                                       "say choose_option", NULL};
    CHECK(lines_in_order(&L, 0, boot));
    CHECK_EQ_INT(ds_voice_keys(&L.v), 0); /* no key was needed */
}

/* A menu: the notice motif, "choose an option", then each option after the
 * option motif, as "number, action". */
static void test_menu_says_choose_then_options_with_motif(void)
{
    to_menu(&L);
    static const char *const empty[] = {"state menu-empty", "ear notice", "say choose_option",
                                        "ear option", "say opt_load_seed",
                                        "ear option", "say opt_net_test",
                                        "ear option", "say opt_about", NULL};
    CHECK(lines_in_a_row(&L, 0, empty));
    to_ready(&L, 0);
    static const char *const ready[] = {"state menu-ready", "ear notice", "say choose_option",
                                        "ear option", "say opt_sign",
                                        "ear option", "say opt_account",
                                        "ear option", "say opt_wipe",
                                        "ear option", "say opt_settings", NULL};
    CHECK(lines_in_a_row(&L, 0, ready));
    /* on testnet, the option is the way back */
    to_menu(&L);
    size_t mark = L.log_len;
    press(&L, '2');
    run_quiet(&L);
    press(&L, '0');
    run_quiet(&L);
    CHECK(HAS(&L, "say opt_net_main", mark));
}

/* The network is said when it matters: the seed loaded, the review begun,
 * the account sent; never in a menu. */
static void test_network_said_when_it_matters(void)
{
    to_menu(&L);
    CHECK(!HAS(&L, "say net_main_say", 0));
    CHECK(!HAS(&L, "say net_test_say", 0));
    /* the seed, on testnet */
    to_ready(&L, 1);
    static const char *const loaded[] = {"ear ok", "say seed_loaded", "say net_test_say",
                                         "state fingerprint", NULL};
    CHECK(lines_in_order(&L, 0, loaded));
    long m = find_line(&L, "state menu-ready", 0);
    CHECK(m >= 0 && !HAS(&L, "say net_test_say", (size_t)m));
    CHECK(!HAS(&L, "say net_main_say", 0));
    /* the review, on mainnet */
    to_ready(&L, 0);
    size_t mark = L.log_len;
    send_psbt(&L, vector("one input with change"));
    run_quiet(&L);
    static const char *const rv[] = {"ear ok", "say review_start", "say net_main_say",
                                     "say review_hear_all", "state review", NULL};
    CHECK(lines_in_a_row(&L, mark, rv));
    CHECK(HAS(&L, "say sending", mark));
    press(&L, '*');
    run_quiet(&L);
    /* the account */
    mark = L.log_len;
    press(&L, '2');
    run_quiet(&L);
    static const char *const acc[] = {"state hold-near", "say account_ready", "say net_main_say",
                                      "say hold_near", NULL};
    CHECK(lines_in_order(&L, mark, acc));
    /* the menu never says it */
    mark = L.log_len;
    press(&L, '*');
    run_quiet(&L);
    CHECK(HAS(&L, "state menu-ready", mark));
    CHECK(!HAS(&L, "say net_main_say", mark));
}

/* An address said by its ends is named as such; whole (5), in groups. */
static void test_address_parts_named(void)
{
    to_ready(&L, 0);
    size_t mark = L.log_len;
    send_psbt(&L, vector("one input with change"));
    run_quiet(&L);
    static const char *const ends[] = {"say sending", "ear data", "num 60000", "say sats",
                                       "say address_starts", "ear data", "chars bc1qk",
                                       "say address_ends", "ear data", "chars pe9ug", NULL};
    CHECK(lines_in_a_row(&L, mark, ends));
    mark = L.log_len;
    press(&L, '5');
    run_quiet(&L);
    static const char *const whole[] = {"say sats", "say address_full_groups", "ear data",
                                        "chars bc1qkd33yck74lg0kaq4tdcmu3hk4yruhjayxpe9ug", NULL};
    CHECK(lines_in_a_row(&L, mark, whole));
    CHECK(!HAS(&L, "say address_starts", mark));
}

/* The number on the line right after `after` (from `from`), or -1. */
static long num_after(const line_t *t, const char *after, size_t from)
{
    long at = find_line(t, after, from);
    if (at < 0)
        return -1;
    const char *p = t->log + at + strlen(after) + 1;
    long n = -1;
    if (sscanf(p, "num %ld", &n) != 1)
        return -1;
    return n;
}

/* Before each exchange of tones: how long it takes (in 5 s steps) and, once,
 * that a cut is mended by hanging up; the motif right before the tones. */
static void test_exchanges_announce_duration_and_reset(void)
{
    /* the seed by the handset: its fixed estimate */
    to_seed_how(&L);
    size_t mark = L.log_len;
    press(&L, '1');
    run_quiet(&L);
    static const char *const seed[] = {"state seed-air", "say seed_listening", "say receive_about",
                                       "say seconds", "say if_cut_reset", "ear receive", NULL};
    CHECK(lines_in_order(&L, mark, seed));
    long s = num_after(&L, "say receive_about", mark);
    CHECK(s >= 5 && s % 5 == 0);

    /* the account, sent: its estimate is what it takes */
    to_ready(&L, 0);
    press(&L, '2');
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '#');
    CHECK(run_until_line(&L, "ear send", mark, 20000));
    static const char *const send[] = {"state tones", "say send_about", "say seconds",
                                       "say if_cut_reset", "ear send", NULL};
    CHECK(lines_in_order(&L, mark, send));
    long n = num_after(&L, "say send_about", mark);
    CHECK(n >= 5 && n % 5 == 0);
    /* from the first part queued (the words and the motif said) */
    for (int i = 0; i < 3000 && strstr(L.log + mark, "\ntones ") == NULL; i++)
        step(&L, NULL);
    uint32_t t0 = L.now;
    CHECK(run_until_line(&L, "say again_hint", mark, 600000));
    long took = (long)(L.now - t0);
    long tones = find_line(&L, "ear send", mark);
    CHECK(tones >= 0 && strstr(L.log + mark, "\ntones ") > L.log + tones);
    if (n * 1000 - took > 5000 || took - n * 1000 > 5000)
        printf("said %ld s, took %ld ms\n", n, took);
    CHECK(n * 1000 - took <= 5000 && took - n * 1000 <= 5000);
    /* 0 sends it again: the estimate again, the reset note not */
    run_quiet(&L);
    mark = L.log_len;
    press(&L, '0');
    CHECK(run_until_line(&L, "ear send", mark, 20000));
    CHECK(HAS(&L, "say send_about", mark));
    CHECK(!HAS(&L, "say if_cut_reset", mark));

    /* the PSBT: a rough estimate and the reset note before listening; then
     * nothing is said until it is all heard (half-duplex: words would cost
     * parts) */
    to_ready(&L, 0);
    CHECK(tones_seed_keypad(zero16, 16, NULL, seed_tones, sizeof seed_tones) > 0);
    const psbt_vector *pv = vector("one input with change");
    int32_t cn = ut_psbt_to_cbor(pv->psbt, pv->psbt_len, psbt_cbor, sizeof psbt_cbor);
    CHECK(cn > 0);
    CHECK(ut_fountain_encoder_init(&psbt_enc, psbt_cbor, (size_t)cn, 100) >= 0);
    CHECK(psbt_enc.seq_len > 1);
    mark = L.log_len;
    press(&L, '1');
    run_quiet(&L);
    static const char *const psbt[] = {"state psbt-listen", "say psbt_listening", "say psbt_about",
                                       "say if_cut_reset", "ear receive", NULL};
    CHECK(lines_in_a_row(&L, mark, psbt));
    size_t first = 0;
    for (uint32_t i = 0; i < 4 * psbt_enc.seq_len && !HAS(&L, "state review", mark); i++) {
        CHECK(tones_next_part(&psbt_enc, part_tones, sizeof part_tones) > 0);
        size_t n = tones_render(part_tones, seed_audio, sizeof seed_audio / sizeof seed_audio[0]);
        for (size_t j = 0; j < n; j += CHUNK) {
            static int16_t c[CHUNK];
            memset(c, 0, sizeof c);
            memcpy(c, seed_audio + j, (n - j < CHUNK ? n - j : CHUNK) * sizeof c[0]);
            step(&L, c);
            if (first == 0 && L.v.rx.keys > 0) /* the first part is being heard */
                first = L.log_len;
        }
        run_ms(&L, 400);
    }
    CHECK(first > 0);
    long end = find_line(&L, "ear ok", first); /* the review begins: the reception is over */
    CHECK(end > 0);
    for (const char *q = L.log + first; end > 0 && q != NULL && q < L.log + end;) {
        CHECK(strncmp(q, "say ", 4) != 0 && strncmp(q, "num ", 4) != 0 && strncmp(q, "ear ", 4) != 0);
        q = strchr(q, '\n');
        q = q != NULL ? q + 1 : NULL;
    }
    CHECK(HAS(&L, "state review", first));
}

/* A duration: under 120 s in seconds rounded to 5, from 120 s in whole
 * minutes (rounded to the nearest). */
static void test_durations_in_seconds_or_minutes(void)
{
    static const struct {
        uint32_t ms;
        const char *num, *unit;
    } c[] = {{1000, "num 5", "say seconds"},     {61000, "num 60", "say seconds"},
             {117000, "num 115", "say seconds"}, {118000, "num 2", "say minutes"},
             {150000, "num 3", "say minutes"},   {170000, "num 3", "say minutes"},
             {200000, "num 3", "say minutes"},   {600000, "num 10", "say minutes"}};
    to_menu(&L);
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        size_t mark = L.log_len;
        CHECK_EQ_INT(ds_voice_say_duration(&L.v, DS_P_SEND_ABOUT, c[i].ms), DS_OK);
        take_echo(&L);
        const char *const want[] = {"say send_about", c[i].num, c[i].unit, NULL};
        if (!lines_in_a_row(&L, mark, want))
            printf("duration %u ms: %s", (unsigned)c[i].ms, L.log + mark);
        CHECK(lines_in_a_row(&L, mark, want));
        run_quiet(&L);
    }
}

/* ---- the packs (DSV3, Speex) ---- */

#define PACK_HDR 22u
#define PACK_ENTRY 8u /* u32 offset, u32 bytes */

static uint32_t rd32le(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void wr32le(uint8_t *b, uint32_t v)
{
    for (int k = 0; k < 4; k++)
        b[k] = (uint8_t)(v >> (8 * k));
}

/* A copy of es.bin to break one byte at a time, its CRC made right again so
 * that only the rule under test can reject it. */
static uint8_t edited[ES_BIN_LEN];

static void reseal(uint8_t *pack)
{
    uint32_t total = rd32le(pack + 10);
    wr32le(pack + 14, 0);
    wr32le(pack + 14, ds_crc32(0, pack, total));
}

/* The first clip of es.bin with more than one frame: its offset and bytes. */
static uint16_t a_long_clip(uint32_t *offset, uint32_t *bytes)
{
    for (uint16_t id = 0; id < DS_P_COUNT; id++) {
        const uint8_t *e = es_bin + PACK_HDR + PACK_ENTRY * id;
        *offset = rd32le(e);
        *bytes = rd32le(e + 4);
        if (*bytes > 2u * (1u + es_bin[*offset]))
            return id;
    }
    return DS_P_COUNT;
}

static void test_pack_v3_opens_and_says_a_clip(void)
{
    static ds_pack es;
    static ds_say s;
    static int16_t buf[16000 * 20];
    CHECK_EQ_INT(ds_pack_open(&es, es_bin, ES_BIN_LEN), DS_OK);
    CHECK(memcmp(es_bin, "DSV3es", 6) == 0);
    ds_say_init(&s, &es);
    CHECK_EQ_INT(ds_say_clip(&s, DS_P_GREETING), DS_OK);
    size_t n = 0;
    while (ds_say_busy(&s) && n < sizeof buf / sizeof buf[0])
        n += ds_say_read(&s, buf + n, 160);
    CHECK(!ds_say_busy(&s));
    size_t loud = 0;
    int peak = 0;
    for (size_t i = 0; i < n; i++) {
        int v = buf[i] < 0 ? -buf[i] : buf[i];
        loud += v > 500;
        peak = v > peak ? v : peak;
    }
    CHECK(n > 16000); /* the greeting lasts more than a second */
    CHECK(loud > n / 10);
    CHECK(peak > 8000);
}

static void test_pack_v2_is_rejected(void)
{
    ds_pack p;
    memcpy(edited, es_bin, ES_BIN_LEN);
    edited[3] = '2';
    edited[6] = 2; /* format 2, as the ADPCM packs were */
    reseal(edited);
    CHECK_EQ_INT(ds_pack_open(&p, edited, ES_BIN_LEN), DS_ERR_ARG);
    memcpy(edited, es_bin, ES_BIN_LEN);
    edited[6] = 2; /* the magic alone is not enough */
    reseal(edited);
    CHECK_EQ_INT(ds_pack_open(&p, edited, ES_BIN_LEN), DS_ERR_ARG);
}

static void test_pack_frame_length_out_of_range_is_rejected(void)
{
    ds_pack p;
    uint32_t offset, bytes;
    CHECK(a_long_clip(&offset, &bytes) < DS_P_COUNT);
    uint32_t second = offset + 1u + es_bin[offset];
    static const uint8_t bad[] = {0, DS_SPEEX_MAX_BYTES + 1, 0xff};
    for (size_t k = 0; k < sizeof bad; k++) {
        memcpy(edited, es_bin, ES_BIN_LEN);
        edited[second] = bad[k];
        reseal(edited);
        CHECK_EQ_INT(ds_pack_open(&p, edited, ES_BIN_LEN), DS_ERR_ARG);
    }
    /* The same pack, resealed but unchanged, opens. */
    memcpy(edited, es_bin, ES_BIN_LEN);
    reseal(edited);
    CHECK_EQ_INT(ds_pack_open(&p, edited, ES_BIN_LEN), DS_OK);
}

static void test_pack_clip_past_end_is_rejected(void)
{
    ds_pack p;
    uint32_t offset, bytes;
    uint16_t id = a_long_clip(&offset, &bytes);
    CHECK(id < DS_P_COUNT);

    /* The clip's last frame says one byte more than the clip has. */
    uint32_t last = offset;
    while (last + 1u + es_bin[last] < offset + bytes)
        last += 1u + es_bin[last];
    CHECK_EQ_INT(last + 1u + es_bin[last], offset + bytes);
    memcpy(edited, es_bin, ES_BIN_LEN);
    edited[last]++;
    reseal(edited);
    CHECK_EQ_INT(ds_pack_open(&p, edited, ES_BIN_LEN), DS_ERR_ARG);

    /* The clip's byte count reaches past the end of the pack. */
    memcpy(edited, es_bin, ES_BIN_LEN);
    wr32le(edited + PACK_HDR + PACK_ENTRY * id + 4, ES_BIN_LEN - offset + 1u);
    reseal(edited);
    CHECK_EQ_INT(ds_pack_open(&p, edited, ES_BIN_LEN), DS_ERR_ARG);

    /* The clip cut one byte short: its last frame now overruns it. */
    memcpy(edited, es_bin, ES_BIN_LEN);
    wr32le(edited + PACK_HDR + PACK_ENTRY * id + 4, bytes - 1u);
    reseal(edited);
    CHECK_EQ_INT(ds_pack_open(&p, edited, ES_BIN_LEN), DS_ERR_ARG);
}

/* The keys the firmware's detector hears in x (16 kHz), fed as ds_voice
 * feeds it, 10 ms at a time. After each slice ds_voice reads the live key
 * (key_now) and takes a tone heard twice in a row (20 ms, no 50 ms gap) as
 * begun (held_key: ONSET_RUNS, KEY_GAP_RUNS); a key counts later, at 70 ms.
 * A begun tone already matters: a real key starting within 250 ms of it is
 * taken for data and dropped. So no prompt may begin a tone at all. The
 * listener's own live view and groups must stay empty too. */
static int keys_in(const int16_t *x, size_t n)
{
    static ut_listener l;
    char lv[UT_LIVE + 1], g[UT_GROUP + 1];
    char run_key = 0, key = 0;
    unsigned run_n = 0, run_gap = 0;
    int heard = 0;
    ut_listener_init(&l, 16000);
    for (size_t i = 0; i < n; i += CHUNK) {
        ut_listener_push(&l, x + i, n - i < CHUNK ? n - i : CHUNK);
        char k = l.keys.live_key;
        if (k == 0) {
            if (++run_gap >= 5) {
                run_key = 0;
                run_n = 0;
            }
            continue;
        }
        run_n = k == run_key ? run_n + 1 : 1;
        run_key = k;
        run_gap = 0;
        if (run_n >= 2 && key == 0) {
            key = k;
            heard++;
        }
    }
    ut_listener_flush(&l);
    lv[0] = g[0] = 0;
    heard += ut_listener_live(&l, lv, sizeof lv) > 0;
    heard += ut_listener_group(&l, g, sizeof g) > 0;
    if (heard)
        printf("  key '%c', live '%s', group '%s'\n", key ? key : ' ', lv, g);
    return heard;
}

static void test_no_prompt_decodes_to_a_dtmf_key(void)
{
    static ds_pack packs[2];
    static ds_say s;
    static int16_t buf[16000 * 40];
    CHECK_EQ_INT(ds_pack_open(&packs[0], ds_pack_en, ds_pack_en_len), DS_OK);
    CHECK_EQ_INT(ds_pack_open(&packs[1], es_bin, ES_BIN_LEN), DS_OK);
    for (int k = 0; k < 2; k++) {
        for (uint16_t id = 0; id < DS_P_COUNT; id++) {
            ds_say_init(&s, &packs[k]);
            ds_say_clip(&s, id);
            size_t n = 0;
            while (ds_say_busy(&s) && n < sizeof buf / sizeof buf[0])
                n += ds_say_read(&s, buf + n, CHUNK);
            CHECK(!ds_say_busy(&s));
            if (keys_in(buf, n)) {
                printf("clip %s (%s) decodes to a DTMF key\n", ds_prompt_name[id],
                       packs[k].lang);
                ds_failures++;
            }
        }
    }
}

/* What the firmware's detector makes of x (16 kHz) begun `phase` samples
 * after one of its 10 ms hops: runs of one key as held_key counts them
 * (dropouts under 50 ms bridged). *onsets: tones begun (2 detections);
 * returns the longest run, in detections (7, 70 ms, is a key). */
static unsigned longest_run(const int16_t *x, size_t n, unsigned phase, unsigned *onsets)
{
    static ut_detector d;
    static const int16_t zero[160];
    ut_det det[64];
    char run_key = 0;
    unsigned run_n = 0, run_gap = 0, longest = 0;
    ut_detector_init(&d, 16000);
    *onsets = 0;
    for (size_t at = 0, fed = 0; at < n + phase;) {
        size_t c, used, k;
        if (fed < phase) {
            c = phase - fed < 160 ? phase - fed : 160;
            k = ut_detector_push(&d, zero, c, &used, det, 64);
            fed += used;
            at += used;
        } else {
            c = n - (at - phase) < 1024 ? n - (at - phase) : 1024;
            k = ut_detector_push(&d, x + (at - phase), c, &used, det, 64);
            at += used;
        }
        for (size_t i = 0; i < k; i++) {
            char key = det[i].key;
            if (key == 0) {
                if (++run_gap >= 5) {
                    run_key = 0;
                    run_n = 0;
                }
                continue;
            }
            run_n = key == run_key ? run_n + 1 : 1;
            run_key = key;
            run_gap = 0;
            if (run_n == 2)
                (*onsets)++;
            if (run_n > longest)
                longest = run_n;
        }
    }
    return longest;
}

/* Every clip of both packs and every motif, through ds_say at 16 kHz,
 * begun at every sample of the detector's 10 ms hop (160 alignments: all
 * there are, the 8 kHz decimation included): none ever sounds as a key
 * (70 ms of one tone). A few begin a tone for a moment at some alignments
 * (test_no_prompt_decodes_to_a_dtmf_key sees only one): the signer then
 * pauses and goes on (test_barge_in_no_key_lets_it_go_on). Their number
 * is held under a ceiling, so a voice that makes it much worse fails. */
#define ONSET_PAIRS_CEILING 250 /* 180 pairs (12 sounds) when written */
static void test_no_prompt_makes_a_key_at_any_alignment(void)
{
    static ds_pack packs[2];
    static ds_say s;
    static int16_t buf[16000 * 40];
    unsigned pairs = 0, onset_pairs = 0, keys = 0, sounds = 0, onset_sounds = 0, worst = 0;
    CHECK_EQ_INT(ds_pack_open(&packs[0], ds_pack_en, ds_pack_en_len), DS_OK);
    CHECK_EQ_INT(ds_pack_open(&packs[1], es_bin, ES_BIN_LEN), DS_OK);
    for (int k = 0; k < 3; k++) {
        unsigned count = k < 2 ? DS_P_COUNT : DS_EAR_LAST + 1u;
        for (unsigned id = k < 2 ? 0u : (unsigned)DS_EAR_NOTICE; id < count; id++) {
            size_t n = 0;
            int with_onset = 0;
            ds_say_init(&s, &packs[k < 2 ? k : 0]);
            if (k < 2)
                ds_say_clip(&s, (uint16_t)id);
            else
                CHECK_EQ_INT(ds_say_earcon(&s, (int)id), DS_OK);
            while (ds_say_busy(&s) && n < sizeof buf / sizeof buf[0])
                n += ds_say_read(&s, buf + n, CHUNK);
            CHECK(!ds_say_busy(&s));
            sounds++;
            for (unsigned phase = 0; phase < 160; phase++) {
                unsigned onsets;
                unsigned longest = longest_run(buf, n, phase, &onsets);
                pairs++;
                if (longest > worst)
                    worst = longest;
                if (onsets > 0) {
                    onset_pairs++;
                    with_onset = 1;
                }
                if (longest >= 7) {
                    printf("%s %s at alignment %u sounds as a key (%u ms)\n",
                           k < 2 ? ds_prompt_name[id] : "motif", k < 2 ? packs[k].lang : "",
                           phase, longest * 10);
                    keys++;
                }
            }
            onset_sounds += with_onset;
        }
    }
    printf("talk-off: %u sounds x 160 alignments: %u begin a tone (%u sounds), "
           "longest %u ms, %u keys\n",
           sounds, onset_pairs, onset_sounds, worst * 10, keys);
    CHECK(pairs > 160u * 2u * 100u);
    CHECK_EQ_INT(keys, 0);
    CHECK(onset_pairs <= ONSET_PAIRS_CEILING);
}

/* A sound (a clip of pack k < 2, or a motif when k == 2) at 16 kHz. */
static size_t render_sound(ds_pack *packs, int k, unsigned id, int16_t *out, size_t cap)
{
    static ds_say s;
    size_t n = 0;
    ds_say_init(&s, &packs[k < 2 ? k : 0]);
    if (k < 2)
        ds_say_clip(&s, (uint16_t)id);
    else
        ds_say_earcon(&s, (int)id);
    while (ds_say_busy(&s) && n + CHUNK <= cap)
        n += ds_say_read(&s, out + n, CHUNK);
    return n;
}

/* The barge-in itself, with the speech's own sound: each sound that can
 * begin a tone, played as the signer's voice at every alignment of its
 * listener; when it mutes, the sound goes on for DS_VOICE_OUT_DELAY_MS
 * (what the line-out still held), then stops. It never makes a key. */
static void test_own_voice_through_the_mute_is_no_key(void)
{
    static ds_pack packs[2];
    static int16_t buf[16000 * 40], in[16000 * 41];
    unsigned trials = 0, muted = 0, keys = 0;
    CHECK_EQ_INT(ds_pack_open(&packs[0], ds_pack_en, ds_pack_en_len), DS_OK);
    CHECK_EQ_INT(ds_pack_open(&packs[1], es_bin, ES_BIN_LEN), DS_OK);
    to_menu(&L);
    CHECK_EQ_INT(ds_say_pause(&L.v.say, 60000), DS_OK); /* speaking: silence */
    run_ms(&L, 100);
    CHECK(ds_voice_speaking(&L.v));
    save_line(&L);
    for (int k = 0; k < 3; k++) {
        unsigned count = k < 2 ? DS_P_COUNT : DS_EAR_LAST + 1u;
        for (unsigned id = k < 2 ? 0u : (unsigned)DS_EAR_NOTICE; id < count; id++) {
            size_t n = render_sound(packs, k, id, buf, sizeof buf / sizeof buf[0]);
            unsigned onsets = 0, any = 0;
            for (unsigned phase = 0; phase < 160 && !any; phase++) {
                (void)longest_run(buf, n, phase, &onsets);
                any = onsets > 0;
            }
            if (!any)
                continue;
            for (unsigned pad = 0; pad < 160; pad++) {
                size_t total = (pad + n + 16000 / 2 + CHUNK - 1) / CHUNK * CHUNK;
                size_t cut = SIZE_MAX;
                uint32_t k0 = ds_voice_keys(&L.v);
                memset(in, 0, total * sizeof in[0]);
                memcpy(in + pad, buf, n * sizeof buf[0]);
                for (size_t at = 0; at < total; at += CHUNK) {
                    if (cut != SIZE_MAX)
                        for (size_t i = at; i < at + CHUNK; i++)
                            if (i >= cut)
                                in[i] = 0;
                    step(&L, in + at);
                    if (cut == SIZE_MAX && L.v.muted)
                        cut = at + CHUNK + DS_VOICE_OUT_DELAY_MS * 16u;
                }
                trials++;
                muted += cut != SIZE_MAX;
                if (ds_voice_keys(&L.v) != k0) {
                    printf("%s %s +%u: its own voice made a key\n",
                           k < 2 ? ds_prompt_name[id] : "motif", k < 2 ? packs[k].lang : "", pad);
                    keys++;
                }
                restore_line(&L);
            }
        }
    }
    printf("own voice through the mute: %u tries, %u muted, %u keys\n", trials, muted, keys);
    CHECK(trials >= 160);
    CHECK(muted > 0);
    CHECK_EQ_INT(keys, 0);
}

/* Where one sound runs into the next (clip to clip in each pack, clip to
 * motif and back), with no pause between: the last and first 100 ms of
 * each ordered pair, at an alignment that turns with the pair. No join
 * sounds as a key. */
static void test_no_join_makes_a_key(void)
{
    enum { EDGE = 1600, MAXS = DS_P_COUNT * 2 + DS_EAR_LAST + 1 };
    static ds_pack packs[2];
    static int16_t buf[16000 * 40], head[MAXS][EDGE], tail[MAXS][EDGE], join[2 * EDGE];
    static size_t hn[MAXS], tn[MAXS];
    static int pack_of[MAXS];
    unsigned m = 0, joins = 0, keys = 0, worst = 0;
    CHECK_EQ_INT(ds_pack_open(&packs[0], ds_pack_en, ds_pack_en_len), DS_OK);
    CHECK_EQ_INT(ds_pack_open(&packs[1], es_bin, ES_BIN_LEN), DS_OK);
    for (int k = 0; k < 3; k++) {
        unsigned count = k < 2 ? DS_P_COUNT : DS_EAR_LAST + 1u;
        for (unsigned id = k < 2 ? 0u : (unsigned)DS_EAR_NOTICE; id < count; id++) {
            size_t n = render_sound(packs, k, id, buf, sizeof buf / sizeof buf[0]);
            if (n == 0)
                continue;
            hn[m] = n < EDGE ? n : EDGE;
            tn[m] = hn[m];
            memcpy(head[m], buf, hn[m] * sizeof buf[0]);
            memcpy(tail[m], buf + n - tn[m], tn[m] * sizeof buf[0]);
            pack_of[m] = k;
            m++;
        }
    }
    for (unsigned a = 0; a < m; a++) {
        for (unsigned b = 0; b < m; b++) {
            /* within one voice: a pack's clips, and the motifs with either */
            if (pack_of[a] < 2 && pack_of[b] < 2 && pack_of[a] != pack_of[b])
                continue;
            unsigned onsets;
            memcpy(join, tail[a], tn[a] * sizeof join[0]);
            memcpy(join + tn[a], head[b], hn[b] * sizeof join[0]);
            unsigned r = longest_run(join, tn[a] + hn[b], (a * 7u + b * 13u) % 160u, &onsets);
            joins++;
            if (r > worst)
                worst = r;
            if (r >= 7)
                keys++;
        }
    }
    printf("joins: %u, longest %u ms, %u keys\n", joins, worst * 10, keys);
    CHECK(joins > 50000);
    CHECK_EQ_INT(keys, 0);
}

/* ---- the melodic motifs ---- */

static const unsigned dtmf_hz[8] = {697, 770, 852, 941, 1209, 1336, 1477, 1633};

static void test_motifs_stay_clear_of_dtmf(void)
{
    for (int e = DS_EAR_NOTICE; e <= DS_EAR_LAST; e++) {
        const uint16_t *hz, *ms;
        uint8_t n;
        CHECK_EQ_INT(ds_say_motif(e, &hz, &ms, &n), DS_OK);
        CHECK(n >= 1);
        for (uint8_t i = 0; i < n; i++) {
            CHECK(hz[i] >= 300 && hz[i] <= 2500);
            for (int k = 0; k < 8; k++) {
                double d = (double)hz[i] - (double)dtmf_hz[k];
                if ((d < 0 ? -d : d) <= 0.015 * (double)dtmf_hz[k]) {
                    printf("motif %d note %u Hz is within 1.5%% of %u\n", e, hz[i], dtmf_hz[k]);
                    ds_failures++;
                }
            }
        }
    }
    {
        const uint16_t *hz, *ms;
        uint8_t n;
        CHECK_EQ_INT(ds_say_motif(0, &hz, &ms, &n), DS_ERR_ARG);
        CHECK_EQ_INT(ds_say_motif(DS_EAR_LAST + 1, &hz, &ms, &n), DS_ERR_ARG);
    }
}

static void test_motif_lengths(void)
{
    for (int e = DS_EAR_NOTICE; e <= DS_EAR_LAST; e++) {
        const uint16_t *hz, *ms;
        uint8_t n;
        unsigned total = 0;
        CHECK_EQ_INT(ds_say_motif(e, &hz, &ms, &n), DS_OK);
        for (uint8_t i = 0; i < n; i++)
            total += ms[i];
        if (e == DS_EAR_OPTION) {
            CHECK(total >= 10 && total <= 150);
        } else if (e == DS_EAR_DATA) {
            CHECK(total >= 10 && total <= 200);
        } else {
            CHECK(total >= 300 && total <= 800);
        }
    }
}

static void test_no_motif_decodes_to_a_dtmf_key(void)
{
    static ds_pack p;
    static ds_say s;
    static int16_t buf[16000 * 2];
    CHECK_EQ_INT(ds_pack_open(&p, ds_pack_en, ds_pack_en_len), DS_OK);
    for (int e = DS_EAR_NOTICE; e <= DS_EAR_LAST; e++) {
        ds_say_init(&s, &p);
        CHECK_EQ_INT(ds_say_earcon(&s, e), DS_OK);
        size_t n = 0;
        while (ds_say_busy(&s) && n < sizeof buf / sizeof buf[0])
            n += ds_say_read(&s, buf + n, CHUNK);
        CHECK(!ds_say_busy(&s));
        if (keys_in(buf, n)) {
            printf("motif %d decodes to a DTMF key\n", e);
            ds_failures++;
        }
    }
}

static void test_motifs_are_distinct(void)
{
    for (int a = DS_EAR_NOTICE; a <= DS_EAR_LAST; a++) {
        for (int b = a + 1; b <= DS_EAR_LAST; b++) {
            const uint16_t *ha, *hb, *ma, *mb;
            uint8_t na, nb;
            ds_say_motif(a, &ha, &ma, &na);
            ds_say_motif(b, &hb, &mb, &nb);
            int same = na == nb && memcmp(ha, hb, na * sizeof ha[0]) == 0;
            if (same) {
                printf("motifs %d and %d have the same notes\n", a, b);
                ds_failures++;
            }
        }
    }
}

TEST_MAIN(test_one_language_no_list, test_greets_on_off_hook,
          test_languages_listed_and_chosen, test_about_says_fingerprints,
          test_hash_in_menu_does_nothing, test_two_toggles_network, test_zero_repeats,
          test_key_during_prompt_barges_in, test_own_voice_is_not_a_key,
          test_hang_up_keeps_session, test_air_seed_with_long_pin,
          test_air_seed_short_pin_refused, test_keypad_seed_no_pin,
          test_keypad_seed_with_pin, test_pin_chars_never_in_echo,
          test_hang_up_mid_pin_wipes, test_keypad_choice_key_not_data,
          test_air_listens_only_when_quiet, test_pin_keys_forgotten,
          test_air_warning_once, test_heavy_work_in_tick,
          test_frame_tones_not_pin_keys, test_reboot_wipes_everything,
          test_review_speaks_outputs, test_five_reads_whole_address, test_whole_address_p2tr,
          test_amount_units, test_hook_flash_signs, test_hook_timing_boundaries,
          test_key_does_not_confirm, test_hang_up_during_reception_back_to_menu,
          test_wrong_network_rejected, test_account_tones_decode,
          test_looping_wallet_drives_nothing, test_hash_waits_for_the_item,
          test_warns_foreign_inputs, test_warns_input_without_witness,
          test_looping_wallet_cable_pace, test_looping_wallet_air_pace,
          test_typing_pace_counts, test_gate_after_reception,
          test_multitap_same_key_paces, test_wipe_needs_hook,
          test_slow_wallet_never_wipes, test_slow_wallet_review_needs_every_item,
          test_slow_wallet_never_wipes_resume_or_fingerprint,
          test_hook_seen_at_its_edges_only, test_flash_during_prompt_ignored,
          test_review_fails_closed, test_review_names_keys, test_keypad_path_warns,
          test_help_once_after_8s, test_zero_repeats_last_heard,
          test_help_without_long_says_short, test_key_cuts_motif_and_long,
          test_barge_in_loses_few_keys, test_barge_in_hushes_on_onset,
          test_machine_tones_over_speech_are_no_keys,
          test_barge_in_no_key_lets_it_go_on, test_barge_in_no_key_keeps_the_confirmation,
          test_own_voice_said_whole_in_16ms_blocks,
          test_data_rule_still_holds_after_hush,
          test_no_help_while_receiving, test_boot_has_no_flashed_question,
          test_menu_says_choose_then_options_with_motif, test_network_said_when_it_matters,
          test_address_parts_named, test_exchanges_announce_duration_and_reset,
          test_durations_in_seconds_or_minutes,
          test_pack_v3_opens_and_says_a_clip, test_pack_v2_is_rejected,
          test_pack_frame_length_out_of_range_is_rejected,
          test_pack_clip_past_end_is_rejected, test_no_prompt_decodes_to_a_dtmf_key,
          test_no_prompt_makes_a_key_at_any_alignment,
          test_own_voice_through_the_mute_is_no_key, test_no_join_makes_a_key,
          test_motifs_stay_clear_of_dtmf, test_motif_lengths,
          test_no_motif_decodes_to_a_dtmf_key, test_motifs_are_distinct)
