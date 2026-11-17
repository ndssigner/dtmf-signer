/* DTMF-Signer "Centralita": the screenless build, a tone telephone as the
 * whole interface. The signer powers the line, the port tells it the hook
 * (off-hook or on-hook), hands it what arrives on the line (the telephone's
 * keys and microphone, and the signer's own voice: they share one node) and
 * takes back what the signer says into it, at 16 kHz.
 *
 *   on-hook (idle) --off-hook--> [language list] --> greeting --#--> menu
 *   menu (no seed): 1 load seed, 2 network (says the new one), 9 about,
 *   0 repeat.
 *   load seed: by the handset 1, by the keypad 2 (* back).
 *     handset: the air warning (once per power-up), the "listen" sound,
 *       then the signer only
 *       listens (half-duplex) until a crypto-seed frame is heard; then its
 *       PIN by narrated multi-tap. By air the policy wants a PIN of 12
 *       characters or more (DS_AIR); otherwise it fails and asks again.
 *     keypad: "*", the digits, "#" (ur-tones keypad mode), then its PIN if
 *       it has one, "#" alone if not. Typed when asked, it counts as a cable.
 *     Then the seed's fingerprint: # loads the wallet, * wipes. A failure
 *     says "error_generic".
 *   menu (with a seed): 1 sign, 2 account, 3 wipe, 9 settings, 0 repeat.
 *   wipe (3): "to wipe, hang up for a moment": a hook flash wipes (keys
 *     never do); * or # back to the menu.
 *   sign: "listening for the transaction", the "listen" sound, then the
 *     signer only listens (half-duplex) until the whole PSBT is heard. Then
 *     the review, one item at a time: each output ("you send" the amount
 *     "to" the address's first and last 5 characters; change: "change" and
 *     the amount), then the fee ("high fee" when the core warns), then,
 *     when some input will not be signed, a warning (how many are not ours;
 *     an input of ours without its amount attached). # next (once the item
 *     has been said in full), 0 repeat, 5 the whole address in groups of 4,
 *     * cancel. After the last item, # asks for the confirmation: a hook flash (a short hang-up) signs;
 *     keys never do. Then "signed, hold the handset to your wallet": # plays
 *     the signed PSBT as ur-tones data tones (multi-part URs, 100-byte
 *     fragments, two rounds), half-duplex; then 0 plays them again, # (or *)
 *     back to the menu, with the redial note the first time in a call. An
 *     error (wrong network, invalid PSBT, change not ours...) says so and
 *     goes back to the menu: nothing is signed.
 *   account: the account (crypto-account UR) the same way, "hash to send".
 *   settings: the amounts automatic 1, in bitcoin 2, in sats 3 (until the
 *     power is cut); About 9; * or # back.
 *   Amounts: automatic says 1 bitcoin (100 000 000 sat) and more in bitcoin
 *   (the whole part, "point", each decimal digit, trailing zeros dropped,
 *   "bitcoin"), less in sats ("sats").
 *   Keys and a wallet's data tones on the same line, in every state. Keys
 *   from the air can never be fully told from typing, so what matters is
 *   structural: the review must be said in full, and signing and the wipe
 *   need the hook. The rules keep data tones out at the paces wallets use:
 *   a key must sound 70 ms; a tone that begins 250 ms or less after a
 *   different one began, after 150 ms or less of silence (the most a frame
 *   holds), marks the line as carrying data, and no key counts until
 *   700 ms without a tone; a key waits until 250 ms after it began and is
 *   dropped if data shows meanwhile; the same key again within 1.5 s
 *   (multi-tap) is never data, each tap its own. Once a seed or a PSBT has
 *   been received, or the signer's own tones have played, no key counts
 *   until the line has had no tone for 1000 ms (a wallet pauses at least
 *   300 ms between frames). A device playing tones near the handset can
 *   still press keys (cancel, repeat, start an action).
 *   PIN by multi-tap (ds_multitap): each key is said back as a secret
 *   character, the "next" sound when one is committed, "back" and the last
 *   one left after *; # ends it, * with nothing left cancels. A PIN key
 *   counts as any key does (above).
 *   The decrypt and the derivation run from ds_voice_tick, never from
 *   ds_voice_line_in: the key that asks for them gets a short sound.
 *
 * Keys are heard while the signer speaks (barge-in): a key the state acts on
 * stops what is being said and acts; any other key is ignored and the
 * prompt goes on. The prompts are notched so they never read as keys.
 * After DS_VOICE_REPEAT_MS of silence with no key the current prompt is
 * said again.
 *
 * The hook is debounced (DS_VOICE_DEBOUNCE_MS). An on-hook of
 * DS_VOICE_FLASH_MIN_MS up to DS_VOICE_HANGUP_MS (both included) is a hook
 * flash (the confirmation of a signature, only in the step that asks for it
 * and only begun after that step began); a longer one is a hang-up: the
 * session is kept (what was being typed or received is wiped: an input
 * starts again; a review is cancelled and its PSBT must be played again;
 * a signed PSBT or the account waits to be played), and the next off-hook
 * asks
 * "resume_ask": # resumes where it was (that prompt again), * wipes and goes
 * to the menu. Cutting the power (ds_voice_init again) zeroes everything.
 *
 * With one language (an empty pack region) there is no language list. With
 * more, before the greeting each language is offered in its own voice
 * ("For English, press 1", then each pack's "lang_press" and its digit); the
 * digit picks the language until the power is cut.
 *
 * The echo (ds_voice_take_echo) is public data only, one line per take:
 * "state <name>", what is said ("say <id>", "num N", "chars X", "ear ok",
 * "tones K"; a secret character only as "say pin-char"), "lang xx",
 * "hook-flash", "fingerprint xxxxxxxx", the account's descriptor,
 * "echo-dropped N", and the signed PSBT as ds_app gives it:
 *   signed-psbt-begin <id> <bytes>
 *   signed-psbt <base64 of up to 48 bytes>     (in order, as many as needed)
 *   signed-psbt-end <id>
 * or "signed-psbt-abort <id>" if it was left before the end. Never a PIN, a
 * seed or a tone group heard.
 *
 * Everything is inside the struct: no malloc, no globals. It points only at
 * the built-in English pack and the pack region (both read-only flash), and
 * into itself (the PSBT received, in its receiver). It is about 80 KB (42 KB
 * the receiver, 20 KB what goes out): declare it static, never on the
 * stack. */
