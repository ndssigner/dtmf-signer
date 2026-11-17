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
    ST_RESUME_ASK, /* off-hook after a hang-up: # resumes, * wipes */
    ST_SEED_HOW,   /* by the handset, 1; by the keypad, 2 */
    ST_SEED_AIR,   /* the wallet plays the encrypted seed (half-duplex) */
    ST_PIN,        /* its PIN, by multi-tap */
    ST_KEYPAD,     /* the seed typed: "*", the digits, "#" */
    ST_KEYPAD_PIN, /* its PIN if it has one, by multi-tap; "#" alone: none */
    ST_FINGERPRINT,/* the seed's fingerprint: # derives, * wipes */
    ST_MENU_READY  /* the menu with a seed */
};

enum { JOB_NONE = 0, JOB_PIN, JOB_DERIVE }; /* heavy work, run from a tick */

#define STEPS_DONE (-1)
#define KEY_QUIET_RUNS 3 /* detections (10 ms each) without a key: it is over */
#define PIN_KEY_RUNS 7   /* a PIN key counts once it has lasted 70 ms */
#define PIN_GAP_RUNS 5   /* a PIN key is over after 50 ms without it */
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
    case ST_SEED_HOW: return "seed-how";
    case ST_SEED_AIR: return "seed-air";
    case ST_PIN: return "pin";
    case ST_KEYPAD: return "seed-keypad";
    case ST_KEYPAD_PIN: return "keypad-pin";
    case ST_FINGERPRINT: return "fingerprint";
    case ST_MENU_READY: return "menu-ready";
    default: return "?";
    }
}

/* Where # after "resume_ask" goes back to when a hang-up left st (ST_IDLE:
 * nowhere; the states before the menu just start again). A hang-up wipes
 * what was being typed or received, so an input starts again: the seed is
 * played or typed again. */
static int resume_point(int st)
{
    switch (st) {
    case ST_MENU_EMPTY:
    case ST_ABOUT:
    case ST_SEED_HOW:
    case ST_FINGERPRINT:
    case ST_MENU_READY:
        return st;
    case ST_SEED_AIR:
    case ST_PIN:
        return ST_SEED_AIR;
    case ST_KEYPAD:
    case ST_KEYPAD_PIN:
        return ST_KEYPAD;
    default:
        return ST_IDLE;
    }
}

/* The inputs: their prompt is not said again by itself (the silence of a
 * reception or of someone typing), and keys there are data. */
static int input(int st)
{
    return st == ST_SEED_AIR || st == ST_PIN || st == ST_KEYPAD || st == ST_KEYPAD_PIN;
}

