/* DTMF-Signer "Centralita": the exchange's state machine. See ds_voice.h.
 *
 * Each state says its prompt in steps (say_step): a step is queued only
 * once the previous one has been played, so a step may change the voice
 * (ds_say picks the pack when an item starts playing) and a long prompt
 * (About) never has to fit the say queue at once. Entering a state, a
 * repeat (0, or the help after DS_VOICE_HELP_MS of silence) and a resume all
 * restart the steps from 0. A step may also wait (STEP_WAIT): the tones of
 * the next part are made in ds_voice_tick, and the step is queued once they
 * are ready.
 *
 * Heavy work never runs in ds_voice_line_in: taking a frame heard
 * (ds_rx_poll: Reed-Solomon, fountain codes), the review, the signing and
 * making a part into tones all run from ds_voice_tick. */
#include "ds_voice.h"

#include <stdio.h>
#include <string.h>

#include "ds_about.h"
#include "ds_base64.h"
#include "ds_bitcoin.h"
#include "ds_core.h"
#include "ds_prompts.h"

enum {
    ST_IDLE = 0,   /* on-hook */
    ST_LANG,       /* the language list (more than one pack) */
    ST_MENU_EMPTY, /* the menu without a seed */
    ST_ABOUT,
    ST_RESUME_ASK, /* off-hook after a hang-up: # resumes, * asks to wipe (hook) */
    ST_SEED_HOW,   /* by the handset, 1; by the keypad, 2 */
    ST_SEED_AIR,   /* the wallet plays the encrypted seed (half-duplex) */
    ST_PIN,        /* its PIN, by multi-tap */
    ST_KEYPAD,     /* the seed typed: "*", the digits, "#" */
    ST_KEYPAD_PIN, /* its PIN if it has one, by multi-tap; "#" alone: none */
    ST_FINGERPRINT,/* the seed's fingerprint: # derives, * asks to wipe (hook) */
    ST_MENU_READY, /* the menu with a seed */
    ST_PSBT_RX,    /* the wallet plays the PSBT (half-duplex) */
    ST_REVIEW,     /* the review, item by item */
    ST_CONFIRM,    /* a hook flash signs */
    ST_HOLD_NEAR,  /* signed (or the account): # plays it */
    ST_TONES,      /* it plays as data tones; then 0 again, # the menu */
    ST_SETTINGS,   /* the unit of the amounts, About */
    ST_WIPE_CONFIRM /* a hook flash wipes; * or # back to wipe_back */
};

/* heavy work, run from a tick */
enum { JOB_NONE = 0, JOB_PIN, JOB_DERIVE, JOB_SIGN, JOB_ACCOUNT };

/* What goes out as tones. */
enum { OUT_NONE = 0, OUT_SIGNED, OUT_ACCOUNT };

/* The signed PSBT's echo, one step per take (as ds_app's). */
enum { SIG_NONE = 0, SIG_BEGIN, SIG_CHUNKS, SIG_END };

#define STEPS_DONE (-1)
#define STEP_WAIT (-1)   /* say_step: nothing to queue yet, ask again */
#define STEP_FAIL (-2)   /* say_step: the step could not be queued whole */
#define SAT_PER_BTC 100000000u
#define FRAME_GAP_MS 500u    /* silence after each part (a group ends at 300) */
#define ADDR_ENDS 5          /* the address's first and last characters said */
#define KEY_QUIET_RUNS 3 /* detections (10 ms each) without a key: it is over */
#define KEY_RUNS 7       /* a key counts once it has lasted 70 ms */
#define KEY_GAP_RUNS 5   /* a key is over after 50 ms without it */
#define ONSET_RUNS 2     /* a tone heard 20 ms has begun */
#define DATA_ONSET_RUNS 25 /* a different tone begun within 250 ms: data */
#define DATA_QUIET_RUNS 70 /* data is over after 700 ms without a tone */
#define DATA_GAP_RUNS 15 /* ... after 150 ms or less of silence (SPEC 1.1) */
#define REPEAT_RUNS 150  /* the same key again within 1.5 s: multi-tap */
#define GATE_QUIET_RUNS 100 /* after data: no key until 1000 ms without a tone */
#define SENTENCE_GAP_MS 300u /* between the sentences of a prompt */
#define ENTRY_GAP_MS 500u    /* between the entries of a list */
#define OPTION_GAP_MS 150u   /* after each option of a menu */
/* A seed by the handset: one crypto-seed frame at air pace (80 ms tone,
 * 80 ms gap, 600 ms pauses: SPEC 1.1) is 120 tones for 16 bytes, 156 for 32,
 * that is 20 to 26 s. */
#define SEED_AIR_S 25u
#define AIR_TONE_MS (UT_AIR_TONE_MS + UT_AIR_GAP_MS)
#define AIR_PAUSE_MS 600u
/* The tones of a multi-part UR's part carrying a fragment of f bytes, about
 * (2 per byte, plus its header, the type and Reed-Solomon's parity). */
#define PART_TONES(f) (2u * (f) + 120u)

static const char *state_name(int st)
{
    switch (st) {
    case ST_IDLE: return "idle";
    case ST_LANG: return "lang";
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
    case ST_PSBT_RX: return "psbt-listen";
    case ST_REVIEW: return "review";
    case ST_CONFIRM: return "confirm";
    case ST_HOLD_NEAR: return "hold-near";
    case ST_TONES: return "tones";
    case ST_SETTINGS: return "settings";
    case ST_WIPE_CONFIRM: return "wipe-confirm";
    default: return "?";
    }
}

/* Where # after "resume_ask" goes back to when a hang-up left st (ST_IDLE:
 * nowhere; the states before the menu just start again). A hang-up wipes
 * what was being typed or received, so an input is not resumed: its
 * question is asked again (how to load the seed; the menu, where 1 plays
 * the PSBT again: its review is cancelled, the PSBT it reviewed is gone).
 * A reception started by mistake is left only by hanging up, so it must
 * never resume into itself. What goes out waits to be played again. */
static int resume_point(int st)
{
    switch (st) {
    case ST_MENU_EMPTY:
    case ST_ABOUT:
    case ST_SEED_HOW:
    case ST_FINGERPRINT:
    case ST_MENU_READY:
    case ST_SETTINGS:
    case ST_HOLD_NEAR:
        return st;
    case ST_TONES:
        return ST_HOLD_NEAR;
    case ST_PSBT_RX:
    case ST_REVIEW:
    case ST_CONFIRM:
        return ST_MENU_READY;
    case ST_SEED_AIR:
    case ST_PIN:
    case ST_KEYPAD:
    case ST_KEYPAD_PIN:
        return ST_SEED_HOW;
    default:
        return ST_IDLE;
    }
}

/* The inputs: their prompt is not said again by itself (the silence of a
 * reception or of someone typing), and keys there are data. */
static int input(int st)
{
    return st == ST_SEED_AIR || st == ST_PIN || st == ST_KEYPAD || st == ST_KEYPAD_PIN ||
           st == ST_PSBT_RX;
}

/* The states that give the help after DS_VOICE_HELP_MS of silence (not the
 * tones: 0 plays them again when asked). */
