/* dtmf-signer-host: the computer build. Reads a seed (a ur-tones UR, keypad
 * tones, a frame of tones or a WAV recording), then either
 *
 *   keys  prints its fingerprints, the BIP-84 account xpub and the descriptor;
 *   sign  reads a PSBT, prints its review, asks for "yes" on standard input
 *         (the stand-in for the signer's button) and writes the signed PSBT.
 *   tones writes a WAV recording of a seed or a PSBT as ur-tones tones (a test
 *         tool: the recordings are played into the prototype, and read back
 *         by keys --wav and sign --psbt FILE.wav).
 *
 * Exit codes: 0 done, 1 error (message on stderr), 2 wrong usage, 3 signing
 * refused by the user. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ds_base64.h"
#include "ds_rx.h"
#include "ds_session.h"
#include "ds_wallet.h"
#include "ur_tones.h"
#include "wav.h"

#define EXIT_FAIL 1
#define EXIT_USAGE 2
#define EXIT_REFUSED 3
#define PSBT_FILE_MAX (4u << 20) /* the largest PSBT file read */
#define SUMMARY_MAX 8192
#define PASSPHRASE_MAX 256

typedef enum { SRC_NONE, SRC_UR, SRC_KEYPAD, SRC_FRAME, SRC_WAV } source_kind;

typedef struct {
    source_kind kind;
    const char *value; /* the UR, the tones or the file name */
    char *pin;         /* points into argv */
    char *passphrase;  /* points into argv */
    const char *psbt;  /* sign: the PSBT file */
    const char *out;   /* sign: where the signed PSBT goes (default stdout) */
    int sign;
    int testnet;
} options;

static void usage(void)
{
    fputs("usage: dtmf-signer-host keys SEED [--pin PIN] [--passphrase TEXT] [--testnet]\n"
          "       dtmf-signer-host sign SEED --psbt FILE [--out FILE]\n"
          "                             [--pin PIN] [--passphrase TEXT] [--testnet]\n"
          "       dtmf-signer-host tones seed ENTROPY_HEX [--pin PIN] [--keypad] --out FILE.wav\n"
          "                                 [--rate 16000|44100] [--timing cable|air]\n"
          "       dtmf-signer-host tones psbt FILE --out FILE.wav\n"
          "                                 [--rate 16000|44100] [--timing cable|air]\n"
          "SEED is --ur UR, --keypad TONES, --frame TONES or --wav FILE.\n"
          "sign reads a PSBT (base64, binary, ur:crypto-psbt / ur:psbt lines, or a\n"
          "WAV recording of its tones), shows what it would sign and signs after\n"
          "\"yes\" on standard input.\n"
          "tones writes a recording of a seed (a UR frame, or keypad tones with\n"
          "--keypad) or of a PSBT (a file as for sign) for testing; cable timing\n"
          "(40/20/400 ms) by default, air timing is 80/80/600 ms.\n"
          "warning: arguments are visible to other processes (ps) and are kept in\n"
          "the shell history. Use only test seeds with this tool.\n",
          stderr);
}

static const char *ut_message(int32_t code)
{
    switch (code) {
    case UT_ERR_UR: return "not a UR text";
    case UT_ERR_CHECKSUM: return "bad checksum (a wrong word, digit or character)";
    case UT_ERR_SYNC: return "no sync at the start of the tones";
    case UT_ERR_DECODE: return "the tones cannot be decoded (too many errors)";
    case UT_ERR_PIN: return "a PIN is letters and digits only";
    case UT_ERR_SEED: return "not a seed";
    case UT_ERR_TOO_LONG: return "too long for one frame";
    case UT_ERR_SPACE: return "output too small";
    default: return "invalid input";
    }
}

static const char *ds_message(int32_t code)
{
    switch (code) {
    case DS_ERR_PASSPHRASE: return "the passphrase must be printable ASCII, 256 bytes at most";
    case DS_ERR_DERIVE: return "key derivation failed";
    case DS_ERR_SEED: return "not a valid seed";
    case DS_ERR_SPACE: return "output too small";
    default: return "invalid argument";
    }
}