#ifndef DS_VOICE_H
#define DS_VOICE_H

#include <stddef.h>
#include <stdint.h>

#include "ds_multitap.h"
#include "ds_pack.h"
#include "ds_rx.h"
#include "ds_say.h"
#include "ds_session.h"
#include "ur_tones.h"

#define DS_VOICE_RATE DS_SAY_RATE   /* line_in and line_out */
#define DS_VOICE_DEBOUNCE_MS 50u
#define DS_VOICE_FLASH_MIN_MS 200u  /* a shorter on-hook is ignored */
#define DS_VOICE_HANGUP_MS 1500u    /* longer than this, a hang-up */
#define DS_VOICE_REPEAT_MS 6000u    /* silence before a prompt is said again */
#define DS_VOICE_ECHO_BUF 1024
#define DS_VOICE_ECHO_MAX 256       /* the cap ds_voice_take_echo needs */
#define DS_VOICE_ECHO_CHUNK 48      /* signed PSBT bytes per echo line */
#define DS_VOICE_FRAGMENT 100       /* bytes per part of what goes out as tones */
#define DS_VOICE_ROUNDS 2           /* it goes out as 2 x its fragments' parts */
/* What goes out: the signed PSBT as CBOR (at most the PSBT, 112 bytes per
 * input of ours, the byte string's header) or the account's CBOR. */
#define DS_VOICE_OUT_MAX (DS_APP_PSBT_MAX + DS_MAX_INPUTS * 112 + 8)

/* How amounts are said (the setting lasts until the power is cut). */
enum { DS_VOICE_UNIT_AUTO = 0, DS_VOICE_UNIT_BTC, DS_VOICE_UNIT_SATS };