static int helps(int st)
{
    return st != ST_IDLE && st != ST_TONES && !input(st);
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

/* The signed PSBT's echo stops when its bytes are zeroed before it ended. */
static void echo_signed_abort(ds_voice *v)
{
    char l[40];
    if (v->sig_phase != SIG_NONE) {
        snprintf(l, sizeof l, "signed-psbt-abort %u", (unsigned)v->sig_id);
        echo_line(v, l);
        v->sig_phase = SIG_NONE;
        v->sig_pos = 0;
    }
}

/* The next line of the signed PSBT's echo (cap >= DS_VOICE_ECHO_MAX): the
 * PSBT is the end of the CBOR in v->out. */
static int32_t echo_signed_next(ds_voice *v, char *l, size_t cap)
{
    const uint8_t *psbt = v->out + (v->cbor_len - v->out_len);
    int32_t r;
    size_t n;

    switch (v->sig_phase) {
    case SIG_BEGIN:
        v->sig_phase = SIG_CHUNKS;
        v->sig_pos = 0;
        return (int32_t)snprintf(l, cap, "signed-psbt-begin %u %u", (unsigned)v->sig_id,
                                 (unsigned)v->out_len);
    case SIG_CHUNKS:
        n = v->out_len - v->sig_pos;
        if (n > DS_VOICE_ECHO_CHUNK) {
            n = DS_VOICE_ECHO_CHUNK;
        }
        memcpy(l, "signed-psbt ", 12);
        r = ds_base64_encode(psbt + v->sig_pos, n, l + 12, cap - 12);
        v->sig_pos += n;
        if (v->sig_pos >= v->out_len) {
            v->sig_phase = SIG_END;
        }
        return r < 0 ? r : 12 + r;
    case SIG_END:
        v->sig_phase = SIG_NONE;
        v->sig_pos = 0;
        return (int32_t)snprintf(l, cap, "signed-psbt-end %u", (unsigned)v->sig_id);
    default:
        return 0;
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
        return echo_signed_next(v, out, cap);
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

/* The longer version of phrase id, or DS_PACK_NONE if it has none. */
static uint16_t long_of(uint16_t id)
{
    return id < DS_P_COUNT ? ds_prompt_long[id] : DS_PACK_NONE;
}

/* Phrase id, in its longer version if it has one in the voice it is queued
 * in (a language may leave it empty). */
static void long_clip(ds_voice *v, uint16_t id)
{
    const uint8_t *frames;
    uint32_t bytes = 0;
    uint16_t l = long_of(id);
    if (l != DS_PACK_NONE && v->say.pack != NULL &&
        ds_pack_clip(v->say.pack, l, &frames, &bytes) == DS_OK && bytes > 0) {
        id = l;
    }
    clip(v, id);
}

/* A phrase of the state's prompt: the longer version once the help was given. */
static void ask(ds_voice *v, uint16_t id)
{
    if (v->longer) {
        long_clip(v, id);
    } else {
        clip(v, id);
    }
}

/* A pack's fingerprint (DS_PACK_FP bytes) as hex. */
static void hex_fp(const uint8_t b[DS_PACK_FP], char out[2 * DS_PACK_FP + 1])
{
    for (int i = 0; i < DS_PACK_FP; i++) {
        snprintf(out + 2 * i, 3, "%02x", b[i]);
    }
}

/* An amount as the setting says (design 5.7): automatic says 1 bitcoin and
 * more in bitcoin, less in sats. In bitcoin: the whole part as a number,
 * "point" and each decimal digit (trailing zeros dropped; none: no point),
 * "bitcoin". In sats: the number, "sats". */
static int32_t say_amount(ds_voice *v, uint64_t sat)
{
    int btc = v->unit == DS_VOICE_UNIT_BTC ||
              (v->unit == DS_VOICE_UNIT_AUTO && sat >= SAT_PER_BTC);
    uint32_t frac = (uint32_t)(sat % SAT_PER_BTC);
    int32_t r;
    char d[9];
    int n;

    if (!btc) {
        r = ds_say_number(&v->say, sat);
        return r < 0 ? r : ds_say_clip(&v->say, DS_P_SATS);
    }
    r = ds_say_number(&v->say, sat / SAT_PER_BTC);
    if (r >= 0 && frac > 0) {
        snprintf(d, sizeof d, "%08u", (unsigned)frac);
        for (n = 8; d[n - 1] == '0'; n--) {
        }
        r = ds_say_clip(&v->say, DS_P_POINT);
        for (int i = 0; i < n && r >= 0; i++) {
            r = ds_say_number(&v->say, (uint64_t)(d[i] - '0'));
        }
    }
    return r < 0 ? r : ds_say_clip(&v->say, DS_P_BITCOIN);
}

int32_t ds_voice_say_amount(ds_voice *v, uint64_t sat)
{
    int32_t r = say_amount(v, sat);
    echo_said(v);
    return r < 0 ? DS_ERR_SPACE : DS_OK;
}

/* The address of output i of the review into addr; its length, or 0 when
 * the script has none (as ds_review: "Script, N bytes"). */
static int32_t out_address(const ds_voice *v, uint32_t i, char *addr, size_t cap)
{
    const ds_out *o = &v->s.r.out[i];
    int32_t n = 0;
    if (o->full_len <= DS_TX_SCRIPT_KEEP) {
        n = ds_address(o->script, o->full_len, v->s.r.testnet, addr, cap);
    }
    return n > 0 ? n : 0;
}

/* Inputs that will not be signed: not ours (ownership checked and failed,
 * or no derivation of ours), and ours by derivation but with no
 * witness_utxo (ownership not checked). */
static void unsigned_inputs(const ds_psbt_review *r, uint32_t *foreign, uint32_t *no_witness)
{
    *foreign = *no_witness = 0;
    for (uint32_t k = 0; k < r->n_in; k++) {
        if (r->in[k].ours) {
            continue;
        }
        if (r->in[k].no_witness) {
            (*no_witness)++;
        } else {
            (*foreign)++;
        }
    }
}

/* The review's items: the outputs, the fee, and the inputs' warnings when
 * some input will not be signed. */
static uint32_t review_items(const ds_voice *v)
{
    uint32_t f, nw;
    unsigned_inputs(&v->s.r, &f, &nw);
    return v->s.r.n_out + 1 + (f + nw > 0 ? 1 : 0);
}

/* Step k of the review's current item: an output ("you send" the amount
 * "to the address starting with" its 5 first characters "and ending with"
 * its 5 last, or after 5 "to the address in full, in groups of four"; change:
 * "change" and the amount), the fee ("high fee" when warned), or
 * the inputs that will not be signed (the warn sound, then how many are not
 * ours; and that one of ours has no amount attached). 1 when queued, 0 when
 * the item is over, STEP_FAIL when any part of it could not be queued (it
 * must never be heard in part). */
static int review_step(ds_voice *v, int k)
{
    const ds_psbt_review *r = &v->s.r;
    char addr[96], end[ADDR_ENDS + 1];
    int32_t n;
    uint32_t foreign, no_witness;
    int bad = 0;

    if (v->item > r->n_out) { /* the inputs' warnings */
        if (k > 0) {
            return 0;
        }
        unsigned_inputs(r, &foreign, &no_witness);
        if (foreign > 0) {
            bad |= ds_say_earcon(&v->say, DS_EAR_NOTICE) < 0;
            bad |= ds_say_clip(&v->say, DS_P_WARN_INPUTS_UNSIGNED) < 0;
            bad |= ds_say_number(&v->say, foreign) < 0;
        }
        if (no_witness > 0) {
            if (foreign > 0) {
                bad |= ds_say_pause(&v->say, SENTENCE_GAP_MS) < 0;
            }
            bad |= ds_say_earcon(&v->say, DS_EAR_NOTICE) < 0;
            bad |= ds_say_clip(&v->say, DS_P_WARN_INPUT_NO_WITNESS) < 0;
        }
        return bad ? STEP_FAIL : 1;
    }
    if (v->item == r->n_out) { /* the fee */
        if (k > 0) {
            return 0;
        }
        bad |= ds_say_clip(&v->say, DS_P_FEE) < 0;
        bad |= say_amount(v, r->fee) < 0;
        if (r->fee_warning) {
            bad |= ds_say_pause(&v->say, SENTENCE_GAP_MS) < 0;
            bad |= ds_say_clip(&v->say, DS_P_FEE_HIGH) < 0;
        }
        return bad ? STEP_FAIL : 1;
    }
    if (k == 0) {
        if (r->out[v->item].kind == DS_OUT_CHANGE) {
            bad |= ds_say_clip(&v->say, DS_P_CHANGE) < 0;
            bad |= say_amount(v, r->out[v->item].amount) < 0;
        } else {
            bad |= ds_say_clip(&v->say, DS_P_SENDING) < 0;
            bad |= say_amount(v, r->out[v->item].amount) < 0;
        }
        return bad ? STEP_FAIL : 1;
    }
    if (k > 1 || r->out[v->item].kind == DS_OUT_CHANGE) {
        return 0;
    }
    n = out_address(v, v->item, addr, sizeof addr);
    if (n == 0) { /* no address: "to" "script" and its length */
        bad |= ds_say_clip(&v->say, DS_P_TO) < 0;
        bad |= ds_say_chars(&v->say, "script") < 0;
        bad |= ds_say_number(&v->say, r->out[v->item].full_len) < 0;
    } else if (v->whole) {
        bad |= ds_say_clip(&v->say, DS_P_ADDRESS_FULL_GROUPS) < 0;
        bad |= ds_say_chars_grouped(&v->say, addr) < 0;
    } else {
        memcpy(end, addr, ADDR_ENDS);
        end[ADDR_ENDS] = '\0';
        bad |= ds_say_clip(&v->say, DS_P_ADDRESS_STARTS) < 0;
        bad |= ds_say_chars(&v->say, end) < 0;
        bad |= ds_say_pause(&v->say, SENTENCE_GAP_MS) < 0;
        bad |= ds_say_clip(&v->say, DS_P_ADDRESS_ENDS) < 0;
        bad |= ds_say_chars(&v->say, addr + n - ADDR_ENDS) < 0;
    }
    ds_wipe(addr, sizeof addr);
    return bad ? STEP_FAIL : 1;
}

/* Queues step v->step of the state's prompt; 0 when the prompt is over,
 * STEP_WAIT when the step is not ready yet. Called only while nothing is
 * being said. */
static void listen_afresh(ds_voice *v);
static void close_gate(ds_voice *v);
static void hush(ds_voice *v);
static void redial_once(ds_voice *v);

/* The network, as it is said when it matters (the seed loaded, a review
 * begun, the account sent; never in a menu). */
static uint16_t net_phrase(const ds_voice *v)
{
    return v->testnet ? DS_P_NET_TEST_SAY : DS_P_NET_MAIN_SAY;
}

/* A duration, in seconds rounded to 5 (5 at least). */
static uint32_t round5_s(uint32_t ms)
{
    uint32_t s = (ms + 2500u) / 5000u * 5u;
    return s < 5u ? 5u : s;
}

/* "about N seconds" after the phrase that leads it (receive_about,
 * send_about). */
static void say_about(ds_voice *v, uint16_t lead, uint32_t seconds)
{
    clip(v, lead);
    ds_say_number(&v->say, seconds);
    clip(v, DS_P_SECONDS);
}

/* A menu's head, the first step of its prompt: the notice motif, its title
 * (DS_PACK_NONE: none) and what else says where you are, "choose an option". */
static void menu_head(ds_voice *v, uint16_t title, uint16_t now)
{
    ds_say_earcon(&v->say, DS_EAR_NOTICE);
    if (title != DS_PACK_NONE) {
        ask(v, title);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
    }
    if (now != DS_PACK_NONE) {
        ask(v, now);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
    }
    ask(v, DS_P_CHOOSE_OPTION);
}

/* Step k of a menu (head as menu_head says): k 0 the head, then each of its
 * n options as "number, action" after the option motif; 0 when over. */
static int menu_step(ds_voice *v, int k, uint16_t title, uint16_t now, const uint16_t *opt, int n)
{
    if (k == 0) {
        menu_head(v, title, now);
    } else if (k <= n) {
        ds_say_earcon(&v->say, DS_EAR_OPTION);
        ask(v, opt[k - 1]);
        ds_say_pause(&v->say, OPTION_GAP_MS);
    } else {
        return 0;
    }
    return 1;
}

/* What goes out, as tones, takes this long: each part at cable pace (its
 * tones, two pauses) and the silence after it. All parts are about as long
 * as the first, made already. */
static uint32_t out_ms(const ds_voice *v)
{
    uint32_t part = (uint32_t)strlen(v->tones[0]) * (UT_CABLE_TONE_MS + UT_CABLE_GAP_MS) +
                    2u * DS_SAY_TONES_PAUSE_MS + FRAME_GAP_MS;
    return v->out_total * part;
}

static int say_step(ds_voice *v)
{
    int k = v->step;
    char fp[2 * DS_PACK_FP + 1];
    static const uint16_t empty_main[] = {DS_P_OPT_LOAD_SEED, DS_P_OPT_NET_TEST, DS_P_OPT_ABOUT};
    static const uint16_t empty_test[] = {DS_P_OPT_LOAD_SEED, DS_P_OPT_NET_MAIN, DS_P_OPT_ABOUT};
    static const uint16_t ready[] = {DS_P_OPT_SIGN, DS_P_OPT_ACCOUNT, DS_P_OPT_WIPE,
                                     DS_P_OPT_SETTINGS};
    static const uint16_t seed_how[] = {DS_P_OPT_BY_HANDSET, DS_P_OPT_BY_KEYPAD, DS_P_OPT_BACK};
    static const uint16_t resume[] = {DS_P_OPT_RESUME, DS_P_OPT_START_OVER};
    static const uint16_t settings[] = {DS_P_OPT_UNIT_AUTO, DS_P_OPT_UNIT_BTC, DS_P_OPT_UNIT_SATS,
                                        DS_P_OPT_ABOUT, DS_P_OPT_BACK};
#define MENU(title, now, o) menu_step(v, k, (title), (now), (o), (int)(sizeof(o) / sizeof((o)[0])))

    switch (v->state) {
    case ST_LANG:
        /* each language in its own voice: "For English, press 1", ... */
        if (k >= v->packs.n) {
            ds_say_set_pack(&v->say, voice(v));
            return 0;
        }
        ds_say_set_pack(&v->say, &v->packs.pack[k]);
        ask(v, DS_P_LANG_PRESS);
        ask(v, (uint16_t)(DS_P_C_1 + k));
        ds_say_pause(&v->say, ENTRY_GAP_MS);
        break;
    case ST_MENU_EMPTY:
        if (!(v->testnet ? MENU(DS_PACK_NONE, DS_PACK_NONE, empty_test)
                         : MENU(DS_PACK_NONE, DS_PACK_NONE, empty_main))) {
            return 0;
        }
        break;
    case ST_ABOUT:
        /* the firmware's version, then each pack: its language, its
         * fingerprint; then the way back */
        if (k == 0) {
            ds_say_earcon(&v->say, DS_EAR_NOTICE);
            ask(v, DS_P_ABOUT_IS);
            ds_say_chars(&v->say, DS_VERSION);
        } else if (k <= v->packs.n) {
            ask(v, DS_P_ABOUT_VOICE);
            ds_say_chars(&v->say, v->packs.pack[k - 1].lang);
            ds_say_pause(&v->say, SENTENCE_GAP_MS);
            hex_fp(v->packs.fp[k - 1], fp);
            ds_say_chars_grouped(&v->say, fp);
        } else if (k == v->packs.n + 1) {
            ds_say_earcon(&v->say, DS_EAR_OPTION);
            ask(v, DS_P_OPT_BACK);
        } else {
            return 0;
        }
        ds_say_pause(&v->say, ENTRY_GAP_MS);
        break;
    case ST_RESUME_ASK:
        if (!MENU(DS_P_RESUME_ASK, DS_PACK_NONE, resume)) {
            return 0;
        }
        break;
    case ST_SEED_HOW:
        if (!MENU(DS_P_SEED_HOW, DS_PACK_NONE, seed_how)) {
            return 0;
        }
        break;
    case ST_SEED_AIR:
        /* where, the warning (once per power-up), how long, what to do if
         * it cuts, then the "receive" motif: nothing more is said until the
         * seed has been heard (half-duplex) */
        if (k > 0) {
            return 0;
        }
        ask(v, DS_P_SEED_LISTENING);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        if (!v->air_warned) {
            v->air_warned = 1;
            ask(v, DS_P_AIR_WARNING);
            ds_say_pause(&v->say, SENTENCE_GAP_MS);
        }
        say_about(v, DS_P_RECEIVE_ABOUT, SEED_AIR_S);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        ask(v, DS_P_IF_CUT_RESET);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        ds_say_earcon(&v->say, DS_EAR_RECEIVE);
        break;
    case ST_PIN:
        if (k > 0) {
            return 0;
        }
        ask(v, DS_P_PIN_ASK);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        ask(v, DS_P_PIN_WARNING);
        break;
    case ST_KEYPAD:
        if (k > 0) {
            return 0;
        }
        if (!v->keypad_warned) { /* once per power-up: the keys and the PIN */
            v->keypad_warned = 1;
            ask(v, DS_P_PIN_WARNING);
            ds_say_pause(&v->say, SENTENCE_GAP_MS);
        }
        ask(v, DS_P_KEYPAD_ASK);
        break;
    case ST_KEYPAD_PIN:
        if (k > 0) {
            return 0;
        }
        ask(v, DS_P_KEYPAD_PIN_ASK);
        break;
    case ST_FINGERPRINT:
        if (k == 1 && !v->redial_said) {
            /* then the redial note, once per call: a PIN, or a seed by
             * the keypad, was typed */
            ds_say_pause(&v->say, SENTENCE_GAP_MS);
            redial_once(v);
            break;
        }
        if (k > 0) {
            return 0;
        }
        snprintf(fp, sizeof fp, "%08x", (unsigned)v->seed_fp);
        ask(v, DS_P_FINGERPRINT_IS);
        ds_say_chars_grouped(&v->say, fp);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        ask(v, DS_P_IF_CORRECT_HASH);
        break;
    case ST_MENU_READY:
        if (!MENU(DS_PACK_NONE, DS_PACK_NONE, ready)) {
            return 0;
        }
        break;
    case ST_PSBT_RX:
        /* then nothing is said until the first part brings the total
         * (half-duplex) */
        if (k > 0) {
            return 0;
        }
        ask(v, DS_P_PSBT_LISTENING);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        ask(v, DS_P_IF_CUT_RESET);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        ds_say_earcon(&v->say, DS_EAR_RECEIVE);
        break;
    case ST_REVIEW:
        switch (review_step(v, k)) {
        case 0:
            return 0;
        case STEP_FAIL:
            /* never heard in part: none of it, the error instead */
            hush(v);
            clip(v, DS_P_ERROR_GENERIC);
            echo_said(v);
            return STEP_FAIL;
        default:
            break;
        }
        break;
    case ST_CONFIRM:
        if (k > 0) {
            return 0;
        }
        ask(v, DS_P_SIGN_HOOK);
        break;
    case ST_WIPE_CONFIRM:
        if (k > 0) {
            return 0;
        }
        ask(v, DS_P_WIPE_HOOK);
        break;
    case ST_HOLD_NEAR:
        /* what is ready (the account: on which network), then what to do */
        if (k > 0) {
            return 0;
        }
        if (v->out_kind == OUT_SIGNED) {
            ask(v, DS_P_SIGNED);
        } else {
            ask(v, DS_P_ACCOUNT_READY);
            ask(v, net_phrase(v));
        }
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
        ask(v, DS_P_HOLD_NEAR);
        break;
    case ST_TONES:
        /* how long it takes, once the first part is made (and the first
         * time, what to do if it cuts), the "send" motif; then each part as
         * it is made (in a tick), a silence after it; then the hint, and
         * keys are heard again */
        if (k == 0) {
            if (v->out_made == 0 && v->out_total > 0) {
                return STEP_WAIT;
            }
            say_about(v, DS_P_SEND_ABOUT, round5_s(out_ms(v)));
            ds_say_pause(&v->say, SENTENCE_GAP_MS);
            if (!v->out_reset_said) {
                v->out_reset_said = 1;
                ask(v, DS_P_IF_CUT_RESET);
                ds_say_pause(&v->say, SENTENCE_GAP_MS);
            }
            ds_say_earcon(&v->say, DS_EAR_SEND);
        } else if (v->out_sent < v->out_total) {
            if (v->out_made <= v->out_sent) {
                return STEP_WAIT;
            }
            ds_say_tones(&v->say, v->tones[v->out_sent % 2]);
            ds_say_pause(&v->say, FRAME_GAP_MS);
            v->out_sent++;
        } else if (!v->out_heard) {
            close_gate(v); /* nothing heard during the tones counts */
            v->out_heard = 1;
            ask(v, DS_P_AGAIN_HINT);
        } else {
            return 0;
        }
        break;
    case ST_SETTINGS:
        if (!MENU(DS_P_SETTINGS,
                  v->unit == DS_VOICE_UNIT_BTC    ? DS_P_UNIT_BTC
                  : v->unit == DS_VOICE_UNIT_SATS ? DS_P_UNIT_SATS
                                                  : DS_P_UNIT_AUTO,
                  settings)) {
            return 0;
        }
        break;
    default:
        return 0;
    }
#undef MENU
    echo_said(v);
    v->step++;
    return 1;
}

/* Every step of the state's prompt has been played. A review item: it has
 * been heard (and after the first, once per review, its keys are named).
 * The confirmation and the wipe: a flash counts from now on. */
static void prompt_said(ds_voice *v)
{
    if (v->state == ST_REVIEW) {
        v->item_heard = 1;
        if (v->item == v->items_heard) {
            v->items_heard++;
        }
        if (!v->keys_said) {
            v->keys_said = 1;
            ds_say_pause(&v->say, SENTENCE_GAP_MS);
            clip(v, DS_P_REVIEW_KEYS);
            echo_said(v);
        }
    } else if ((v->state == ST_CONFIRM || v->state == ST_WIPE_CONFIRM) && !v->prompt_heard) {
        v->prompt_heard = 1;
        v->confirm_at = v->now;
    }
}

/* Queues the next steps while nothing is being said. */
static void pump(ds_voice *v)
{
    while (v->step != STEPS_DONE && !ds_say_busy(&v->say)) {
        int r = say_step(v);
        if (r == STEP_WAIT || r == STEP_FAIL) {
            break;
        }
        if (r == 0) {
            v->step = STEPS_DONE;
            prompt_said(v);
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

/* A new request (a state, a review item): its prompt short, its own help. */
static void new_request(ds_voice *v)
{
    v->helped = 0;
    v->longer = 0;
    v->quiet_at = v->now;
}

/* Moves to st and says its prompt once what is queued has been said. */
static void go(ds_voice *v, int st)
{
    char l[40];
    v->state = st;
    new_request(v);
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

/* The prompt once more, with its longer versions (the review: its keys,
 * longer); given once per request. */
static void help(ds_voice *v)
{
    v->helped = 1;
    if (v->state == ST_REVIEW) {
        hush(v);
        long_clip(v, DS_P_REVIEW_KEYS);
        echo_said(v);
    } else {
        v->longer = 1;
        repeat(v);
    }
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
    v->onset_key = 0;
    v->since_onset = 0;
    v->data = 0;
    v->data_quiet = 0;
    v->gate = 0;
    v->gate_quiet = 0;
    v->pending = 0;
    v->gap_before = 0;
    v->repeat = 0;
}

/* A data reception ended (a seed, a PSBT) or the signer's own tones did:
 * the listener afresh, and no key at all, in any state, until the line has
 * had no tone for 1000 ms. A wallet looping its parts pauses between them
 * at least 300 ms (ur-tones SPEC 1.1; 400 by cable and 600 by air as
 * recommended), so at those paces it keeps the gate shut; one pausing a
 * second or more can open it. A person waits longer than that before
 * pressing. */
static void close_gate(ds_voice *v)
{
    listen_afresh(v);
    v->gate = 1;
}

/* The one way a key is taken, in every state, once the gate is open (see
 * close_gate). Keys from the air cannot be fully told from a person typing:
 * these rules keep a wallet's data tones out at the paces wallets use, and
 * what is left (a device near the handset pressing keys) can cancel,
 * repeat or start an action but never sign, wipe or skip the review, which
 * need the hook or the whole review said.
 *  - A key must sound 70 ms; dropouts under 50 ms (the signer's own voice
 *    on the line can cause them) neither end it nor make it a new press.
 *  - Rhythm: a tone that begins 250 ms or less after the previous one
 *    began, is a different key, and follows 150 ms or less of silence (the
 *    longest silence inside a frame, ur-tones SPEC 1.1) marks the line as
 *    carrying data; no key counts until 700 ms without a tone. A key held
 *    long enough waits until 250 ms after it began and is dropped if data
 *    is seen meanwhile, so the first tone of a burst is not taken.
 *  - The same key again within 1.5 s (multi-tap) is never data: each of
 *    its onsets is a tap of its own, acted on at 70 ms (and a tap of it
 *    still waiting is acted on at once).
 * Called once per 10 ms slice with the key heard now; returns the key to
 * act on, or 0. */
static char held_key(ds_voice *v, char key)
{
    char out = 0;

    if (v->since_onset < UINT32_MAX) {
        v->since_onset++;
    }
    if (v->data) {
        /* data tones heard: no key until the burst is over */
        v->data_quiet = key == 0 ? v->data_quiet + 1 : 0;
        if (v->data_quiet >= DATA_QUIET_RUNS) {
            v->data = 0;
            v->data_quiet = 0;
        }
        return 0;
    }
    if (key == 0) {
        if (++v->run_gap >= KEY_GAP_RUNS) {
            v->run_key = 0;
            v->run_n = 0;
            v->held = 0;
        }
    } else {
        if (key == v->run_key) {
            v->run_n++;
        } else {
            v->gap_before = v->run_gap; /* the silence before this tone */
            v->run_key = key;
            v->run_n = 1;
        }
        v->run_gap = 0;
        if (v->run_n == ONSET_RUNS) { /* a tone heard 20 ms has begun */
            if (v->onset_key != 0 && key != v->onset_key) {
                if (v->since_onset <= DATA_ONSET_RUNS && v->gap_before <= DATA_GAP_RUNS) {
                    v->data = 1;
                    v->data_quiet = 0;
                    v->run_key = 0;
                    v->run_n = 0;
                    v->held = 0;
                    v->pending = 0;
                    v->onset_key = 0;
                    return 0;
                }
                v->repeat = 0;
            } else {
                v->repeat = v->onset_key == key && v->since_onset <= REPEAT_RUNS;
                if (v->repeat && v->pending != 0) {
                    out = v->pending; /* the tap before: no data came */
                    v->pending = 0;
                }
            }
            v->onset_key = key;
            v->since_onset = 0;
        }
        if (v->run_key != v->held && v->run_n >= KEY_RUNS) {
            v->held = v->run_key;
            if (v->repeat && out == 0) {
                out = v->held;
            } else {
                v->pending = v->held;
            }
        }
    }
    if (out == 0 && v->pending != 0 && v->since_onset > DATA_ONSET_RUNS) {
        out = v->pending;
        v->pending = 0;
    }
    return out;
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
    v->rx_told = 0;
}

/* What goes out (the signed PSBT, the account) to zero, with its parts and
 * tones; its echo, if not over, says it was aborted. */
static void clear_out(ds_voice *v)
{
    echo_signed_abort(v);
    ds_wipe(v->out, sizeof v->out);
    ds_wipe(&v->enc, sizeof v->enc);
    ds_wipe(&v->frame, sizeof v->frame);
    ds_wipe(v->tones, sizeof v->tones);
    v->cbor_len = 0;
    v->out_len = 0;
    v->out_kind = OUT_NONE;
    v->out_total = v->out_made = v->out_sent = 0;
    v->out_heard = 0;
    v->out_reset_said = 0;
}

/* The review ends: cancelled (nothing signed) unless it was signed; the
 * PSBT received goes with the receiver. */
static void clear_review(ds_voice *v)
{
    ds_session_cancel(&v->s); /* a no-op in READY */
    ds_rx_wipe(&v->rx);
    v->rx_keys = 0;
    v->psbt = NULL;
    v->psbt_len = 0;
    v->item = 0;
    v->whole = 0;
    v->item_heard = 0;
    v->items_heard = 0;
}

/* "Wipe and start over": the session goes, the language chosen for this
 * power-up and the unit of the amounts stay. */
static void wipe(ds_voice *v)
{
    drop_input(v);
    clear_review(v);
    clear_out(v);
    ds_session_wipe(&v->s);
    v->seed_fp = 0;
    v->testnet = 0;
    v->resume_to = ST_IDLE;
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_CLOSE);
    clip(v, DS_P_WIPED);
    echo_said(v);
    go(v, ST_MENU_EMPTY);
}

/* The redial note, the first time in this call: a seed or a PIN typed may
 * be in the telephone's memory. Queued after what is queued. */
static void redial_once(ds_voice *v)
{
    if (!v->redial_said) {
        v->redial_said = 1;
        clip(v, DS_P_REDIAL_NOTE);
        ds_say_pause(&v->say, SENTENCE_GAP_MS);
    }
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
    close_gate(v); /* a wallet may still be playing the seed */
    ds_multitap_init(&v->mt);
    enter(v, st);
}

/* Loading failed (phrase: why) or was refused: nothing stays; the question
 * again. */
static void seed_failed(ds_voice *v, uint16_t phrase)
{
    drop_input(v);
    ds_session_wipe(&v->s);
    v->seed_fp = 0;
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_FAIL);
    clip(v, phrase);
    ds_say_pause(&v->say, SENTENCE_GAP_MS);
    redial_once(v); /* a PIN, or a seed by the keypad, was typed */
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
        seed_failed(v, DS_P_SEED_REFUSED); /* refused before it is even decrypted */
        return;
    }
    n = ds_rx_seed(&v->rx, encrypted ? v->mt.text : NULL, entropy);
    drop_input(v);
    if (n < 0) {
        ds_wipe(entropy, sizeof entropy);
        seed_failed(v, DS_P_SEED_FAILED);
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
        seed_failed(v, DS_P_SEED_FAILED);
        return;
    }
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_OK);
    clip(v, DS_P_SEED_LOADED);
    clip(v, net_phrase(v));
    ds_say_pause(&v->say, SENTENCE_GAP_MS);
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
    ds_say_earcon(&v->say, DS_EAR_OPTION);
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

/* ---- the review, the signature, the account ---- */

/* The error phrase for a review that failed. */
static uint16_t error_phrase(int32_t r)
{
    switch (r) {
    case DS_ERR_NETWORK: return DS_P_ERROR_NETWORK;
    case DS_ERR_CHANGE: return DS_P_ERROR_CHANGE;
    case DS_ERR_PSBT:
    case DS_ERR_UNSUPPORTED:
    case DS_ERR_AMOUNTS:
    case DS_ERR_SIGHASH: return DS_P_ERROR_PSBT;
    default: return DS_P_ERROR_GENERIC;
    }
}

/* Nothing is signed: the fail sound, what went wrong, the menu. */
static void sign_failed(ds_voice *v, uint16_t phrase)
{
    clear_review(v);
    clear_out(v);
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_FAIL);
    clip(v, phrase);
    echo_said(v);
    go(v, ST_MENU_READY);
}

static int32_t psbt_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    const ds_voice *v = ctx;
    if ((size_t)off > v->psbt_len || n > v->psbt_len - off) {
        return DS_ERR_PSBT;
    }
    memcpy(buf, v->psbt + off, n);
    return (int32_t)n;
}

/* The signed PSBT goes 8 bytes into v->out, room for its CBOR header. */
static int32_t out_write(void *ctx, const uint8_t *p, size_t n)
{
    ds_voice *v = ctx;
    if (n > sizeof v->out - 8 - v->out_len) {
        return DS_ERR_SPACE;
    }
    memcpy(v->out + 8 + v->out_len, p, n);
    v->out_len += n;
    return (int32_t)n;
}

static ds_source psbt_source(ds_voice *v)
{
    ds_source src;
    src.read = psbt_read;
    src.ctx = v;
    src.len = (uint32_t)v->psbt_len;
    return src;
}

/* 1 if a signed PSBT of psbt_len bytes with n_ours inputs signed is sure to
 * fit v->out as CBOR and go out in at most UT_MAX_PARTS parts (as ds_app's
 * check for its QR): the signer never signs what it cannot give back. */
static int fits_out(size_t psbt_len, uint32_t n_ours)
{
    size_t bound;
    if (psbt_len > DS_APP_PSBT_MAX || n_ours > DS_MAX_INPUTS) {
        return 0;
    }
    bound = psbt_len + (size_t)n_ours * 112 + 8;
    return bound <= DS_VOICE_OUT_MAX &&
           (bound + DS_VOICE_FRAGMENT - 1) / DS_VOICE_FRAGMENT <= UT_MAX_PARTS;
}

/* The whole PSBT was heard (from a tick): analyse it, then say its first
 * item. Any error: nothing to sign. */
static void review(ds_voice *v)
{
    const uint8_t *p = NULL;
    size_t len = 0;
    ds_source src;
    int32_t r;

    close_gate(v); /* the reception is over: nothing heard during it counts */
    r = ds_rx_psbt(&v->rx, &p, &len);
    if (r != DS_OK || len > DS_APP_PSBT_MAX) {
        sign_failed(v, DS_P_ERROR_PSBT);
        return;
    }
    v->psbt = p;
    v->psbt_len = len;
    src = psbt_source(v);
    r = ds_session_review(&v->s, &src, DS_AIR);
    if (r != DS_OK) {
        sign_failed(v, error_phrase(r));
        return;
    }
    if (v->s.r.n_ours == 0 || !fits_out(len, v->s.r.n_ours)) {
        sign_failed(v, DS_P_ERROR_GENERIC); /* nothing of ours, or too big */
        return;
    }
    v->item = 0;
    v->whole = 0;
    v->item_heard = 0;
    v->items_heard = 0;
    v->keys_said = 0;
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_OK);
    clip(v, DS_P_REVIEW_START);
    clip(v, net_phrase(v));
    ds_say_pause(&v->say, SENTENCE_GAP_MS);
    echo_said(v);
    go(v, ST_REVIEW);
}

/* What goes out is ready in v->out (cbor_len bytes): its parts, then "hold
 * the handset to your wallet". */
static void start_out(ds_voice *v, int kind)
{
    const uint8_t *cbor = v->out;
    if (ut_fountain_encoder_init(&v->enc, cbor, v->cbor_len, DS_VOICE_FRAGMENT) < 0) {
        sign_failed(v, DS_P_ERROR_GENERIC);
        return;
    }
    v->out_kind = kind;
    v->out_total = DS_VOICE_ROUNDS * v->enc.seq_len;
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_OK);
    echo_said(v);
    go(v, ST_HOLD_NEAR);
}