/* Parses the arguments after "keys" or "sign". 0 on success. */
static int parse(int argc, char **argv, options *o)
{
    int i;

    memset(o, 0, sizeof *o);
    o->sign = !strcmp(argv[1], "sign");
    for (i = 2; i < argc; i++) {
        const char *a = argv[i];
        source_kind kind = SRC_NONE;

        if (!strcmp(a, "--testnet")) {
            if (o->testnet)
                return -1;
            o->testnet = 1;
            continue;
        }
        if (o->sign && (!strcmp(a, "--psbt") || !strcmp(a, "--out"))) {
            const char **dst = !strcmp(a, "--psbt") ? &o->psbt : &o->out;
            if (i + 1 >= argc || *dst != NULL)
                return -1;
            *dst = argv[++i];
            continue;
        }
        if (!strcmp(a, "--ur")) kind = SRC_UR;
        else if (!strcmp(a, "--keypad")) kind = SRC_KEYPAD;
        else if (!strcmp(a, "--frame")) kind = SRC_FRAME;
        else if (!strcmp(a, "--wav")) kind = SRC_WAV;
        else if (strcmp(a, "--pin") && strcmp(a, "--passphrase"))
            return -1;
        if (i + 1 >= argc)
            return -1;
        if (kind != SRC_NONE) {
            if (o->kind != SRC_NONE)
                return -1;
            o->kind = kind;
            o->value = argv[++i];
        } else if (!strcmp(a, "--pin")) {
            if (o->pin != NULL)
                return -1;
            o->pin = argv[++i];
        } else {
            if (o->passphrase != NULL)
                return -1;
            o->passphrase = argv[++i];
        }
    }
    return o->kind == SRC_NONE || (o->sign && o->psbt == NULL) ? -1 : 0;
}

/* The entropy of one group of tones: keypad mode if it starts with '*', else
 * a frame. Returns its length or a negative error. */
static int32_t group_to_seed(const char *group, const char *pin, uint8_t *entropy)
{
    char ur[UT_MAX_UR + 1];
    int32_t n;

    if (group[0] == '*')
        n = ut_keypad_to_seed(group, pin, entropy);
    else if ((n = ut_frame_to_ur(group, ur, sizeof ur, NULL)) >= 0)
        n = ut_ur_to_seed(ur, pin, entropy);
    ds_wipe(ur, sizeof ur);
    return n;
}

/* The first group of tones in the recording that holds a seed. */
static int32_t wav_to_seed(const char *path, const char *pin, uint8_t *entropy)
{
    static const char *const none = "no seed found in the recording";
    ut_listener *l = NULL;
    int16_t *samples = NULL;
    char group[UT_GROUP + 1];
    uint32_t rate = 0;
    int32_t count, n = UT_ERR_SEED, got;
    size_t pos, step, count_cap = 0;
    int flushed = 0;

    count = ds_wav_read(path, NULL, 0, &rate);
    if (count < 0) {
        fprintf(stderr, "dtmf-signer-host: cannot read %s as a 16-bit PCM WAV\n", path);
        return UT_ERR_ARG;
    }
    count_cap = (size_t)count;
    samples = malloc(count_cap * sizeof *samples + 1);
    l = malloc(sizeof *l);
    if (samples == NULL || l == NULL) {
        fputs("dtmf-signer-host: out of memory\n", stderr);
        n = UT_ERR_ARG;
        goto done;
    }
    count = ds_wav_read(path, samples, (size_t)count, &rate);
    if (count < 0) {
        fprintf(stderr, "dtmf-signer-host: cannot read %s as a 16-bit PCM WAV\n", path);
        n = UT_ERR_ARG;
        goto done;
    }
    ut_listener_init(l, rate);
    step = rate / 10 + 1; /* push in short pieces: groups are taken as they come */
    for (pos = 0; !flushed; pos += step) {
        if (pos < (size_t)count) {
            size_t k = (size_t)count - pos < step ? (size_t)count - pos : step;
            ut_listener_push(l, samples + pos, k);
        } else {
            ut_listener_flush(l);
            flushed = 1;
        }
        while ((got = ut_listener_group(l, group, sizeof group)) > 0) {
            int32_t r = group_to_seed(group, pin, entropy);
            if (r > 0) {
                n = r;
                goto done;
            }
        }
    }
    fprintf(stderr, "dtmf-signer-host: %s\n", none);
done:
    ds_wipe(group, sizeof group);
    if (samples != NULL) {
        ds_wipe(samples, (size_t)count_cap * sizeof *samples + 1);
        free(samples);
    }
    if (l != NULL) {
        ds_wipe(l, sizeof *l);
        free(l);
    }
    return n;
}