/* The states whose prompt is said again after DS_VOICE_REPEAT_MS of silence. */
static int repeats(int st)
{
    return st != ST_IDLE && !input(st);
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
    case ST_SEED_HOW:
        if (k > 0) {
            return 0;
        }
        clip(v, DS_P_SEED_HOW);
        break;
    case ST_SEED_AIR:
        /* the warning, then "listening": nothing more is said until the
         * seed has been heard (half-duplex) */
        if (k > 0) {
            return 0;
        }
        if (!v->air_warned) { /* once per power-up */
            v->air_warned = 1;
            clip(v, DS_P_AIR_WARNING);
            ds_say_pause(&v->say, SENTENCE_GAP_MS);
        }
        ds_say_earcon(&v->say, DS_EAR_LISTEN);
        break;
    case ST_PIN:
        if (k > 0) {
            return 0;
        }
        clip(v, DS_P_PIN_ASK);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        clip(v, DS_P_PIN_WARNING);
        break;
    case ST_KEYPAD:
        if (k > 0) {
            return 0;
        }
        clip(v, DS_P_KEYPAD_ASK);
        break;
    case ST_KEYPAD_PIN:
        if (k > 0) {
            return 0;
        }
        clip(v, DS_P_KEYPAD_PIN_ASK);
        break;
    case ST_FINGERPRINT:
        if (k > 0) {
            return 0;
        }
        snprintf(fp, sizeof fp, "%08x", (unsigned)v->seed_fp);
        clip(v, DS_P_FINGERPRINT_IS);
        ds_say_chars_grouped(&v->say, fp);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        clip(v, DS_P_IF_CORRECT_HASH);
        break;
    case ST_MENU_READY:
        if (k > 0) {
            return 0;
        }
        clip(v, DS_P_MENU_READY);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        clip(v, DS_P_ABOUT);
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

/* ---- the secrets of an input ---- */

/* The key the listener hears now (0: none) and for how many detections in
 * a row (10 ms each). ur-tones has no call for it: this reads ut_keys' live
 * view, and is the only place that does. */
static char key_now(const ut_listener *l, uint32_t *run)
{
    *run = l->keys.live_run;
    return l->keys.live_key;
}

/* The listener wiped whole and started afresh: its groups, live view,
 * segmenter and detector may all hold keys typed (a PIN's presses, a seed's
 * digits). A key may still be sounding (the '#' that ended a PIN, the key
 * that chose the keypad): nothing is heard until no key has sounded for
 * 30 ms, so it is not heard twice. */
static void listen_afresh(ds_voice *v)
{
    ds_wipe(&v->l, sizeof v->l);
    ut_listener_init(&v->l, v->rate);
    v->deaf = 1;
    v->held = 0;
    v->run_key = 0;
    v->run_n = 0;
    v->run_gap = 0;
}

/* A PIN key counts once it has been heard for 70 ms (a wallet still playing
 * the seed, 40 ms tones, each a different key from the last, is not
 * typing). Called once per 10 ms slice with the key heard now; dropouts
 * under 50 ms (the signer's own voice on the line can cause them) neither
 * end the key nor make it a new press. Returns the key to act on, or 0. */
static char pin_key(ds_voice *v, char key)
{
    if (key == 0) {
        if (++v->run_gap >= PIN_GAP_RUNS) {
            v->run_key = 0;
            v->run_n = 0;
            v->held = 0;
        }
        return 0;
    }
    if (key == v->run_key) {
        v->run_n++;
    } else {
        v->run_key = key;
        v->run_n = 1;
    }
    v->run_gap = 0;
    if (v->run_key != v->held && v->run_n >= PIN_KEY_RUNS) {
        v->held = v->run_key;
        return v->held;
    }
    return 0;
}

/* What was typed or received to zero: the receiver (and the seed group it
 * holds), the PIN, the listener, any work asked for on them. */
static void drop_input(ds_voice *v)
{
    ds_rx_wipe(&v->rx);
    v->rx_keys = 0;
    ds_multitap_wipe(&v->mt);
    listen_afresh(v);
    v->job = JOB_NONE;
    v->job_seen = 0;
}

/* "Wipe and start over": the session goes, the language chosen for this
 * power-up stays. Task 9 wipes the PSBT here too. */
static void wipe(ds_voice *v)
{
    drop_input(v);
    ds_session_wipe(&v->s);
    v->seed_fp = 0;
    v->testnet = 0;
    v->resume_to = ST_IDLE;
    hush(v);
    clip(v, DS_P_WIPED);
    echo_said(v);
    go(v, ST_MENU_EMPTY);
}

/* ---- loading the seed ---- */

/* A receiver for a seed, then st (ST_SEED_AIR or ST_KEYPAD). */
static void start_seed(ds_voice *v, int st)
{
    drop_input(v); /* deaf: the receiver starts once the choosing key is over */
    ds_rx_init(&v->rx, v->rate, DS_RX_SEED);
    enter(v, st);
}

/* The seed is held: its PIN (st: ST_PIN or ST_KEYPAD_PIN). No key sounds
 * now (the seed's group closed after 300 ms of silence), so the listener
 * starts afresh. */
static void ask_pin(ds_voice *v, int st)
{
    listen_afresh(v);
    ds_multitap_init(&v->mt);
    enter(v, st);
}

/* Loading failed or was refused: nothing stays; the question again. */
static void seed_failed(ds_voice *v)
{
    drop_input(v);
    ds_session_wipe(&v->s);
    v->seed_fp = 0;
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_FAIL);
    clip(v, DS_P_ERROR_GENERIC);
    echo_said(v);
    go(v, ST_SEED_HOW);
}

/* The PIN is complete: decrypt, apply the policy, load. As ds_app's job_pin:
 * the PIN, the group and the entropy are wiped on every path. By the
 * handset the seed came by air (DS_AIR: encrypted with a PIN of 12 or more);
 * typed on the keypad when asked it counts as a cable (DS_CABLE). */
