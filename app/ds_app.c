/* DTMF-Signer device logic: the screen state machine. See ds_app.h. */
#include "ds_app.h"

#include <stdio.h>
#include <string.h>

#include "ds_base64.h"
#include "ds_btn.h"

enum {
    ST_HOME = 0,
    ST_LOAD,      /* listening for a seed */
    ST_PIN,       /* typing the PIN */
    ST_SEED_FP,   /* "Seed fingerprint" */
    ST_PASS_ASK,  /* "Passphrase?" */
    ST_PASS,      /* typing the passphrase */
    ST_WALLET_FP, /* "Wallet fingerprint" */
    ST_READY,
    ST_XPUB,
    ST_LISTEN,    /* listening for a PSBT */
    ST_PAGER,     /* the review */
    ST_SIGNED,
    ST_ERROR,     /* a message; OK goes to err_to */
    ST_WORKING,
    ST_WIPED
};

/* Heavy work, run in the tick after "Working..." was shown. */
enum { JOB_NONE = 0, JOB_PIN, JOB_DERIVE, JOB_REVIEW, JOB_SIGN, JOB_QR_SIGNED, JOB_QR_XPUB };

/* Error screens: the title and what OK returns to. */
enum {
    E_SEED = 1,   /* the seed could not be loaded -> HOME */
    E_PASS,       /* the passphrase was refused -> Passphrase? */
    E_REJECTED,   /* the review failed -> READY */
    E_NOTHING,    /* no input of this wallet -> READY */
    E_TOO_BIG,    /* the signed PSBT would not fit a QR -> READY */
    E_SIGN,       /* signing failed -> READY */
    E_NO_QR,      /* signed, but the QR would not start -> READY */
};

/* The signed PSBT's echo, one step per take. */
enum { SIG_NONE = 0, SIG_BEGIN, SIG_CHUNKS, SIG_END };

static const uint8_t CONTRASTS[] = {0x01, 0x04, 0x10, 0x20, 0x40, 0x80, 0xFF};
#define N_CONTRASTS ((int)(sizeof CONTRASTS / sizeof CONTRASTS[0]))
#define DEFAULT_CONTRAST 3 /* 0x20 */

#define HOME_ITEMS 4
#define READY_ITEMS 5

static const char *state_name(int st)
{
    switch (st) {
    case ST_HOME: return "home";
    case ST_LOAD: return "listen-seed";
    case ST_PIN: return "pin";
    case ST_SEED_FP: return "seed-fingerprint";
    case ST_PASS_ASK: return "passphrase?";
    case ST_PASS: return "passphrase";
    case ST_WALLET_FP: return "wallet-fingerprint";
    case ST_READY: return "ready";
    case ST_XPUB: return "xpub";
    case ST_LISTEN: return "listen-psbt";
    case ST_PAGER: return "review";
    case ST_SIGNED: return "signed";
    case ST_ERROR: return "error";
    case ST_WORKING: return "working";
    case ST_WIPED: return "wiped";
    default: return "?";
    }
}

/* ---- echo ---- */

static void echo_pop(ds_app *a)
{
    size_t n = strlen(a->echo) + 1;
    memmove(a->echo, a->echo + n, a->echo_len - n);
    a->echo_len -= n;
    memset(a->echo + a->echo_len, 0, n);
}

/* Queues a line, dropping the oldest ones when the queue is full. */
static void echo_line(ds_app *a, const char *line)
{
    size_t n = strlen(line) + 1;
    if (n > DS_APP_ECHO_MAX) {
        a->echo_dropped++;
        return;
    }
    while (a->echo_len + n > sizeof a->echo) {
        echo_pop(a);
        a->echo_dropped++;
    }
    memcpy(a->echo + a->echo_len, line, n);
    a->echo_len += n;
}

/* Stops the signed PSBT's echo when its bytes are zeroed before it ended:
 * the abort is said in the echo. */
static void echo_signed_abort(ds_app *a)
{
    char l[40];
    if (a->sig_phase != SIG_NONE) {
        snprintf(l, sizeof l, "signed-psbt-abort %u", (unsigned)a->sig_id);
        echo_line(a, l);
        a->sig_phase = SIG_NONE;
        a->sig_pos = 0;
    }
}