/* The flash confirmed (from a tick): sign, consuming the confirmation. The
 * signed PSBT becomes its CBOR in place (the header in the 8 bytes before
 * it), so one buffer holds both; the PSBT received is no longer needed. */
static void job_sign(ds_voice *v)
{
    ds_source src = psbt_source(v);
    ds_sink dst;
    int32_t r;
    size_t len;

    dst.write = out_write;
    dst.ctx = v;
    clear_out(v);
    r = ds_session_sign(&v->s, &src, &dst);
    len = v->out_len;
    if (r == DS_OK) {
        r = ut_psbt_to_cbor(v->out + 8, len, v->out, sizeof v->out);
    }
    if (r < 0) {
        sign_failed(v, DS_P_ERROR_GENERIC);
        return;
    }
    v->cbor_len = (size_t)r;
    clear_review(v);
    if (v->cbor_len < sizeof v->out) {
        ds_wipe(v->out + v->cbor_len, sizeof v->out - v->cbor_len); /* the old tail */
    }
    v->sig_id++;
    v->sig_phase = SIG_BEGIN;
    v->sig_pos = 0;
    start_out(v, OUT_SIGNED);
}

/* The account (from a tick): its descriptor in the echo, its CBOR out. */
static void job_account(ds_voice *v)
{
    char desc[200];
    int32_t n;

    clear_out(v);
    n = ds_wallet_account_cbor(&v->s.w, v->out, sizeof v->out);
    if (n <= 0) {
        sign_failed(v, DS_P_ERROR_GENERIC);
        return;
    }
    v->cbor_len = (size_t)n;
    if (ds_wallet_descriptor(&v->s.w, desc, sizeof desc) > 0) {
        echo_line(v, desc);
    }
    start_out(v, OUT_ACCOUNT);
}