static void load_seed(ds_voice *v)
{
    uint8_t entropy[32];
    size_t pin_len = v->mt.len;
    int encrypted = pin_len > 0;
    int channel = v->state == ST_PIN ? DS_AIR : DS_CABLE;
    int32_t n, r;
    ds_wallet tmp;
    char l[24];

    listen_afresh(v);
    if (ds_policy_accepts(encrypted ? DS_DATA_SEED_ENCRYPTED : DS_DATA_SEED_PLAIN,
                          channel, pin_len) < 0) {
        seed_failed(v); /* refused before it is even decrypted */
        return;
    }
    n = ds_rx_seed(&v->rx, encrypted ? v->mt.text : NULL, entropy);
    drop_input(v);
    if (n < 0) {
        ds_wipe(entropy, sizeof entropy);
        seed_failed(v);
        return;
    }
    ds_session_init(&v->s, v->testnet);
    r = ds_session_seed(&v->s, entropy, (size_t)n, encrypted, pin_len, channel, 0);
    if (r == DS_OK || r == DS_WARN_AIR) {
        r = ds_wallet_load(&tmp, entropy, (size_t)n, NULL, v->testnet);
        v->seed_fp = tmp.seed_fp;
        ds_wallet_wipe(&tmp);
    }
    ds_wipe(entropy, sizeof entropy);
    if (r != DS_OK) {
        seed_failed(v);
        return;
    }
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_OK);
    echo_said(v);
    v->state = ST_FINGERPRINT;
    echo_line(v, "state fingerprint");
    snprintf(l, sizeof l, "fingerprint %08x", (unsigned)v->seed_fp);
    echo_line(v, l);
    v->step = 0;
    pump(v);
}

/* Heavy work (a PBKDF2, a derivation) is not done on the audio path: it is
 * asked for here and run from ds_voice_tick, not in the tick that first sees
 * it (as ds_app's jobs). Meanwhile a short sound says the key was taken, and
 * keys are ignored. */
static void ask_job(ds_voice *v, int job)
{
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_NEXT);
    echo_said(v);
    v->job = job;
    v->job_seen = 0;
}

/* The fingerprint was accepted: the wallet, without a passphrase. */
static void derive(ds_voice *v)
{
    if (ds_session_passphrase(&v->s, NULL, DS_CABLE, 0) == DS_OK) {
        enter(v, ST_MENU_READY);
        return;
    }
    ds_session_wipe(&v->s);
    v->seed_fp = 0;
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_FAIL);
    clip(v, DS_P_ERROR_GENERIC);
    echo_said(v);
    go(v, ST_MENU_EMPTY);
}

static void run_job(ds_voice *v)
{
    int job = v->job;

    v->job = JOB_NONE;
    v->job_seen = 0;
    if (job == JOB_PIN && (v->state == ST_PIN || v->state == ST_KEYPAD_PIN)) {
        load_seed(v);
    } else if (job == JOB_DERIVE && v->state == ST_FINGERPRINT) {
        derive(v);
    }
}

/* What a PIN key did (a DS_MT_ code), said back: each character only as a
 * secret one ("say pin-char" in the echo). */
static void pin_said(ds_voice *v, int r)
{
    ds_multitap *m = &v->mt;

    switch (r) {
    case DS_MT_SHOW:
        hush(v);
        ds_say_secret_char(&v->say, m->pending);
        break;
    case DS_MT_COMMIT:
        hush(v);
        ds_say_earcon(&v->say, DS_EAR_NEXT);
        if (m->has_pending) {
            ds_say_secret_char(&v->say, m->pending);
        }
        break;
    case DS_MT_BACK:
        hush(v);
        ds_say_earcon(&v->say, DS_EAR_BACK);
        if (m->len > 0) {
            ds_say_secret_char(&v->say, m->text[m->len - 1]);
        }
        break;
    case DS_MT_DONE:
        ask_job(v, JOB_PIN);
        return;
    case DS_MT_CANCEL:
        drop_input(v);
        hush(v);
        clip(v, DS_P_CANCELLED);
        echo_said(v);
        go(v, ST_SEED_HOW);
        return;
    default:
        return;
    }
    echo_said(v);
}

/* The receiver's groups: a seed of the kind this state waits for goes on to
 * its PIN; anything else is dropped (by the keypad, with the fail sound;
 * by the handset, in silence: half-duplex). */
static void poll_rx(ds_voice *v)
{
    int32_t r;
    int keypad = v->state == ST_KEYPAD;

    while ((r = ds_rx_poll(&v->rx)) != DS_RX_NONE) {
        if (r == DS_RX_GOT_SEED) {
            if ((v->rx.group[0] == '*') == keypad) {
                ask_pin(v, keypad ? ST_KEYPAD_PIN : ST_PIN);
                return;
            }
            /* a frame typed?, keypad digits played?: not here */
            ds_rx_wipe(&v->rx);
            ds_rx_init(&v->rx, v->rate, DS_RX_SEED);
            v->rx_keys = 0;
        }
        if (keypad) {
            hush(v);
            ds_say_earcon(&v->say, DS_EAR_FAIL);
            echo_said(v);
        }
    }
}