/* The next line of the signed PSBT's echo into l (cap >= DS_APP_ECHO_MAX). */
static int32_t echo_signed_next(ds_app *a, char *l, size_t cap)
{
    int32_t r;
    size_t n;

    switch (a->sig_phase) {
    case SIG_BEGIN:
        a->sig_phase = SIG_CHUNKS;
        a->sig_pos = 0;
        return (int32_t)snprintf(l, cap, "signed-psbt-begin %u %u", (unsigned)a->sig_id,
                                 (unsigned)a->out_len);
    case SIG_CHUNKS:
        n = a->out_len - a->sig_pos;
        if (n > DS_APP_ECHO_CHUNK) {
            n = DS_APP_ECHO_CHUNK;
        }
        memcpy(l, "signed-psbt ", 12);
        r = ds_base64_encode(a->out + a->sig_pos, n, l + 12, cap - 12);
        a->sig_pos += n;
        if (a->sig_pos >= a->out_len) {
            a->sig_phase = SIG_END;
        }
        return r < 0 ? r : 12 + r;
    case SIG_END:
        a->sig_phase = SIG_NONE;
        a->sig_pos = 0;
        return (int32_t)snprintf(l, cap, "signed-psbt-end %u", (unsigned)a->sig_id);
    default:
        return 0;
    }
}

int32_t ds_app_take_echo(ds_app *a, char *out, size_t cap)
{
    size_t n;

    if (cap < DS_APP_ECHO_MAX) {
        return DS_ERR_SPACE; /* nothing taken */
    }
    if (a->echo_dropped > 0) {
        int32_t r = (int32_t)snprintf(out, cap, "echo-dropped %u", (unsigned)a->echo_dropped);
        a->echo_dropped = 0;
        return r;
    }
    if (a->echo_len > 0) {
        n = strlen(a->echo);
        memcpy(out, a->echo, n + 1);
        echo_pop(a);
        return (int32_t)n;
    }
    return echo_signed_next(a, out, cap);
}

/* ---- the screen ---- */

static void line(ds_screen *s, int row, const char *text)
{
    snprintf(s->line[row], sizeof s->line[row], "%s", text);
}

static void menu(const ds_app *a, ds_screen *s, int first, const char *const *items, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        line(s, first + i, items[i]);
    }
    s->highlight = first + a->cursor;
}

static void listening(const ds_app *a, ds_screen *s, int psbt)
{
    line(s, 0, "Listening (cable)");
    if (psbt) {
        uint32_t have = 0, need = 0;
        ds_rx_progress(&a->rx, &have, &need);
        if (need == 0) {
            snprintf(s->line[1], sizeof s->line[1], "parts 0/?  lost %u",
                     (unsigned)a->lost);
        } else {
            snprintf(s->line[1], sizeof s->line[1], "parts %u/%u  lost %u",
                     (unsigned)have, (unsigned)need, (unsigned)a->lost);
        }
    } else {
        snprintf(s->line[1], sizeof s->line[1], "tones %u  lost %u",
                 (unsigned)a->rx.tones, (unsigned)a->lost);
    }
    if (a->msg) {
        line(s, 3, psbt ? "Not a PSBT" : "Not a seed");
    }
    line(s, 7, "<: back");
}

static void error_screen(const ds_app *a, ds_screen *s)
{
    switch (a->err_kind) {
    case E_SEED:
        line(s, 0, "Seed not loaded");
        line(s, 1, "Wrong PIN or data");
        break;
    case E_PASS:
        line(s, 0, "Bad passphrase");
        break;
    case E_REJECTED:
        line(s, 0, "Rejected");
        line(s, 1, ds_error_name(a->err));
        break;
    case E_NOTHING:
        line(s, 0, "Rejected");
        line(s, 1, "No input of this");
        line(s, 2, "wallet to sign");
        break;
    case E_TOO_BIG:
        line(s, 0, "Too big for QR");
        line(s, 1, "Nothing signed");
        break;
    case E_SIGN:
        line(s, 0, "Sign failed");
        line(s, 1, ds_error_name(a->err));
        break;
    case E_NO_QR:
        line(s, 0, "Signed, but no QR:");
        line(s, 1, "output discarded");
        break;
    default:
        line(s, 0, "Error");
        break;
    }
    line(s, 7, "OK: back");
}

