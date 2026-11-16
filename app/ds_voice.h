/* DTMF-Signer "Centralita": the screenless build, a tone telephone as the
 * whole interface. The signer powers the line, the port tells it the hook
 * (off-hook or on-hook), hands it what arrives on the line (the telephone's
 * keys and microphone, and the signer's own voice: they share one node) and
 * takes back what the signer says into it, at 16 kHz.
 *
 *   on-hook (idle) --off-hook--> [language list] --> greeting --#--> menu
 *   menu (no seed): 1 load seed, 2 network (says the new one), 9 about,
 *   0 repeat.
 *
 * Keys are heard while the signer speaks (barge-in): a key the state acts on
 * stops what is being said and acts; any other key is ignored and the
 * prompt goes on. The prompts are notched so they never read as keys.
 * After DS_VOICE_REPEAT_MS of silence with no key the current prompt is
 * said again.
 *
 * The hook is debounced (DS_VOICE_DEBOUNCE_MS). An on-hook of
 * DS_VOICE_FLASH_MIN_MS up to DS_VOICE_HANGUP_MS is a hook flash (the
 * confirmation of a signature, in the step that asks for it); a longer one
 * is a hang-up: the session is kept, and the next off-hook asks
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
 * "hook-flash", "echo-dropped N". Never a PIN, a seed or a tone group.
 *
 * Everything is inside the struct: no malloc, no globals. It points only at
 * the built-in English pack and the pack region (both read-only flash).
 * Declare it static, never on the stack. */
#ifndef DS_VOICE_H
#define DS_VOICE_H

#include <stddef.h>
#include <stdint.h>

#include "ds_pack.h"
#include "ds_say.h"
#include "ur_tones.h"

#define DS_VOICE_RATE DS_SAY_RATE   /* line_in and line_out */
#define DS_VOICE_DEBOUNCE_MS 50u
#define DS_VOICE_FLASH_MIN_MS 200u  /* a shorter on-hook is ignored */
#define DS_VOICE_HANGUP_MS 1500u    /* from here on, a hang-up */
#define DS_VOICE_REPEAT_MS 6000u    /* silence before a prompt is said again */
#define DS_VOICE_ECHO_BUF 1024
#define DS_VOICE_ECHO_MAX DS_SAY_TOKEN_MAX /* the cap ds_voice_take_echo needs */

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

#endif