typedef struct {
    ds_packs packs;      /* slot 0: built-in English */
    ds_say say;
    ut_listener l;
    uint32_t rate;
    int lang;            /* the pack in use (index into packs) */
    int lang_chosen;     /* 1 once a digit picked it (or there is one only) */
    int state;
    int resume_to;       /* the state a hang-up left (0: none) */
    int about_back;      /* the state About returns to */
    int step;            /* the next part of the state's prompt to queue */
    int testnet;
    /* the hook */
    int hook_raw, hook;  /* the last level given; the debounced one (1: off-hook) */
    uint32_t raw_at;     /* when hook_raw last changed */
    uint32_t down_at;    /* when the debounced on-hook began */
    uint32_t now, quiet_at; /* the clock; the last time something was said */
    uint32_t keys;       /* keys heard (a count only) */
    /* loading the seed (secrets: wiped on a hang-up, a wipe, a failure) */
    ds_rx rx;            /* the seed's tones: by the handset or typed */
    uint32_t rx_keys;    /* rx.keys already counted */
    int deaf;            /* 1: no key counts until none has sounded for 30 ms */
    char held;           /* PIN: the key acted on, still held */
    char run_key;        /* PIN: the key sounding, for run_n slices of 10 ms */
    uint32_t run_n, run_gap; /* (dropouts under 50 ms bridged) */
    char onset_key;      /* the last key begun, and 10 ms slices since */
    uint32_t since_onset;
    int data;            /* data tones are sounding: no key until quiet */
    uint32_t data_quiet;
    char pending;        /* a key held, waiting 250 ms from its start (data?) */
    uint32_t gap_before; /* the silence before the tone sounding */
    int repeat;          /* it is the same key again (multi-tap) */
    int gate;            /* after data: no key until 1000 ms without a tone */
    uint32_t gate_quiet;
    int air_warned;      /* the air warning was said (once per power-up) */
    int job, job_seen;   /* heavy work asked for, run from ds_voice_tick */
    ds_multitap mt;      /* the PIN */
    ds_session s;        /* zero until a seed is loaded */
    uint32_t seed_fp;    /* the loaded seed's fingerprint (public) */
    /* the review, the signature, the account (zeroed when left) */
    const uint8_t *psbt; /* the PSBT received, inside rx.work */
    size_t psbt_len;
    uint32_t item;       /* review: the item said (the outputs, then the fee) */
    int whole;           /* review: the whole address, by 5 */
    int item_heard;      /* review: the current item was said in full */
    uint32_t items_heard;/* review: items said in full, in order, from the first */
    uint32_t confirm_at; /* when the confirmation step began */
    int unit;            /* DS_VOICE_UNIT_*: lasts until the power is cut */
    int out_kind;        /* what goes out: the signed PSBT or the account */
    uint8_t out[DS_VOICE_OUT_MAX]; /* its CBOR (the signed PSBT after a header) */
    size_t cbor_len, out_len;      /* the CBOR; the signed PSBT at its end */
    ut_fountain_encoder enc;       /* its parts */
    ut_frame frame;                /* a part being made into tones */
    char tones[2][UT_MAX_FRAME + 1]; /* the part playing, the next one */
    uint32_t out_total, out_made, out_sent; /* parts: to send, made, queued */
    int out_heard;       /* the tones are over: keys are heard again */
    int redial_said;     /* the redial note was said in this call */
    uint32_t sig_id;     /* signed PSBTs so far, for the echo */
    int sig_phase;       /* the signed PSBT's echo: where it is */
    size_t sig_pos;
    char echo[DS_VOICE_ECHO_BUF]; /* NUL-separated lines */
    size_t echo_len;
    uint32_t echo_dropped;
} ds_voice;

/* Zeroes everything, then idle (on-hook). rate: of line_in (DS_VOICE_RATE).
 * The packs: built-in English and those found in pack_region (may be NULL
 * when region_len is 0). */
void ds_voice_init(ds_voice *v, uint32_t rate, const uint8_t *pack_region,
                   uint32_t region_len);

/* The hook as the port measures it now (1: off-hook); call it on every
 * change, or as often as convenient. */
void ds_voice_hook(ds_voice *v, int off_hook, uint32_t now_ms);

/* What arrives on the line (keys, microphone, and the signer's own voice). */
void ds_voice_line_in(ds_voice *v, const int16_t *x, size_t n);

/* What the signer puts on the line: n samples at DS_VOICE_RATE (silence
 * when it has nothing to say); returns n. */
size_t ds_voice_line_out(ds_voice *v, int16_t *out, size_t n);

/* The clock (milliseconds, free-running, wrapping): the hook's timing and
 * the repeat of prompts. */
void ds_voice_tick(ds_voice *v, uint32_t now_ms);

/* The next echo line (NUL-terminated): returns its length, 0 when there is
 * none, or DS_ERR_SPACE (nothing taken) when cap is under DS_VOICE_ECHO_MAX. */
int32_t ds_voice_take_echo(ds_voice *v, char *out, size_t cap);

/* Keys heard since power-up, for the board LED: a count, never the keys. */
uint32_t ds_voice_keys(const ds_voice *v);

/* 1 while the signer is speaking (something queued or playing). */
int ds_voice_speaking(const ds_voice *v);

/* Queues an amount of sat as the unit setting says it (the review uses it;
 * public for tests). Returns DS_OK or DS_ERR_SPACE. */
int32_t ds_voice_say_amount(ds_voice *v, uint64_t sat);

#endif