static void build(const ds_app *a, ds_screen *s)
{
    char buf[DS_UI_COLS + 1];

    memset(s, 0, sizeof *s);
    s->highlight = -1;
    switch (a->state) {
    case ST_HOME: {
        char net[22], con[22], size[22];
        const char *items[HOME_ITEMS];
        snprintf(net, sizeof net, "Network: %s", a->testnet ? "testnet" : "mainnet");
        snprintf(con, sizeof con, "QR contrast: %02X", CONTRASTS[a->contrast]);
        snprintf(size, sizeof size, "QR size: %dpx", a->scale);
        items[0] = "Load seed";
        items[1] = net;
        items[2] = con;
        items[3] = size;
        line(s, 0, "DTMF-Signer");
        menu(a, s, 2, items, HOME_ITEMS);
        break;
    }
    case ST_LOAD:
        listening(a, s, 0);
        break;
    case ST_PIN:
        ds_picker_screen(&a->pick, "PIN (none: OK)", s);
        break;
    case ST_SEED_FP:
        line(s, 0, "Seed fingerprint");
        snprintf(buf, sizeof buf, "%08x", (unsigned)a->seed_fp);
        line(s, 2, buf);
        line(s, 5, "OK: continue");
        line(s, 6, "<: discard");
        break;
    case ST_PASS_ASK:
        line(s, 0, "Passphrase?");
        line(s, 2, "OK: none");
        line(s, 3, ">: type it");
        line(s, 6, "<: discard");
        break;
    case ST_PASS:
        ds_picker_screen(&a->pick, "Passphrase", s);
        break;
    case ST_WALLET_FP:
        line(s, 0, "Wallet fingerprint");
        snprintf(buf, sizeof buf, "%08x", (unsigned)a->s.w.wallet_fp);
        line(s, 2, buf);
        line(s, 6, "OK: continue");
        break;
    case ST_READY: {
        char con[22], size[22];
        const char *items[READY_ITEMS];
        snprintf(con, sizeof con, "QR contrast: %02X", CONTRASTS[a->contrast]);
        snprintf(size, sizeof size, "QR size: %dpx", a->scale);
        items[0] = "Sign PSBT";
        items[1] = "Show XPUB";
        items[2] = con;
        items[3] = size;
        items[4] = "Wipe";
        snprintf(buf, sizeof buf, "Wallet %08x", (unsigned)a->s.w.wallet_fp);
        line(s, 0, buf);
        menu(a, s, 2, items, READY_ITEMS);
        break;
    }
    case ST_XPUB:
    case ST_SIGNED:
        s->qr = 1;
        if (a->state == ST_XPUB) {
            line(s, 0, "XPUB");
            snprintf(buf, sizeof buf, "%08x", (unsigned)a->s.w.wallet_fp);
            line(s, 1, buf);
        } else {
            line(s, 0, "Signed");
        }
        snprintf(buf, sizeof buf, "part %u", (unsigned)a->qr.shown);
        line(s, 3, buf);
        line(s, 4, a->qr_scale == 1 ? "1px" : "2px");
        line(s, 7, a->state == ST_XPUB ? "<: back" : "<: done");
        break;
    case ST_LISTEN:
        listening(a, s, 1);
        break;
    case ST_PAGER:
        ds_pager_page(&a->pager, s);
        if (!a->armed && a->pager.page + 1 >= a->pager.pages) {
            line(s, 7, "Tap OK, then hold OK");
        }
        break;
    case ST_ERROR:
        error_screen(a, s);
        break;
    case ST_WORKING:
        line(s, 0, "Working...");
        break;
    case ST_WIPED:
        line(s, 0, "Wiped");
        break;
    default:
        break;
    }
}

/* Rebuilds the screen; dirty when it changed. */
static void refresh(ds_app *a)
{
    ds_screen s;
    build(a, &s);
    if (memcmp(&s, &a->screen, sizeof s) != 0) {
        a->screen = s;
        a->dirty = 1;
    }
    ds_wipe(&s, sizeof s); /* a picker shows the character being chosen */
}

static void set_state(ds_app *a, int st)
{
    char l[40];
    /* A new screen: run_job gates it again if a job put it up. */
    a->gated = 0;
    if (a->state != st) {
        a->state = st;
        snprintf(l, sizeof l, "state %s", state_name(st));
        echo_line(a, l);
    }
    a->cursor = 0;
    a->msg = 0;
}