/* The seed's audio into the receiver; keys counted for the LED. */
static int rx_in(ds_voice *v, const int16_t *x, size_t n)
{
    uint32_t got;

    ds_rx_push(&v->rx, x, n);
    got = v->rx.keys - v->rx_keys;
    v->rx_keys = v->rx.keys;
    v->keys += got;
    return got > 0;
}

/* ---- keys ---- */

static void on_key(ds_voice *v, char c)
{
    int st;
    char l[16];

    if (v->job != JOB_NONE) {
        return; /* busy: the key that asked for the work was enough */
    }
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
        if (c == '1') {
            enter(v, ST_SEED_HOW);
        } else if (c == '2') {
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
            if (st == ST_SEED_AIR || st == ST_KEYPAD) {
                start_seed(v, st);
            } else {
                enter(v, st);
            }
        } else if (c == '*') {
            wipe(v);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_SEED_HOW:
        if (c == '1') {
            start_seed(v, ST_SEED_AIR);
        } else if (c == '2') {
            start_seed(v, ST_KEYPAD);
        } else if (c == '*') {
            enter(v, ST_MENU_EMPTY);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_PIN:
    case ST_KEYPAD_PIN:
        pin_said(v, ds_multitap_key(&v->mt, c, v->now));
        break;
    case ST_FINGERPRINT:
        if (c == '#') {
            ask_job(v, JOB_DERIVE);
        } else if (c == '*') {
            wipe(v);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_MENU_READY:
        if (c == '3') {
            wipe(v);
        } else if (c == '9') {
            v->about_back = ST_MENU_READY;
            enter(v, ST_ABOUT);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    default:
        break;
    }
}

/* A slice (at most 10 ms) of what arrives on the line. */
static void hear(ds_voice *v, const int16_t *x, size_t n)
{
    char k[UT_LIVE + 1];
    int32_t got;
    uint32_t run;
    char key;

    if (v->state == ST_SEED_AIR) {
        /* half-duplex: the receiver hears only once the signer is quiet */
        if (!ds_voice_speaking(v)) {
            rx_in(v, x, n);
            poll_rx(v);
        }
        return;
    }
    if (v->state == ST_KEYPAD && !v->deaf) {
        if (rx_in(v, x, n) && ds_voice_speaking(v)) {
            hush(v); /* the first digit barges in */
        }
        poll_rx(v);
        return;
    }
    ut_listener_push(&v->l, x, n);
    got = ut_listener_live(&v->l, k, sizeof k);
    key = key_now(&v->l, &run);
    if (v->deaf) {
        if (key == 0 && run >= KEY_QUIET_RUNS) {
            v->deaf = 0;
        }
    } else if (v->state == ST_PIN || v->state == ST_KEYPAD_PIN) {
        key = pin_key(v, key);
        if (key != 0) {
            v->keys++;
            on_key(v, key);
        }
    } else {
        for (int32_t i = 0; i < got; i++) {
            v->keys++;
            on_key(v, k[i]);
        }
    }
    ds_wipe(k, sizeof k);
}

void ds_voice_line_in(ds_voice *v, const int16_t *x, size_t n)
{
    size_t slice = v->rate / 100 > 0 ? v->rate / 100 : 1;

    /* in slices of 10 ms, so a key's length is seen and a change of state
     * (to a receiver, to deafness) takes effect within the call */
    while (n > 0 && v->state != ST_IDLE && v->hook) {
        size_t c = n < slice ? n : slice;
        hear(v, x, c);
        x += c;
        n -= c;
    }
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
    listen_afresh(v); /* nothing heard before counts */
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
    int st = resume_point(v->state);
    if (st != ST_IDLE) {
        v->resume_to = st;
    }
    /* what was being typed or received goes, the listener with it */
    drop_input(v);
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
    if (v->job != JOB_NONE && v->job_seen) {
        run_job(v);
    }
    if (v->hook && v->job == JOB_NONE &&
        (v->state == ST_PIN || v->state == ST_KEYPAD_PIN)) {
        pin_said(v, ds_multitap_tick(&v->mt, now_ms)); /* a pause commits */
    }
    if (!repeats(v->state) || !v->hook || ds_voice_speaking(v)) {
        v->quiet_at = now_ms;
    } else if (now_ms - v->quiet_at >= DS_VOICE_REPEAT_MS) {
        v->quiet_at = now_ms;
        repeat(v);
    }
    if (v->job != JOB_NONE) {
        v->job_seen = 1; /* runs at the next tick */
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