static void print_fp(const char *name, uint32_t fp)
{
    printf("%s %08x\n", name, (unsigned)fp);
}

/* ---- keys ---------------------------------------------------------------- */

static int do_keys(const uint8_t *entropy, size_t n, const char *pass, int testnet)
{
    char text[512];
    ds_wallet w;
    int32_t r;
    int exit_code = EXIT_FAIL;

    memset(&w, 0, sizeof w);
    memset(text, 0, sizeof text);
    r = ds_wallet_load(&w, entropy, n, pass, testnet);
    if (r < 0) {
        fprintf(stderr, "dtmf-signer-host: %s\n", ds_message(r));
        goto done;
    }
    if (ds_wallet_xpub(&w, text, sizeof text) < 0) {
        fputs("dtmf-signer-host: cannot write the xpub\n", stderr);
        goto done;
    }
    print_fp("seed_fingerprint", w.seed_fp);
    print_fp("wallet_fingerprint", w.wallet_fp);
    printf("xpub %s\n", text);
    if (ds_wallet_descriptor(&w, text, sizeof text) < 0) {
        fputs("dtmf-signer-host: cannot write the descriptor\n", stderr);
        goto done;
    }
    printf("descriptor %s\n", text);
    exit_code = 0;
done:
    ds_wallet_wipe(&w);
    ds_wipe(text, sizeof text);
    return exit_code;
}

/* ---- sign ---------------------------------------------------------------- */

/* The whole file at path in a new buffer (NUL-terminated beyond *len), or
 * NULL after a message. */