static void start_job(ds_app *a, int job)
{
    a->job = job;
    a->job_seen = 0;
    set_state(a, ST_WORKING);
}

static void error_to(ds_app *a, int kind, int32_t err)
{
    a->err_kind = kind;
    a->err = err;
    set_state(a, ST_ERROR);
}

/* ---- wiping ---- */

static void clear_qr(ds_app *a)
{
    ds_wipe(&a->qr, sizeof a->qr);
    ds_wipe(a->cbor, sizeof a->cbor);
    a->cbor_len = 0;
}

/* The signed PSBT, the QR, the review text and the receiver to zero. */
static void clear_work(ds_app *a)
{
    clear_qr(a);
    echo_signed_abort(a);
    ds_wipe(a->out, sizeof a->out);
    a->out_len = 0;
    ds_wipe(a->summary, sizeof a->summary);
    ds_wipe(&a->pager, sizeof a->pager);
    ds_rx_wipe(&a->rx);
    a->psbt = NULL;
    a->psbt_len = 0;
}

/* Everything secret to zero: the session, the picker and the work. */
static void clear_all(ds_app *a)
{
    ds_session_wipe(&a->s);
    ds_picker_wipe(&a->pick);
    clear_work(a);
    a->seed_fp = 0;
}

static void go_home(ds_app *a)
{
    clear_all(a);
    set_state(a, ST_HOME);
}

static void go_ready(ds_app *a)
{
    ds_session_cancel(&a->s);
    clear_work(a);
    set_state(a, ST_READY);
}

static void wipe(ds_app *a)
{
    clear_all(a);
    a->job = JOB_NONE;
    a->t_kind = NULL;
    a->reboot = 1;
    set_state(a, ST_WIPED);
}

/* ---- the PSBT source and the signed PSBT sink ---- */

static int32_t psbt_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    const ds_app *a = ctx;
    if ((size_t)off > a->psbt_len || n > a->psbt_len - off) {
        return DS_ERR_PSBT;
    }
    memcpy(buf, a->psbt + off, n);
    return (int32_t)n;
}

static int32_t out_write(void *ctx, const uint8_t *p, size_t n)
{
    ds_app *a = ctx;
    if (n > sizeof a->out - a->out_len) {
        return DS_ERR_SPACE;
    }
    memcpy(a->out + a->out_len, p, n);
    a->out_len += n;
    return (int32_t)n;
}

static ds_source psbt_source(ds_app *a)
{
    ds_source src;
    src.read = psbt_read;
    src.ctx = a;
    src.len = (uint32_t)a->psbt_len;
    return src;
}

int ds_app_fits_qr(ds_app *a, size_t psbt_len, uint32_t n_ours)
{
    size_t bound;
    int32_t r;

    if (psbt_len > DS_APP_PSBT_MAX || n_ours > DS_MAX_INPUTS) {
        return 0;
    }
    bound = psbt_len + (size_t)n_ours * 112 + 8;
    if (bound > sizeof a->cbor) {
        return 0;
    }
    /* ds_qr_start measures the parts with the message CRC-32 at its longest
     * (5 bytes), so whether they fit depends on the length only: zeros
     * probe every message of this length. */
    clear_qr(a);
    r = ds_qr_start(&a->qr, "crypto-psbt", a->cbor, bound, 1);
    clear_qr(a);
    return r > 0;
}

/* ---- heavy work ---- */

static void job_pin(ds_app *a)
{
    uint8_t entropy[32];
    size_t pin_len = a->pick.len;
    int32_t n, r;
    ds_wallet tmp;

    n = ds_rx_seed(&a->rx, pin_len > 0 ? a->pick.buf : NULL, entropy);
    ds_picker_wipe(&a->pick);
    ds_rx_wipe(&a->rx);
    if (n < 0) {
        ds_wipe(entropy, sizeof entropy);
        clear_all(a);
        error_to(a, E_SEED, n);
        return;
    }
    /* On these channels DS_NEED_CONFIRM and DS_WARN_AIR cannot happen: any
     * result but DS_OK is an error. */
    r = ds_session_seed(&a->s, entropy, (size_t)n, pin_len > 0, pin_len, DS_CABLE, 0);
    if (r == DS_OK) {
        r = ds_wallet_load(&tmp, entropy, (size_t)n, NULL, a->testnet);
        a->seed_fp = tmp.seed_fp;
        ds_wallet_wipe(&tmp);
    }
    ds_wipe(entropy, sizeof entropy);
    if (r != DS_OK) {
        clear_all(a);
        error_to(a, E_SEED, r);
        return;
    }
    set_state(a, ST_SEED_FP);
}

