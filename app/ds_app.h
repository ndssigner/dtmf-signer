/* DTMF-Signer device logic: the screen state machine on top of the signing
 * session. The port feeds it audio, button events and a millisecond clock,
 * and takes back the screen, the contrast and the USB echo.
 *
 *   HOME --Load seed--> listening --seed--> PIN --> Working... -->
 *   Seed fingerprint --OK--> Passphrase? --> Working... --> Wallet fingerprint
 *   --> READY: Sign PSBT (listening --> Working... --> review pages --> hold
 *   OK on the last page --> Working... --> signed PSBT as QR) or Show XPUB.
 *
 * The session's invariants hold through the screens:
 *  - nothing is signed unless every page of the review has been reached and
 *    OK is held (DS_BTN_LONG) on the last page; only then ds_session_confirm.
 *    That LONG counts only after the review is armed by a DS_BTN_PRESS (of
 *    any button) delivered at least DS_APP_ARM_MS of tick time after the
 *    first ds_app_tick that followed ds_app_take_dirty handing the review
 *    screen to the port. ds_btn sends one LONG per press and a PRESS ends
 *    its press, so the LONG's press began after that release. What this
 *    guarantees: if the port draws the screen ds_app_take_dirty reports
 *    before its next tick, and feeds each event within 200 ms of the button
 *    samples it comes from (ring lag plus the 30 ms debounce), then the
 *    release that armed the review happened after the review was drawn,
 *    and so did the start of the hold that confirms. A hold of OK carried
 *    over from "Listening" or "Working...", or a tap made then, never
 *    confirms; until armed, the last page says "Tap OK, then hold OK";
 *  - before the review can be confirmed, the signed PSBT's upper bound
 *    (PSBT + 112 bytes per input of ours + 8) must fit an animated QR at
 *    1 px, or the review is cancelled ("Too big for QR"): the device never
 *    signs what it cannot give back;
 *  - the seed fingerprint is shown before the passphrase (a wrong but valid
 *    PIN gives another wallet) and LEFT there wipes everything;
 *  - the entropy, the PIN, the passphrase, the temporary wallet and the tone
 *    groups are wiped on every path; the signed PSBT and its CBOR are zeroed
 *    when the signed screen is left or the signing is cancelled or fails;
 *  - DS_BTN_WIPE on any screen (or Wipe in the menu) zeroes everything and
 *    asks for a reboot.
 *
 * Heavy work (PIN and seed decoding, the wallet derivation, the review, the
 * signing, starting a QR) runs in the ds_app_tick after the one that put
 * "Working..." on screen, so the port can draw it first. Buttons are ignored
 * while working, except DS_BTN_WIPE.
 *
 * The echo (ds_app_take_echo) is public data only, one line per take: the
 * state names ("state ready"), the timings "t pin|derive|review|sign|qr
 * <ms>", "fingerprint xxxxxxxx", the descriptor, "echo-dropped N" when the
 * queue was full, and the signed PSBT as
 *   signed-psbt-begin <id> <bytes>
 *   signed-psbt <base64 of up to 48 bytes>     (in order, as many as needed)
 *   signed-psbt-end <id>
 * or "signed-psbt-abort <id>" if the signed screen was left before the end
 * (other lines may come between these). The id counts the signed PSBTs.
 * Never a PIN, a passphrase, a seed or a tone group.
 *
 * Everything is inside the struct: no malloc, no globals, no pointers to
 * memory outside it. With the default limits (DS_APP_PSBT_MAX 16384,
 * DS_MAX_INPUTS and DS_MAX_OUTPUTS 32) a ds_app is about 96 KB (42 KB of it
 * the receiver, 20 KB each the signed PSBT and its CBOR): declare it static,
 * never on the stack. With these limits every PSBT the receiver takes fits a
 * QR once signed; the check above matters when the limits are raised. */
#ifndef DS_APP_H
#define DS_APP_H

#include <stddef.h>
#include <stdint.h>

#include "ds_qr.h"
#include "ds_rx.h"
#include "ds_session.h"
#include "ds_ui.h"

/* The largest signed PSBT: each input of ours adds at most 112 bytes (its
 * key and signature pair). */
#define DS_APP_OUT_MAX (DS_APP_PSBT_MAX + DS_MAX_INPUTS * 112)
/* Its CBOR: a byte string header of at most 8 bytes. */
#define DS_APP_CBOR_MAX (DS_APP_OUT_MAX + 8)
/* The review text: at most about 120 characters per output line and 40 per
 * input line, plus the fee and its warning. */
#define DS_APP_SUMMARY_MAX (DS_MAX_OUTPUTS * 128 + DS_MAX_INPUTS * 48 + 128)
/* Echo lines waiting to be taken (when full, the oldest are dropped and
 * "echo-dropped N" says so). */
