/* DTMF-Signer "Centralita": the exchange's state machine. See ds_voice.h.
 *
 * Each state says its prompt in steps (say_step): a step is queued only
 * once the previous one has been played, so a step may change the voice
 * (ds_say picks the pack when an item starts playing) and a long prompt
 * (About) never has to fit the say queue at once. Entering a state, a
 * repeat (0, or DS_VOICE_REPEAT_MS of silence) and a resume all restart the
 * steps from 0. */
#include "ds_voice.h"

#include <stdio.h>
#include <string.h>

#include "ds_about.h"
#include "ds_core.h"
#include "ds_prompts.h"

enum {
    ST_IDLE = 0,   /* on-hook */
    ST_LANG,       /* the language list (more than one pack) */
    ST_GREET,      /* greeting; # to the menu */
    ST_MENU_EMPTY, /* the menu without a seed */
    ST_ABOUT,
    ST_RESUME_ASK  /* off-hook after a hang-up: # resumes, * wipes */
};

#define STEPS_DONE (-1)
#define SENTENCE_GAP_MS 300u /* between the sentences of a prompt */
#define ENTRY_GAP_MS 500u    /* between the entries of a list */

static const char *state_name(int st)
{
    switch (st) {
    case ST_IDLE: return "idle";
    case ST_LANG: return "lang";
    case ST_GREET: return "greet";
    case ST_MENU_EMPTY: return "menu-empty";
    case ST_ABOUT: return "about";
    case ST_RESUME_ASK: return "resume-ask";
    default: return "?";
    }
}

/* The states a hang-up can come back to (# after "resume_ask"). Those
 * before the menu just start again. */
static int resumable(int st)
{
    return st == ST_MENU_EMPTY || st == ST_ABOUT;
}

/* The states whose prompt is said again after DS_VOICE_REPEAT_MS of silence. */
static int repeats(int st)
{
    return st != ST_IDLE;
}

/* ---- echo (as ds_app's) ---- */

static void echo_pop(ds_voice *v)
{
    size_t n = strlen(v->echo) + 1;
    memmove(v->echo, v->echo + n, v->echo_len - n);
    v->echo_len -= n;
    memset(v->echo + v->echo_len, 0, n);
}

static void echo_line(ds_voice *v, const char *line)
{
    size_t n = strlen(line) + 1;
    if (n > DS_VOICE_ECHO_MAX) {
        v->echo_dropped++;
        return;
    }
    while (v->echo_len + n > sizeof v->echo) {
        echo_pop(v);
        v->echo_dropped++;
    }
    memcpy(v->echo + v->echo_len, line, n);
    v->echo_len += n;
}

/* What was queued to say, into the echo, in order with the state lines. */
static void echo_said(ds_voice *v)
{
    char t[DS_SAY_TOKEN_MAX];
    while (ds_say_take_log(&v->say, t, sizeof t) > 0) {
        echo_line(v, t);
    }
}

int32_t ds_voice_take_echo(ds_voice *v, char *out, size_t cap)
{
    size_t n;

    if (cap < DS_VOICE_ECHO_MAX) {
        return DS_ERR_SPACE;
    }
    echo_said(v);
    if (v->echo_dropped > 0) {
        int32_t r = (int32_t)snprintf(out, cap, "echo-dropped %u", (unsigned)v->echo_dropped);
        v->echo_dropped = 0;
        return r;
    }
    if (v->echo_len == 0) {
        return 0;
    }
    n = strlen(v->echo);
    memcpy(out, v->echo, n + 1);
    echo_pop(v);
    return (int32_t)n;
}

/* ---- what each state says ---- */

static const ds_pack *voice(const ds_voice *v)
{
    return &v->packs.pack[v->lang];
}

static void clip(ds_voice *v, uint16_t id)
{
    ds_say_clip(&v->say, id);
}