/* The next part as tones (from a tick), into the buffer not playing: part
 * out_made is made once part out_made - 1 has been queued, so the one
 * before it, which used that buffer, has been played. */
static void make_part(ds_voice *v)
{
    const char *type = v->out_kind == OUT_SIGNED ? "crypto-psbt" : "crypto-account";
    int32_t r;

    v->enc.seq_num = v->out_made; /* the next is part out_made + 1 */
    r = ut_fountain_next_frame(&v->enc, type, &v->frame);
    if (r >= 0) {
        r = ut_frame_encode(&v->frame, v->tones[v->out_made % 2], sizeof v->tones[0]);
    }
    ds_wipe(&v->frame, sizeof v->frame);
    if (r < 0) {
        v->out_total = v->out_made; /* cannot happen: stop at what was made */
        return;
    }
    v->out_made++;
}

/* # (or 0 after them): the tones from the first part. */
static void play_out(ds_voice *v)
{
    v->out_made = v->out_sent = 0;
    v->out_heard = 0;
    enter(v, ST_TONES);
}

/* Done with what went out: zeroed; back to the menu, with the redial note
 * if this call has not had it. */
static void leave_out(ds_voice *v)
{
    clear_out(v);
    hush(v);
    redial_once(v);
    echo_said(v);
    go(v, ST_MENU_READY);
}