#define DS_APP_ECHO_BUF 1024
/* The cap ds_app_take_echo needs: the longest line with its NUL. */
#define DS_APP_ECHO_MAX 256
/* Bytes of the signed PSBT per echo line (64 base64 characters). */
#define DS_APP_ECHO_CHUNK 48

#define DS_APP_MSG_MS 2000 /* "Not a seed", "Not a PSBT" */
#define DS_APP_PART_MS 250 /* one QR part every 250 ms */
/* A PRESS arms the review only this long after the port took it (see
 * above): button events may come from samples up to 200 ms old. */
#define DS_APP_ARM_MS 300
#define DS_APP_UI_CONTRAST 0x80

typedef struct {
    ds_session s;
    ds_rx rx;
    ds_qr qr;
    ds_pager pager;
    ds_picker pick; /* the PIN or the passphrase being typed: a secret */
    ds_screen screen;
    uint8_t out[DS_APP_OUT_MAX];   /* the signed PSBT */
    size_t out_len;
    uint8_t cbor[DS_APP_CBOR_MAX]; /* the CBOR shown as QR */
    size_t cbor_len;
    char summary[DS_APP_SUMMARY_MAX];
    const uint8_t *psbt; /* the received PSBT, inside rx.work */
    size_t psbt_len;
    char echo[DS_APP_ECHO_BUF]; /* NUL-separated lines */
    size_t echo_len;
    uint32_t rate;
    int state, cursor;
    int testnet, contrast, scale; /* settings: contrast index, 2 or 1 px */
    int qr_scale;                 /* the scale the QR really uses */
    int err_kind;                 /* which error screen */
    int32_t err;                  /* the error it names */
    uint32_t seed_fp;
    int job, job_seen;            /* the heavy work pending */
    const char *t_kind;           /* the timing to echo at the next tick */
    uint32_t t_at;
    uint32_t now, msg_until, part_at, lost;
    int msg;                      /* 1 while "Not a ..." is shown */
    int armed;                    /* a late enough PRESS was taken in the review */
    int pager_taken;              /* the port took the review screen */
    int pager_shown;              /* a tick ran after that, at pager_at */
    uint32_t pager_at;
    uint32_t echo_dropped;        /* lines dropped from the full queue */
    uint32_t sig_id;              /* signed PSBTs so far, for the echo */
    int sig_phase;                /* the signed PSBT's echo: where it is */
    size_t sig_pos;
    int dirty, reboot;
} ds_app;

/* Everything to zero, then HOME. audio_rate: the rate of ds_app_audio. */
void ds_app_init(ds_app *a, uint32_t audio_rate);

/* Line-in samples. Taken only while listening; ignored otherwise. */
void ds_app_audio(ds_app *a, const int16_t *x, size_t n);

/* The port lost this many samples (its ring overflowed). While listening
 * the loss is counted on screen and the gap is fed as silence (up to 2 s),
 * so the receiver keeps its timing: Reed-Solomon corrects the frame or it is
 * lost and the fountain waits for another. */
void ds_app_audio_lost(ds_app *a, uint32_t samples);

/* A button event: btn DS_BTN_LEFT..RIGHT, kind DS_BTN_PRESS/LONG/WIPE. */
void ds_app_button(ds_app *a, int btn, int kind);

/* The clock (milliseconds, free-running, wrapping): runs the pending heavy
 * work, takes what the receiver heard, times messages and QR parts. */
void ds_app_tick(ds_app *a, uint32_t now_ms);

/* 1 if the screen changed since the last call: draw it before the next
 * ds_app_tick (the review's arming delay counts from that tick). */
int ds_app_take_dirty(ds_app *a);

/* The screen in fb: the text, and the QR in the left square when there is one. */
void ds_app_draw(const ds_app *a, uint8_t fb[8][128]);

/* The display contrast: DS_APP_UI_CONTRAST, or the QR setting while a QR is shown. */
uint8_t ds_app_contrast(const ds_app *a);

/* The next echo line (NUL-terminated, no line break): returns its length, 0
 * when there is none, or DS_ERR_SPACE (nothing taken) when cap is under
 * DS_APP_ECHO_MAX. */
int32_t ds_app_take_echo(ds_app *a, char *out, size_t cap);

/* 1 after a wipe: the port reboots the device. */
int ds_app_wants_reboot(const ds_app *a);

/* 1 if a signed PSBT of psbt_len bytes with n_ours inputs signed is sure to
 * fit an animated QR (at 1 px), 0 if not. Uses a->qr and a->cbor as scratch
 * (zeroed after): call it only while no QR is shown. */
int ds_app_fits_qr(ds_app *a, size_t psbt_len, uint32_t n_ours);

#endif