static void job_derive(ds_app *a)
{
    char l[32];
    int32_t r = ds_session_passphrase(&a->s, a->pick.len > 0 ? a->pick.buf : NULL,
                                      DS_LOCAL, 0);
    ds_picker_wipe(&a->pick);
    if (r == DS_ERR_PASSPHRASE && a->s.state == DS_S_SEED) {
        error_to(a, E_PASS, r);
        return;
    }
    if (r != DS_OK) {
        clear_all(a);
        error_to(a, E_SEED, r);
        return;
    }
    snprintf(l, sizeof l, "fingerprint %08x", (unsigned)a->s.w.wallet_fp);
    echo_line(a, l);
    set_state(a, ST_WALLET_FP);
}

static void job_review(ds_app *a)
{
    const uint8_t *p = NULL;
    size_t len = 0;
    ds_source src;
    int32_t r;

    r = ds_rx_psbt(&a->rx, &p, &len);
    if (r != DS_OK || len > DS_APP_PSBT_MAX) {
        clear_work(a);
        error_to(a, E_REJECTED, r != DS_OK ? r : DS_ERR_LIMIT);
        return;
    }
    a->psbt = p;
    a->psbt_len = len;
    src = psbt_source(a);
    r = ds_session_review(&a->s, &src, DS_CABLE);
    if (r != DS_OK) {
        ds_session_cancel(&a->s);
        clear_work(a);
        error_to(a, E_REJECTED, r);
        return;
    }
    if (a->s.r.n_ours == 0) {
        ds_session_cancel(&a->s);
        clear_work(a);
        error_to(a, E_NOTHING, 0);
        return;
    }
    r = ds_psbt_summary_text(&a->s.r, a->summary, sizeof a->summary);
    if (r < 0) {
        ds_session_cancel(&a->s);
        clear_work(a);
        error_to(a, E_REJECTED, r);
        return;
    }
    /* Never sign what cannot be given back. */
    if (!ds_app_fits_qr(a, len, a->s.r.n_ours)) {
        ds_session_cancel(&a->s);
        clear_work(a);
        error_to(a, E_TOO_BIG, DS_ERR_LIMIT);
        return;
    }
    ds_pager_init(&a->pager, a->summary);
    /* A hold or a tap carried over from before the review cannot arm it. */
    a->armed = 0;
    set_state(a, ST_PAGER);
}

static void job_sign(ds_app *a)
{
    ds_source src = psbt_source(a);
    ds_sink dst;
    int32_t r;

    dst.write = out_write;
    dst.ctx = a;
    ds_wipe(a->out, sizeof a->out);
    a->out_len = 0;
    r = ds_session_sign(&a->s, &src, &dst);
    if (r == DS_OK) {
        r = ut_psbt_to_cbor(a->out, a->out_len, a->cbor, sizeof a->cbor);
        if (r < 0) {
            r = DS_ERR_SPACE;
        } else {
            a->cbor_len = (size_t)r;
            r = DS_OK;
        }
    }
    if (r != DS_OK) {
        ds_session_cancel(&a->s);
        clear_work(a);
        error_to(a, E_SIGN, r);
        return;
    }
    /* The PSBT received is no longer needed. */
    ds_rx_wipe(&a->rx);
    a->psbt = NULL;
    a->psbt_len = 0;
    a->job = JOB_QR_SIGNED; /* still "Working...": the QR in the next tick */
    a->job_seen = 1;
}

static int32_t show_qr(ds_app *a, const char *type, int st)
{
    int32_t r = ds_qr_start(&a->qr, type, a->cbor, a->cbor_len, a->scale);
    if (r < 0) {
        return r;
    }
    a->qr_scale = (int)r;
    ds_qr_next(&a->qr);
    a->part_at = a->now;
    set_state(a, st);
    return DS_OK;
}