/* 1 when a flash may confirm this step: its prompt has been said in full,
 * and the on-hook began after that. */
static int flash_counts(const ds_voice *v)
{
    return v->job == JOB_NONE && v->prompt_heard &&
           (int32_t)(v->down_at - v->confirm_at) >= 0;
}

/* A short hang-up: in the confirmation step it confirms and asks for the
 * signature, in the wipe step it wipes (each only as flash_counts says);
 * anywhere else it is only echoed. */
static void flash(ds_voice *v)
{
    echo_line(v, "hook-flash");
    if (v->state == ST_WIPE_CONFIRM && flash_counts(v)) {
        wipe(v);
        return;
    }
    if (v->state == ST_CONFIRM && flash_counts(v) && v->s.state == DS_S_REVIEW &&
        ds_session_confirm(&v->s) == DS_OK) {
        ask_job(v, JOB_SIGN);
    }
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
    } else if (job == JOB_SIGN && v->state == ST_CONFIRM && v->s.state == DS_S_CONFIRMED) {
        job_sign(v);
    } else if (job == JOB_ACCOUNT && v->state == ST_MENU_READY && v->s.state == DS_S_READY) {
        job_account(v);
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
        ds_say_earcon(&v->say, DS_EAR_OPTION);
        if (m->has_pending) {
            ds_say_secret_char(&v->say, m->pending);
        }
        break;
    case DS_MT_BACK:
        hush(v);
        ds_say_earcon(&v->say, DS_EAR_CLOSE);
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

/* How long the rest of the PSBT takes, once its first part has told how
 * many fragments it has and how big: the parts still missing, one more for
 * the one played while this is said, each at air pace. */
static void say_psbt_left(ds_voice *v)
{
    uint32_t have = v->rx.d.nknown, need = v->rx.d.seq_len;
    uint32_t parts = (need > have ? need - have : 0) + 1;
    uint32_t part = PART_TONES(v->rx.d.fragment) * AIR_TONE_MS + AIR_PAUSE_MS;
    say_about(v, DS_P_RECEIVE_ABOUT, round5_s(parts * part));
    echo_said(v);
}

/* The PSBT's parts (from a tick): complete, it is reviewed; anything else is
 * dropped and listening goes on. The first part says how long the rest
 * takes. */
static void poll_psbt(ds_voice *v)
{
    int32_t r;
    while ((r = ds_rx_poll(&v->rx)) != DS_RX_NONE) {
        if (r == DS_RX_DONE) {
            review(v);
            return;
        }
        if (r == DS_RX_PART && !v->rx_told) {
            v->rx_told = 1;
            say_psbt_left(v);
        }
    }
}

/* Sign (1 in the menu with a seed): a receiver for the PSBT. */
static void start_psbt(ds_voice *v)
{
    drop_input(v);
    clear_review(v);
    ds_rx_init(&v->rx, v->rate, DS_RX_PSBT);
    enter(v, ST_PSBT_RX);
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

/* The greeting: the name, then the menu at once. */
static void greet(ds_voice *v)
{
    hush(v);
    clip(v, DS_P_GREETING);
    ds_say_pause(&v->say, SENTENCE_GAP_MS);
    echo_said(v);
    go(v, ST_MENU_EMPTY);
}

/* ---- keys ---- */

/* A key asked to wipe (menu 3, "start over" after a hang-up, a seed's
 * fingerprint rejected): no key ever wipes, a hook flash does. # or *
 * goes back to where it was asked. */
static void ask_wipe(ds_voice *v)
{
    v->wipe_back = v->state;
    v->prompt_heard = 0;
    enter(v, ST_WIPE_CONFIRM);
}

/* * in the review: cancelled, nothing signed. */
static void sign_cancel(ds_voice *v)
{
    clear_review(v);
    hush(v);
    ds_say_earcon(&v->say, DS_EAR_FAIL);
    clip(v, DS_P_PSBT_CANCELLED);
    echo_said(v);
    go(v, ST_MENU_READY);
}

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
            greet(v);
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
            clip(v, net_phrase(v));
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
            } else if (st == ST_PSBT_RX) {
                start_psbt(v); /* from the start: the PSBT is played again */
            } else {
                enter(v, st);
            }
        } else if (c == '*') {
            ask_wipe(v); /* wipe and start over: the hook confirms */
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
            ask_wipe(v); /* not this seed: the hook confirms the wipe */
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_MENU_READY:
        if (c == '1') {
            start_psbt(v);
        } else if (c == '2') {
            ask_job(v, JOB_ACCOUNT);
        } else if (c == '3') {
            ask_wipe(v);
        } else if (c == '9') {
            enter(v, ST_SETTINGS);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_SETTINGS:
        if (c >= '1' && c <= '3') {
            v->unit = c == '1' ? DS_VOICE_UNIT_AUTO : c == '2' ? DS_VOICE_UNIT_BTC
                                                                : DS_VOICE_UNIT_SATS;
            hush(v);
            clip(v, c == '1' ? DS_P_UNIT_AUTO : c == '2' ? DS_P_UNIT_BTC : DS_P_UNIT_SATS);
            echo_said(v);
        } else if (c == '9') {
            v->about_back = ST_SETTINGS;
            enter(v, ST_ABOUT);
        } else if (c == '#' || c == '*') {
            enter(v, ST_MENU_READY);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_REVIEW:
        if (c == '#') {
            /* one item at a time, each once said in full (a # while it is
             * said is ignored and it goes on); the confirmation only once
             * every item has been said */
            if (!v->item_heard) {
                break;
            }
            if (v->item + 1 < review_items(v)) {
                v->item++;
                v->whole = 0;
                v->item_heard = 0;
                new_request(v);
                repeat(v);
            } else if (v->items_heard >= review_items(v)) {
                v->prompt_heard = 0;
                enter(v, ST_CONFIRM);
            }
        } else if (c == '5' && v->item < v->s.r.n_out &&
                   v->s.r.out[v->item].kind != DS_OUT_CHANGE) {
            v->whole = 1;
            repeat(v);
        } else if (c == '0') {
            repeat(v);
        } else if (c == '*') {
            sign_cancel(v);
        }
        break;
    case ST_WIPE_CONFIRM:
        /* keys never wipe: only a hook flash does */
        if (c == '*' || c == '#') {
            enter(v, v->wipe_back);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_CONFIRM:
        /* keys never confirm: only a hook flash does */
        if (c == '0') {
            repeat(v);
        } else if (c == '*') {
            sign_cancel(v);
        }
        break;
    case ST_HOLD_NEAR:
        if (c == '#') {
            play_out(v);
        } else if (c == '*') {
            leave_out(v);
        } else if (c == '0') {
            repeat(v);
        }
        break;
    case ST_TONES:
        if (c == '0') {
            play_out(v);
        } else if (c == '#' || c == '*') {
            leave_out(v);
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
    uint32_t run;
    char key;

    if (v->state == ST_SEED_AIR || v->state == ST_PSBT_RX) {
        /* half-duplex: the receiver hears only once the signer is quiet;
         * what it heard is taken in ds_voice_tick */
        if (!ds_voice_speaking(v)) {
            rx_in(v, x, n);
        }
        return;
    }
    if (v->state == ST_KEYPAD && !v->deaf) {
        if (rx_in(v, x, n) && ds_voice_speaking(v)) {
            hush(v); /* the first digit barges in */
        }
        return;
    }
    if (v->state == ST_TONES && !v->out_heard) {
        return; /* half-duplex: its own tones are not keys */
    }
    ut_listener_push(&v->l, x, n);
    (void)ut_listener_live(&v->l, k, sizeof k); /* not used: keys go by length */
    key = key_now(&v->l, &run);
    if (v->deaf) {
        if (key == 0 && run >= KEY_QUIET_RUNS) {
            v->deaf = 0;
        }
    } else if (v->gate) {
        v->gate_quiet = key == 0 ? v->gate_quiet + 1 : 0;
        if (v->gate_quiet >= GATE_QUIET_RUNS) {
            v->gate = 0;
            v->gate_quiet = 0;
        }
    } else if ((key = held_key(v, key)) != 0) {
        v->keys++;
        v->quiet_at = v->now; /* any key: the wait for the help starts again */
        on_key(v, key);
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
    v->redial_said = 0; /* a new call */
    if (v->resume_to != ST_IDLE) {
        enter(v, ST_RESUME_ASK);
    } else if (v->packs.n > 1 && !v->lang_chosen) {
        enter(v, ST_LANG);
    } else {
        greet(v);
    }
}

/* A long on-hook: the session stays, to resume at the next off-hook. */
static void hang_up(ds_voice *v)
{
    /* the wipe step resumes where it was asked (after "resume_ask": what
     * that offered still stands) */
    int st = resume_point(v->state == ST_WIPE_CONFIRM ? v->wipe_back : v->state);
    if (st != ST_IDLE) {
        v->resume_to = st;
    }
    /* what was being typed or received goes, the listener with it; a
     * review is cancelled (a sign asked for but not run is dropped too) */
    drop_input(v);
    if (v->psbt != NULL || v->s.state == DS_S_REVIEW || v->s.state == DS_S_CONFIRMED) {
        clear_review(v);
    }
    enter(v, ST_IDLE);
}

static void debounce(ds_voice *v, uint32_t now)
{
    uint32_t down;

    if (v->hook_raw == v->hook || now - v->raw_at < DS_VOICE_DEBOUNCE_MS) {
        return;
    }
    v->hook = v->hook_raw;
    v->quiet_at = now; /* the wait for the help starts again */
    if (!v->hook) {
        v->down_at = v->raw_at;
        return;
    }
    if (v->state != ST_IDLE) {
        down = v->raw_at - v->down_at;
        if (down > DS_VOICE_HANGUP_MS) {
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
        debounce(v, now_ms); /* the level that ends now, if it held long enough */
        v->hook_raw = off_hook;
        v->raw_at = now_ms;
    }
    debounce(v, now_ms);
}

void ds_voice_tick(ds_voice *v, uint32_t now_ms)
{
    v->now = now_ms;
    debounce(v, now_ms);
    /* still on-hook (not even bouncing back) past DS_VOICE_HANGUP_MS */
    if (!v->hook && !v->hook_raw && v->state != ST_IDLE &&
        now_ms - v->down_at > DS_VOICE_HANGUP_MS) {
        hang_up(v);
    }
    if (v->job != JOB_NONE && v->job_seen) {
        run_job(v);
    }
    /* what the receivers heard, and the next part out: never on the audio path */
    if (v->hook && v->job == JOB_NONE) {
        if (v->state == ST_SEED_AIR || (v->state == ST_KEYPAD && !v->deaf)) {
            poll_rx(v);
        } else if (v->state == ST_PSBT_RX) {
            poll_psbt(v);
        } else if (v->state == ST_TONES && v->out_made < v->out_total &&
                   v->out_made <= v->out_sent) {
            make_part(v);
        }
    }
    if (v->hook && v->job == JOB_NONE &&
        (v->state == ST_PIN || v->state == ST_KEYPAD_PIN)) {
        pin_said(v, ds_multitap_tick(&v->mt, now_ms)); /* a pause commits */
    }
    /* the help: counted from the end of what was said, once per request */
    if (!helps(v->state) || !v->hook || v->helped || ds_voice_speaking(v)) {
        v->quiet_at = now_ms;
    } else if (now_ms - v->quiet_at >= DS_VOICE_HELP_MS) {
        help(v);
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