static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL, *bigger;
    size_t cap = 0, n = 0, got;

    if (f == NULL) {
        fprintf(stderr, "dtmf-signer-host: cannot open %s\n", path);
        return NULL;
    }
    for (;;) {
        if (cap - n < 4096) {
            cap = cap ? cap * 2 : 16384;
            if (cap > PSBT_FILE_MAX + 1)
                cap = PSBT_FILE_MAX + 1;
            bigger = cap > n + 1 ? realloc(buf, cap) : NULL;
            if (bigger == NULL) {
                fprintf(stderr, "dtmf-signer-host: %s is too big\n", path);
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = bigger;
        }
        got = fread(buf + n, 1, cap - 1 - n, f);
        n += got;
        if (got == 0)
            break;
    }
    if (ferror(f)) {
        fprintf(stderr, "dtmf-signer-host: cannot read %s\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* The PSBT in UR text lines (one part or many) as bytes. Modifies text. */
static uint8_t *psbt_from_urs(char *text, size_t text_len, size_t *len)
{
    ut_fountain_decoder *d = malloc(sizeof *d);
    size_t work_len = UT_FOUNTAIN_WORK(text_len);
    uint8_t *work = malloc(work_len), *psbt = NULL;
    const char *type;
    const uint8_t *msg, *bytes;
    size_t msg_len, bytes_len;
    int32_t r = 0;
    char *line, *next;

    if (d == NULL || work == NULL) {
        fputs("dtmf-signer-host: out of memory\n", stderr);
        goto done;
    }
    ut_fountain_decoder_init(d, work, work_len);
    for (line = text; line != NULL && r == 0; line = next) {
        char *end;
        next = strchr(line, '\n');
        if (next != NULL)
            *next++ = '\0';
        end = line + strlen(line);
        while (end > line && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
        while (*line == ' ' || *line == '\t')
            line++;
        if (*line == '\0')
            continue;
        if (!starts_with(line, "ur:crypto-psbt/") && !starts_with(line, "ur:psbt/")) {
            fputs("dtmf-signer-host: not a PSBT UR line\n", stderr);
            goto done;
        }
        r = ut_fountain_receive(d, line);
        if (r < 0) {
            fprintf(stderr, "dtmf-signer-host: %s\n", ut_message(r));
            goto done;
        }
    }
    if (r != 1) {
        fputs("dtmf-signer-host: the UR parts are not enough to rebuild the PSBT\n", stderr);
        goto done;
    }
    if (ut_fountain_result(d, &type, &msg, &msg_len) < 0 ||
        (strcmp(type, "crypto-psbt") && strcmp(type, "psbt")) ||
        ut_cbor_to_psbt(msg, msg_len, &bytes, &bytes_len) < 0 || bytes_len == 0) {
        fputs("dtmf-signer-host: the UR does not hold a PSBT\n", stderr);
        goto done;
    }
    psbt = malloc(bytes_len);
    if (psbt == NULL) {
        fputs("dtmf-signer-host: out of memory\n", stderr);
        goto done;
    }
    memcpy(psbt, bytes, bytes_len);
    *len = bytes_len;
done:
    free(d);
    free(work);
    return psbt;
}

/* The PSBT heard in a WAV recording of its tones, as bytes in a new buffer;
 * NULL after a message. */
static uint8_t *psbt_from_wav(const char *path, size_t *len)
{
    ds_rx *rx = malloc(sizeof *rx); /* about 40 KB: never on the stack */
    int16_t *samples = NULL;
    uint8_t *psbt = NULL;
    const uint8_t *bytes;
    size_t bytes_len, pos, step, k;
    uint32_t rate = 0;
    int32_t count, r = DS_RX_NONE;
    size_t cap = 0;

    count = ds_wav_read(path, NULL, 0, &rate);
    if (count < 0) {
        fprintf(stderr, "dtmf-signer-host: cannot read %s as a 16-bit PCM WAV\n", path);
        goto done;
    }
    cap = (size_t)count;
    samples = malloc(cap * sizeof *samples + 1);
    if (rx == NULL || samples == NULL) {
        fputs("dtmf-signer-host: out of memory\n", stderr);
        goto done;
    }
    count = ds_wav_read(path, samples, (size_t)count, &rate);
    if (count < 0) {
        fprintf(stderr, "dtmf-signer-host: cannot read %s as a 16-bit PCM WAV\n", path);
        goto done;
    }
    ds_rx_init(rx, rate, DS_RX_PSBT);
    step = rate / 10 + 1; /* short pieces: groups are taken as they come */
    for (pos = 0; pos < (size_t)count && r != DS_RX_DONE; pos += step) {
        k = (size_t)count - pos < step ? (size_t)count - pos : step;
        ds_rx_push(rx, samples + pos, k);
        while (r != DS_RX_DONE && (r = ds_rx_poll(rx)) != DS_RX_NONE)
            ;
        if (r == DS_RX_NONE)
            r = DS_RX_PART; /* not done: keep listening */
    }
    if (r != DS_RX_DONE || ds_rx_psbt(rx, &bytes, &bytes_len) < 0 || bytes_len == 0) {
        fputs("dtmf-signer-host: no complete PSBT found in the recording\n", stderr);
        goto done;
    }
    psbt = malloc(bytes_len);
    if (psbt == NULL) {
        fputs("dtmf-signer-host: out of memory\n", stderr);
        goto done;
    }
    memcpy(psbt, bytes, bytes_len);
    *len = bytes_len;
done:
    if (rx != NULL) {
        ds_rx_wipe(rx);
        free(rx);
    }
    if (samples != NULL) { /* a seed recording given by mistake is seed audio */
        ds_wipe(samples, cap * sizeof *samples + 1);
        free(samples);
    }
    return psbt;
}

/* True if the file starts with the RIFF tag of a WAV. */
static int is_wav(const char *path)
{
    char tag[4];
    FILE *f = fopen(path, "rb");
    int yes;

    if (f == NULL)
        return 0;
    yes = fread(tag, 1, sizeof tag, f) == sizeof tag && !memcmp(tag, "RIFF", 4);
    fclose(f);
    return yes;
}

/* The PSBT of a file, binary, base64, UR text or a WAV of its tones, as bytes
 * in a new buffer; NULL after a message. */
static uint8_t *load_psbt(const char *path, size_t *len)
{
    static const uint8_t magic[5] = {0x70, 0x73, 0x62, 0x74, 0xff};
    size_t n = 0;
    uint8_t *raw, *psbt = NULL;
    char *text;

    if (is_wav(path))
        return psbt_from_wav(path, len);
    raw = read_file(path, &n);
    if (raw == NULL)
        return NULL;
    if (n >= sizeof magic && !memcmp(raw, magic, sizeof magic)) {
        *len = n;
        return raw;
    }
    text = (char *)raw;
    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t')
        text++;
    n -= (size_t)(text - (char *)raw);
    if (starts_with(text, "cHNidP")) {
        int32_t got;
        psbt = malloc(n);
        got = psbt == NULL ? -1 : ds_base64_decode(text, n, psbt, n);
        if (got <= 0) {
            fputs("dtmf-signer-host: the PSBT is not valid base64\n", stderr);
            free(psbt);
            psbt = NULL;
        } else {
            *len = (size_t)got;
        }
    } else if (starts_with(text, "ur:")) {
        psbt = psbt_from_urs(text, n, len);
    } else {
        fputs("dtmf-signer-host: the file holds no PSBT (base64, binary or ur:crypto-psbt)\n",
              stderr);
    }
    free(raw);
    return psbt;
}

/* The PSBT as a fixed buffer filled before the review: the ds_source contract. */
typedef struct {
    const uint8_t *p;
    size_t len;
} mem_src;

static int32_t mem_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    const mem_src *m = ctx;

    if (off > m->len || n > m->len - off)
        return DS_ERR_ARG;
    memcpy(buf, m->p + off, n);
    return (int32_t)n;
}

typedef struct {
    uint8_t *p;
    size_t len, cap;
} mem_sink;

static int32_t mem_write(void *ctx, const uint8_t *p, size_t n)
{
    mem_sink *m = ctx;

    if (n > m->cap - m->len)
        return DS_ERR_SPACE;
    memcpy(m->p + m->len, p, n);
    m->len += n;
    return DS_OK;
}

/* Reads the answer: only the line "yes" confirms. */
static int user_says_yes(void)
{
    char line[16];
    size_t n;

    fputs("Type yes to sign: ", stderr);
    fflush(stderr);
    if (fgets(line, sizeof line, stdin) == NULL)
        return 0;
    n = strlen(line);
    if (n > 0 && line[n - 1] == '\n')
        line[--n] = '\0';
    return strcmp(line, "yes") == 0;
}

static int emit(const char *base64, const char *path)
{
    FILE *f = stdout;

    if (path != NULL && (f = fopen(path, "wb")) == NULL) {
        fprintf(stderr, "dtmf-signer-host: cannot write %s\n", path);
        return -1;
    }
    fprintf(f, "%s\n", base64);
    if (fflush(f) != 0 || (path != NULL && fclose(f) != 0)) {
        fputs("dtmf-signer-host: cannot write the signed PSBT\n", stderr);
        return -1;
    }
    return 0;
}

/* The signed PSBT is kept in memory and only written out if the core says
 * DS_OK: on any error nothing reaches stdout or the file. */
static int do_sign(const options *o, const uint8_t *entropy, size_t n, const char *pass)
{
    ds_session s;
    mem_src in;
    mem_sink out;
    ds_source src;
    ds_sink dst;
    char summary[SUMMARY_MAX];
    char *text = NULL;
    uint8_t *psbt;
    size_t psbt_len = 0;
    int32_t r;
    int exit_code = EXIT_FAIL;

    memset(&s, 0, sizeof s);
    memset(&out, 0, sizeof out);
    psbt = load_psbt(o->psbt, &psbt_len);
    if (psbt == NULL)
        return EXIT_FAIL;
    in.p = psbt;
    in.len = psbt_len;
    src.read = mem_read;
    src.ctx = &in;
    src.len = (uint32_t)psbt_len;
    /* room for the signatures the core adds: 128 bytes each, at most DS_MAX_INPUTS */
    out.cap = psbt_len + 128u * 64u;
    out.p = malloc(out.cap);
    text = malloc(out.cap / 3 * 4 + 8);
    if (out.p == NULL || text == NULL) {
        fputs("dtmf-signer-host: out of memory\n", stderr);
        goto done;
    }
    dst.write = mem_write;
    dst.ctx = &out;

    ds_session_init(&s, o->testnet);
    /* a seed from the command line or a file: the cable (a test tool) */
    r = ds_session_seed(&s, entropy, n, o->pin != NULL, o->pin != NULL ? strlen(o->pin) : 0,
                        DS_CABLE, 0);
    if (r == DS_WARN_AIR)
        fputs("dtmf-signer-host: warning: the seed came through the air\n", stderr);
    else if (r != DS_OK)
        goto ds_error;
    if ((r = ds_session_passphrase(&s, pass, DS_CABLE, 0)) != DS_OK)
        goto ds_error;
    if ((r = ds_session_review(&s, &src, DS_CABLE)) != DS_OK)
        goto ds_error;
    if ((r = ds_psbt_summary_text(&s.r, summary, sizeof summary)) < 0)
        goto ds_error;
    fputs(summary, stderr); /* the review comes before the question */
    if (s.r.n_ours == 0) { /* not a signature: fail before asking */
        ds_session_cancel(&s);
        fputs("dtmf-signer-host: nothing to sign: no input of this wallet\n", stderr);
        goto done;
    }
    if (!user_says_yes()) {
        ds_session_cancel(&s);
        fputs("dtmf-signer-host: not confirmed, nothing signed\n", stderr);
        exit_code = EXIT_REFUSED;
        goto done;
    }
    if ((r = ds_session_confirm(&s)) != DS_OK)
        goto ds_error;
    if ((r = ds_session_sign(&s, &src, &dst)) != DS_OK)
        goto ds_error;
    if (ds_base64_encode(out.p, out.len, text, out.cap / 3 * 4 + 8) < 0 ||
        emit(text, o->out) != 0)
        goto done;
    exit_code = 0;
    goto done;
ds_error:
    fprintf(stderr, "dtmf-signer-host: %s\n", ds_error_name(r));
done:
    ds_session_wipe(&s);
    ds_wipe(summary, sizeof summary);
    free(psbt);
    free(out.p);
    free(text);
    return exit_code;
}

/* ---- tones --------------------------------------------------------------- */

typedef struct {
    int16_t *p;
    size_t len, cap;
} audio_buf;

/* Appends the audio of one group of tones (a pause, the tones, a pause). */
static int audio_add(audio_buf *a, const char *tones, uint32_t rate, const ut_timing *t)
{
    ut_synth s;
    size_t got;

    ut_synth_init(&s, tones, rate, t->tone_ms, t->gap_ms, t->pause_ms);
    for (;;) {
        if (a->cap - a->len < 4096) {
            size_t cap = a->cap ? a->cap * 2 : 65536;
            /* a new block, not realloc: the old one may hold seed tones */
            int16_t *bigger = malloc(cap * sizeof *a->p);
            if (bigger == NULL) {
                fputs("dtmf-signer-host: out of memory\n", stderr);
                return -1;
            }
            if (a->p != NULL) {
                memcpy(bigger, a->p, a->len * sizeof *a->p);
                ds_wipe(a->p, a->cap * sizeof *a->p);
                free(a->p);
            }
            a->p = bigger;
            a->cap = cap;
        }
        got = ut_synth_read(&s, a->p + a->len, a->cap - a->len);
        if (got == 0)
            return 0;
        a->len += got;
    }
}

/* The PSBT as audio: its CBOR as a fountain code with 100-byte fragments, the
 * first seq_len parts plus TONES_EXTRA_PARTS mixed ones, so that a receiver
 * that misses one frame can still rebuild it. */
#define TONES_EXTRA_PARTS 2
#define TONES_FRAGMENT 100

static int psbt_audio(const uint8_t *psbt, size_t len, uint32_t rate, const ut_timing *t,
                      audio_buf *a)
{
    static ut_fountain_encoder e;
    uint8_t *cbor = malloc(len + 64);
    char group[UT_GROUP + 1];
    ut_frame f;
    int32_t n;
    uint32_t i;
    int ok = -1;

    if (cbor == NULL) {
        fputs("dtmf-signer-host: out of memory\n", stderr);
        return -1;
    }
    if ((n = ut_psbt_to_cbor(psbt, len, cbor, len + 64)) < 0 ||
        (n = ut_fountain_encoder_init(&e, cbor, (size_t)n, TONES_FRAGMENT)) < 0) {
        fprintf(stderr, "dtmf-signer-host: %s\n", ut_message(n));
        goto done;
    }
    for (i = 0; i < e.seq_len + TONES_EXTRA_PARTS; i++) {
        if ((n = ut_fountain_next_frame(&e, "crypto-psbt", &f)) < 0 ||
            (n = ut_frame_encode(&f, group, sizeof group)) < 0) {
            fprintf(stderr, "dtmf-signer-host: %s\n", ut_message(n));
            goto done;
        }
        if (audio_add(a, group, rate, t) != 0)
            goto done;
    }
    ok = 0;
done:
    free(cbor);
    return ok;
}

/* A hex string of 2 * cap digits at most as bytes; the length, or -1. */
static int hex_bytes(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = strlen(hex), i;

    if (n == 0 || n % 2 != 0 || n / 2 > cap)
        return -1;
    for (i = 0; i < n; i++) {
        char c = hex[i];
        int v = c >= '0' && c <= '9' ? c - '0'
              : c >= 'a' && c <= 'f' ? c - 'a' + 10
              : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0)
            return -1;
        if (i % 2 == 0)
            out[i / 2] = (uint8_t)(v << 4);
        else
            out[i / 2] |= (uint8_t)v;
    }
    return (int)(n / 2);
}

/* tones seed|psbt: argv[2] is the kind, argv[3] its argument. Any argument
 * may hold a secret, so all of them are wiped on every path. */
static int tones_main(int argc, char **argv)
{
    static const ut_timing cable = UT_TIMING_CABLE, air = UT_TIMING_AIR;
    const ut_timing *timing = &cable;
    const char *out = NULL, *kind, *arg;
    char *pin = NULL;
    uint32_t rate = 16000;
    int keypad = 0, seed, have_rate = 0, have_timing = 0, i;
    int exit_code = EXIT_FAIL;
    uint8_t entropy[32];
    char group[UT_GROUP + 1];
    audio_buf a;
    int usage_error = 1;

    memset(&a, 0, sizeof a);
    memset(entropy, 0, sizeof entropy);
    memset(group, 0, sizeof group);
    if (argc < 4)
        goto done;
    kind = argv[2];
    arg = argv[3];
    if (!strcmp(kind, "seed"))
        seed = 1;
    else if (!strcmp(kind, "psbt"))
        seed = 0;
    else
        goto done;
    for (i = 4; i < argc; i++) {
        const char *o = argv[i];
        if (!strcmp(o, "--keypad") && seed && !keypad) {
            keypad = 1;
        } else if (i + 1 < argc && !strcmp(o, "--out") && out == NULL) {
            out = argv[++i];
        } else if (i + 1 < argc && !strcmp(o, "--pin") && seed && pin == NULL) {
            pin = argv[++i];
        } else if (i + 1 < argc && !strcmp(o, "--rate") && !have_rate) {
            const char *v = argv[++i];
            if (!strcmp(v, "16000")) rate = 16000;
            else if (!strcmp(v, "44100")) rate = 44100;
            else goto done;
            have_rate = 1;
        } else if (i + 1 < argc && !strcmp(o, "--timing") && !have_timing) {
            const char *v = argv[++i];
            if (!strcmp(v, "cable")) timing = &cable;
            else if (!strcmp(v, "air")) timing = &air;
            else goto done;
            have_timing = 1;
        } else {
            goto done;
        }
    }
    if (out == NULL)
        goto done;
    usage_error = 0;

    if (seed) {
        int n = hex_bytes(arg, entropy, sizeof entropy);
        int32_t r;
        if (n < 0) {
            fputs("dtmf-signer-host: the entropy is 16 or 32 bytes of hex\n", stderr);
            goto done;
        }
        if (keypad) {
            r = ut_seed_to_keypad(entropy, (size_t)n, pin, group, sizeof group);
        } else {
            char ur[UT_MAX_UR + 1];
            r = ut_seed_to_ur(entropy, (size_t)n, pin, ur, sizeof ur);
            if (r >= 0)
                r = ut_ur_to_frame(ur, group, sizeof group);
            ds_wipe(ur, sizeof ur);
        }
        if (r < 0) {
            fprintf(stderr, "dtmf-signer-host: %s\n", ut_message(r));
            goto done;
        }
        if (audio_add(&a, group, rate, timing) != 0)
            goto done;
    } else {
        size_t len = 0;
        uint8_t *psbt = load_psbt(arg, &len);
        int r;
        if (psbt == NULL)
            goto done;
        r = psbt_audio(psbt, len, rate, timing, &a);
        free(psbt);
        if (r != 0)
            goto done;
    }
    if (ds_wav_write(out, a.p, a.len, rate) != DS_OK) {
        fprintf(stderr, "dtmf-signer-host: cannot write %s\n", out);
        goto done;
    }
    exit_code = 0;
done:
    /* the tones of a seed are the seed: wipe them with the entropy */
    if (a.p != NULL) {
        ds_wipe(a.p, a.cap * sizeof *a.p);
        free(a.p);
    }
    ds_wipe(entropy, sizeof entropy);
    ds_wipe(group, sizeof group);
    for (i = 2; i < argc; i++)
        ds_wipe(argv[i], strlen(argv[i]));
    if (usage_error) {
        usage();
        return EXIT_USAGE;
    }
    return exit_code;
}

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char **argv)
{
    options o;
    uint8_t entropy[32];
    char pass[PASSPHRASE_MAX + 1];
    int32_t n;
    int i;
    int exit_code = EXIT_FAIL;
    const char *pass_arg;

    if (argc >= 2 && !strcmp(argv[1], "tones"))
        return tones_main(argc, argv);
    memset(pass, 0, sizeof pass);
    memset(entropy, 0, sizeof entropy);
    if (argc < 2 || (strcmp(argv[1], "keys") && strcmp(argv[1], "sign")) ||
        parse(argc, argv, &o) != 0) {
        usage();
        /* Any argument may hold a seed, PIN or passphrase: wipe them all. */
        for (i = 2; i < argc; i++)
            ds_wipe(argv[i], strlen(argv[i]));
        return EXIT_USAGE;
    }
    if (o.passphrase != NULL && strlen(o.passphrase) > PASSPHRASE_MAX) {
        fprintf(stderr, "dtmf-signer-host: %s\n", ds_message(DS_ERR_PASSPHRASE));
        goto done;
    }
    if (o.passphrase != NULL)
        memcpy(pass, o.passphrase, strlen(o.passphrase));
    pass_arg = o.passphrase != NULL ? pass : NULL;

    switch (o.kind) {
    case SRC_UR:
        n = ut_ur_to_seed(o.value, o.pin, entropy);
        break;
    case SRC_KEYPAD:
        n = ut_keypad_to_seed(o.value, o.pin, entropy);
        break;
    case SRC_FRAME:
        n = group_to_seed(o.value, o.pin, entropy);
        break;
    default:
        n = wav_to_seed(o.value, o.pin, entropy);
        break;
    }
    if (n < 0) {
        if (o.kind != SRC_WAV)
            fprintf(stderr, "dtmf-signer-host: %s\n", ut_message(n));
        goto done;
    }
    exit_code = o.sign ? do_sign(&o, entropy, (size_t)n, pass_arg)
                       : do_keys(entropy, (size_t)n, pass_arg, o.testnet);
done:
    ds_wipe(entropy, sizeof entropy);
    ds_wipe(pass, sizeof pass);
    if (o.passphrase != NULL)
        ds_wipe(o.passphrase, strlen(o.passphrase));
    if (o.pin != NULL)
        ds_wipe(o.pin, strlen(o.pin));
    if (o.value != NULL && o.kind != SRC_WAV) /* the seed as an argument */
        ds_wipe((char *)o.value, strlen(o.value));
    return exit_code;
}