static void job_qr_signed(ds_app *a)
{
    /* Unreachable while ds_app_fits_qr holds for the review (the bound
     * covers the signed PSBT); if it ever fails, the signature is dropped. */
    if (show_qr(a, "crypto-psbt", ST_SIGNED) != DS_OK) {
        clear_work(a);
        error_to(a, E_NO_QR, DS_ERR_LIMIT);
        return;
    }
    a->sig_id++;
    a->sig_phase = SIG_BEGIN;
    a->sig_pos = 0;
}

static void job_qr_xpub(ds_app *a)
{
    char desc[200];
    int32_t n = ds_wallet_account_cbor(&a->s.w, a->cbor, sizeof a->cbor);
    if (n > 0) {
        a->cbor_len = (size_t)n;
        n = show_qr(a, "crypto-account", ST_XPUB);
    }
    if (n < 0) {
        clear_qr(a);
        error_to(a, E_TOO_BIG, DS_ERR_LIMIT);
        return;
    }
    if (ds_wallet_descriptor(&a->s.w, desc, sizeof desc) > 0) {
        echo_line(a, desc);
    }
}

static void run_job(ds_app *a)
{
    int job = a->job;
    static const char *const kinds[] = {"", "pin", "derive", "review", "sign", "qr", "qr"};

    a->job = JOB_NONE;
    switch (job) {
    case JOB_PIN: job_pin(a); break;
    case JOB_DERIVE: job_derive(a); break;
    case JOB_REVIEW: job_review(a); break;
    case JOB_SIGN: job_sign(a); break;
    case JOB_QR_SIGNED: job_qr_signed(a); break;
    case JOB_QR_XPUB: job_qr_xpub(a); break;
    default: return;
    }
    a->t_kind = kinds[job];
    a->t_at = a->now;
    /* The screen the job put up (not "Working..." again) starts gated: see
     * gate_open. */
    if (a->state != ST_WORKING && a->state != ST_WIPED) {
        a->gated = 1;
        a->gate_taken = 0;
        a->gate_shown = 0;
        a->gate_at = 0;
    }
}

/* 1 once a screen a job put up (the fingerprints, an error, the review, a
 * QR) has been taken by the port, a tick followed (gate_at) and
 * DS_APP_ARM_MS have passed since: an event delivered earlier may come from
 * a press made during "Working..." (events lag their samples by up to
 * 200 ms), so it must not act on a screen the user has not seen. Always 1 on
 * the other screens. */
static int gate_open(const ds_app *a)
{
    return !a->gated || (a->gate_shown && a->now - a->gate_at >= DS_APP_ARM_MS);
}

/* ---- the public calls ---- */

void ds_app_init(ds_app *a, uint32_t audio_rate)
{
    ds_wipe(a, sizeof *a);
    a->rate = audio_rate;
    a->contrast = DEFAULT_CONTRAST;
    a->scale = 2;
    a->state = ST_HOME;
    refresh(a);
    a->dirty = 1;
}

static int listening_now(const ds_app *a)
{
    /* DS_RX_DONE leaves ST_LISTEN in the same poll: no audio reaches the
     * receiver after it. */
    return a->state == ST_LOAD || a->state == ST_LISTEN;
}

void ds_app_audio(ds_app *a, const int16_t *x, size_t n)
{
    if (listening_now(a)) {
        ds_rx_push(&a->rx, x, n);
    }
}

void ds_app_audio_lost(ds_app *a, uint32_t samples)
{
    static const int16_t silence[64];
    uint32_t left = samples;

    if (!listening_now(a)) {
        return;
    }
    a->lost++;
    if (left > 2 * a->rate) {
        left = 2 * a->rate;
    }
    while (left > 0) {
        uint32_t k = left < 64 ? left : 64;
        ds_rx_push(&a->rx, silence, k);
        left -= k;
    }
    refresh(a);
}

static void start_listening(ds_app *a, int mode, int st)
{
    ds_rx_init(&a->rx, a->rate, mode);
    a->lost = 0;
    set_state(a, st);
}