static void hex4(const uint8_t b[4], char out[9])
{
    snprintf(out, 9, "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);
}

/* Queues step v->step of the state's prompt; 0 when the prompt is over.
 * Called only while nothing is being said. */
static int say_step(ds_voice *v)
{
    int k = v->step;
    char fp[9];

    switch (v->state) {
    case ST_LANG:
        /* each language in its own voice: "For English, press 1", ... */
        if (k >= v->packs.n) {
            ds_say_set_pack(&v->say, voice(v));
            return 0;
        }
        ds_say_set_pack(&v->say, &v->packs.pack[k]);
        clip(v, DS_P_LANG_PRESS);
        clip(v, (uint16_t)(DS_P_C_1 + k));
        ds_say_pause(&v->say, ENTRY_GAP_MS);
        break;
    case ST_GREET:
        if (k > 0) {
            return 0;
        }
        clip(v, DS_P_GREETING);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        clip(v, DS_P_FLASHED);
        break;
    case ST_MENU_EMPTY:
        if (k > 0) {
            return 0;
        }
        clip(v, DS_P_MENU_EMPTY);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        clip(v, v->testnet ? DS_P_NETWORK_NOW_TEST : DS_P_NETWORK_NOW_MAIN);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        clip(v, DS_P_ABOUT);
        break;
    case ST_ABOUT:
        /* the firmware's version, then each pack: its language, its fingerprint */
        if (k == 0) {
            ds_say_chars(&v->say, DS_VERSION);
        } else if (k <= v->packs.n) {
            ds_say_chars(&v->say, v->packs.pack[k - 1].lang);
            ds_say_pause(&v->say, SENTENCE_GAP_MS);
            hex4(v->packs.fp[k - 1], fp);
            ds_say_chars_grouped(&v->say, fp);
        } else {
            return 0;
        }
        ds_say_pause(&v->say, ENTRY_GAP_MS);
        break;
    case ST_RESUME_ASK:
        if (k > 0) {
            return 0;
        }
        clip(v, DS_P_RESUME_ASK);
        break;
    default:
        return 0;
    }
    echo_said(v);
    v->step++;
    return 1;
}

/* Queues the next steps while nothing is being said. */
static void pump(ds_voice *v)
{
    while (v->step != STEPS_DONE && !ds_say_busy(&v->say)) {
        if (!say_step(v)) {
            v->step = STEPS_DONE;
        }
    }
}

/* Stops what is being said (barge-in), back to the chosen voice. */
static void hush(ds_voice *v)
{
    ds_say_clear(&v->say);
    ds_say_set_pack(&v->say, voice(v));
    v->step = STEPS_DONE;
}

/* Moves to st and says its prompt once what is queued has been said. */
static void go(ds_voice *v, int st)
{
    char l[40];
    v->state = st;
    snprintf(l, sizeof l, "state %s", state_name(st));
    echo_line(v, l);
    v->step = 0;
    pump(v);
}

/* Stops talking, moves to st and says its prompt. */
static void enter(ds_voice *v, int st)
{
    hush(v);
    go(v, st);
}

/* The state's prompt again, from the start. */
static void repeat(ds_voice *v)
{
    hush(v);
    v->step = 0;
    pump(v);
}

/* "Wipe and start over": the session goes, the language chosen for this
 * power-up stays. Tasks 8-9 wipe the seed, the wallet and the PSBT here. */
static void wipe(ds_voice *v)
{
    v->testnet = 0;
    v->resume_to = ST_IDLE;
    hush(v);
    clip(v, DS_P_WIPED);
    echo_said(v);
    go(v, ST_MENU_EMPTY);
}

/* ---- keys ---- */

static void on_key(ds_voice *v, char c)
{
    int st;
    char l[16];

    switch (v->state) {
    case ST_LANG:
        if (c >= '1' && c < '1' + v->packs.n) {
            v->lang = c - '1';
            v->lang_chosen = 1;
            snprintf(l, sizeof l, "lang %s", voice(v)->lang);
            echo_line(v, l);
            enter(v, ST_GREET);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_GREET:
        if (c == '#') {
            enter(v, ST_MENU_EMPTY);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_MENU_EMPTY:
        /* '1' (load seed) comes with Task 8 */
        if (c == '2') {
            v->testnet = !v->testnet;
            hush(v);
            clip(v, v->testnet ? DS_P_NETWORK_NOW_TEST : DS_P_NETWORK_NOW_MAIN);
            echo_said(v);
        } else if (c == '9') {
            v->about_back = ST_MENU_EMPTY;
            enter(v, ST_ABOUT);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_ABOUT:
        if (c == '#' || c == '*') {
            enter(v, v->about_back);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_RESUME_ASK:
        if (c == '#') {
            st = v->resume_to;
            v->resume_to = ST_IDLE;
            enter(v, st);
        } else if (c == '*') {
            wipe(v);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    default:
        break;
    }
}

void ds_voice_line_in(ds_voice *v, const int16_t *x, size_t n)
{
    char k[UT_LIVE + 1];
    int32_t got;

    if (v->state == ST_IDLE || !v->hook) {
        return; /* on-hook: nothing to hear */
    }
    ut_listener_push(&v->l, x, n);
    got = ut_listener_live(&v->l, k, sizeof k);
    for (int32_t i = 0; i < got; i++) {
        v->keys++;
        on_key(v, k[i]);
    }
    ds_wipe(k, sizeof k);
}

size_t ds_voice_line_out(ds_voice *v, int16_t *out, size_t n)
{
    size_t done = 0;
    while (done < n) {
        size_t c = n - done > 32 ? 32 : n - done;
        pump(v);
        ds_say_read(&v->say, out + done, c);
        done += c;
    }
    return n;
}

/* ---- the hook ---- */

/* Off-hook from idle: resume, the language list, or the greeting. */
static void answer(ds_voice *v)
{
    ut_listener_init(&v->l, v->rate); /* nothing heard before counts */
    if (v->resume_to != ST_IDLE) {
        enter(v, ST_RESUME_ASK);
    } else if (v->packs.n > 1 && !v->lang_chosen) {
        enter(v, ST_LANG);
    } else {
        enter(v, ST_GREET);
    }
}

/* A long on-hook: the session stays, to resume at the next off-hook. */
static void hang_up(ds_voice *v)
{
    if (resumable(v->state)) {
        v->resume_to = v->state;
    }
    enter(v, ST_IDLE);
}

/* A short on-hook. Task 9: in the confirm step it signs. */
static void flash(ds_voice *v)
{
    echo_line(v, "hook-flash");
}

static void debounce(ds_voice *v, uint32_t now)
{
    uint32_t down;

    if (v->hook_raw == v->hook || now - v->raw_at < DS_VOICE_DEBOUNCE_MS) {
        return;
    }
    v->hook = v->hook_raw;
    if (!v->hook) {
        v->down_at = v->raw_at;
        return;
    }
    if (v->state != ST_IDLE) {
        down = v->raw_at - v->down_at;
        if (down >= DS_VOICE_HANGUP_MS) {
            hang_up(v); /* no tick came while on-hook */
        } else {
            if (down >= DS_VOICE_FLASH_MIN_MS) {
                flash(v);
            }
            return;
        }
    }
    answer(v);
}

void ds_voice_hook(ds_voice *v, int off_hook, uint32_t now_ms)
{
    off_hook = off_hook ? 1 : 0;
    if (off_hook != v->hook_raw) {
        v->hook_raw = off_hook;
        v->raw_at = now_ms;
    }
    debounce(v, now_ms);
}

void ds_voice_tick(ds_voice *v, uint32_t now_ms)
{
    v->now = now_ms;
    debounce(v, now_ms);
    if (!v->hook && v->state != ST_IDLE && now_ms - v->down_at >= DS_VOICE_HANGUP_MS) {
        hang_up(v);
    }
    if (!repeats(v->state) || !v->hook || ds_voice_speaking(v)) {
        v->quiet_at = now_ms;
    } else if (now_ms - v->quiet_at >= DS_VOICE_REPEAT_MS) {
        v->quiet_at = now_ms;
        repeat(v);
    }
}

/* ---- the rest ---- */

void ds_voice_init(ds_voice *v, uint32_t rate, const uint8_t *pack_region,
                   uint32_t region_len)
{
    ds_pack en;

    memset(v, 0, sizeof *v);
    v->rate = rate;
    if (ds_pack_open(&en, ds_pack_en, ds_pack_en_len) != DS_OK ||
        ds_packs_scan(&v->packs, &en, pack_region, region_len) != DS_OK) {
        /* the built-in pack is part of the firmware: this cannot fail */
        memset(&v->packs, 0, sizeof v->packs);
        v->packs.pack[0] = en;
        v->packs.n = 1;
    }
    ds_say_init(&v->say, voice(v));
    ut_listener_init(&v->l, rate);
    v->step = STEPS_DONE;
    go(v, ST_IDLE);
}

uint32_t ds_voice_keys(const ds_voice *v)
{
    return v->keys;
}

int ds_voice_speaking(const ds_voice *v)
{
    return ds_say_busy(&v->say) || v->step != STEPS_DONE;
}