static void button_home(ds_app *a, int btn)
{
    if (btn == DS_BTN_UP) {
        a->cursor = (a->cursor + HOME_ITEMS - 1) % HOME_ITEMS;
    } else if (btn == DS_BTN_DOWN) {
        a->cursor = (a->cursor + 1) % HOME_ITEMS;
    } else if (btn == DS_BTN_OK) {
        switch (a->cursor) {
        case 0:
            ds_session_init(&a->s, a->testnet);
            start_listening(a, DS_RX_SEED, ST_LOAD);
            break;
        case 1: a->testnet = !a->testnet; break;
        case 2: a->contrast = (a->contrast + 1) % N_CONTRASTS; break;
        default: a->scale = a->scale == 2 ? 1 : 2; break;
        }
    }
}

static void button_ready(ds_app *a, int btn)
{
    if (btn == DS_BTN_UP) {
        a->cursor = (a->cursor + READY_ITEMS - 1) % READY_ITEMS;
    } else if (btn == DS_BTN_DOWN) {
        a->cursor = (a->cursor + 1) % READY_ITEMS;
    } else if (btn == DS_BTN_OK) {
        switch (a->cursor) {
        case 0: start_listening(a, DS_RX_PSBT, ST_LISTEN); break;
        case 1: start_job(a, JOB_QR_XPUB); break;
        case 2: a->contrast = (a->contrast + 1) % N_CONTRASTS; break;
        case 3: a->scale = a->scale == 2 ? 1 : 2; break;
        default: wipe(a); break;
        }
    }
}

/* The review. ds_btn sends at most one LONG per press, and a PRESS ends
 * its press. A PRESS arms the review only DS_APP_ARM_MS after the review
 * was taken by the port and a tick followed (gate_open): events lag their
 * samples by up to 200 ms, so its release happened after the review was
 * drawn, and every later LONG belongs to a press begun after that. A LONG
 * before (a hold or a tap carried over from "Listening" or "Working...")
 * is ignored. Then OK held confirms, only on the last page once every page
 * has been reached. */
static void button_pager(ds_app *a, int btn, int kind)
{
    if (kind == DS_BTN_LONG) {
        if (btn == DS_BTN_OK && a->armed && a->pager.seen_last &&
            a->pager.page + 1 >= a->pager.pages && a->s.state == DS_S_REVIEW &&
            ds_session_confirm(&a->s) == DS_OK) {
            start_job(a, JOB_SIGN);
        }
        return;
    }
    if (kind != DS_BTN_PRESS) {
        return;
    }
    if (gate_open(a)) {
        a->armed = 1;
    }
    if (btn == DS_BTN_UP) {
        ds_pager_move(&a->pager, -1);
    } else if (btn == DS_BTN_DOWN) {
        ds_pager_move(&a->pager, 1);
    } else if (btn == DS_BTN_LEFT) {
        go_ready(a);
    }
}

static void button_error(ds_app *a, int btn)
{
    if (btn != DS_BTN_OK && btn != DS_BTN_LEFT) {
        return;
    }
    switch (a->err_kind) {
    case E_SEED: go_home(a); break;
    case E_PASS: set_state(a, ST_PASS_ASK); break;
    default: go_ready(a); break;
    }
    a->err = 0;
    a->err_kind = 0;
}

/* The picker for the PIN or the passphrase: LEFT on an empty entry goes back. */
static void button_picker(ds_app *a, int btn, int kind, int pin)
{
    if (kind == DS_BTN_PRESS && btn == DS_BTN_LEFT && a->pick.len == 0) {
        ds_picker_wipe(&a->pick);
        if (pin) {
            go_home(a);
        } else {
            set_state(a, ST_PASS_ASK);
        }
        return;
    }
    if (ds_picker_button(&a->pick, btn, kind) == 1) {
        start_job(a, pin ? JOB_PIN : JOB_DERIVE);
    }
}

void ds_app_button(ds_app *a, int btn, int kind)
{
    if (a->state == ST_WIPED) {
        return;
    }
    if (kind == DS_BTN_WIPE) {
        wipe(a);
        refresh(a);
        return;
    }
    if (a->state == ST_PIN || a->state == ST_PASS) {
        button_picker(a, btn, kind, a->state == ST_PIN);
        refresh(a);
        return;
    }
    if (a->state == ST_PAGER) {
        button_pager(a, btn, kind);
        refresh(a);
        return;
    }
    if (kind != DS_BTN_PRESS || !gate_open(a)) {
        return; /* a tap carried over from "Working..." is dropped */
    }
    switch (a->state) {
    case ST_HOME:
        button_home(a, btn);
        break;
    case ST_LOAD:
        if (btn == DS_BTN_LEFT) {
            go_home(a);
        }
        break;
    case ST_SEED_FP:
        if (btn == DS_BTN_OK) {
            set_state(a, ST_PASS_ASK);
        } else if (btn == DS_BTN_LEFT) {
            go_home(a); /* maybe a wrong PIN: discard it all */
        }
        break;
    case ST_PASS_ASK:
        if (btn == DS_BTN_OK) {
            ds_picker_wipe(&a->pick);
            start_job(a, JOB_DERIVE);
        } else if (btn == DS_BTN_RIGHT) {
            ds_picker_init(&a->pick, DS_PICK_TEXT);
            set_state(a, ST_PASS);
        } else if (btn == DS_BTN_LEFT) {
            go_home(a);
        }
        break;
    case ST_WALLET_FP:
        if (btn == DS_BTN_OK) {
            set_state(a, ST_READY);
        }
        break;
    case ST_READY:
        button_ready(a, btn);
        break;
    case ST_XPUB:
        if (btn == DS_BTN_LEFT) {
            clear_qr(a);
            set_state(a, ST_READY);
            a->cursor = 1;
        }
        break;
    case ST_SIGNED:
    case ST_LISTEN:
        if (btn == DS_BTN_LEFT) {
            go_ready(a);
        }
        break;
    case ST_ERROR:
        button_error(a, btn);
        break;
    default:
        break; /* working: buttons wait */
    }
    refresh(a);
}

/* Takes what the receiver heard, one group per poll. */
static void poll_rx(ds_app *a)
{
    while (listening_now(a)) {
        int32_t r = ds_rx_poll(&a->rx);
        if (r == DS_RX_NONE) {
            break;
        }
        if (r == DS_RX_OTHER) {
            a->msg = 1;
            a->msg_until = a->now + DS_APP_MSG_MS;
        } else if (r == DS_RX_GOT_SEED) {
            ds_picker_init(&a->pick, DS_PICK_PIN);
            set_state(a, ST_PIN);
        } else if (r == DS_RX_DONE) {
            start_job(a, JOB_REVIEW);
        }
    }
}

void ds_app_tick(ds_app *a, uint32_t now_ms)
{
    char l[40];

    a->now = now_ms;
    if (a->state == ST_WIPED) {
        return;
    }
    if (a->t_kind != NULL) {
        snprintf(l, sizeof l, "t %s %u", a->t_kind, (unsigned)(now_ms - a->t_at));
        echo_line(a, l);
        a->t_kind = NULL;
    }
    if (a->gated && a->gate_taken && !a->gate_shown) {
        a->gate_shown = 1;
        a->gate_at = now_ms;
    }
    if (a->job != JOB_NONE && a->job_seen) {
        run_job(a);
    }
    poll_rx(a);
    if (a->msg && (int32_t)(now_ms - a->msg_until) >= 0) {
        a->msg = 0;
    }
    if ((a->state == ST_XPUB || a->state == ST_SIGNED) &&
        now_ms - a->part_at >= DS_APP_PART_MS) {
        ds_qr_next(&a->qr);
        a->part_at += DS_APP_PART_MS;
        if (now_ms - a->part_at >= DS_APP_PART_MS) {
            a->part_at = now_ms; /* a stalled loop: resync, no burst */
        }
        a->dirty = 1;
    }
    if (a->job != JOB_NONE) {
        a->job_seen = 1;
    }
    refresh(a);
}

int ds_app_take_dirty(ds_app *a)
{
    int d = a->dirty;
    a->dirty = 0;
    if (d && a->gated) {
        a->gate_taken = 1; /* the port draws the job's screen now */
    }
    return d;
}

void ds_app_draw(const ds_app *a, uint8_t fb[8][128])
{
    ds_ui_render(&a->screen, fb);
    if (a->screen.qr) {
        ds_qr_draw(&a->qr, fb);
    }
}

uint8_t ds_app_contrast(const ds_app *a)
{
    return a->screen.qr ? CONTRASTS[a->contrast] : DS_APP_UI_CONTRAST;
}

int ds_app_wants_reboot(const ds_app *a)
{
    return a->reboot;
}
