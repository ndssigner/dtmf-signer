/* dtmf-signer-host: the computer build. Reads a seed (a ur-tones UR, keypad
 * tones, a frame of tones or a WAV recording) and prints its fingerprints,
 * the BIP-84 account xpub and the descriptor.
 *
 * Exit codes: 0 done, 1 error (message on stderr), 2 wrong usage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ds_wallet.h"
#include "ur_tones.h"
#include "wav.h"

#define EXIT_FAIL 1
#define EXIT_USAGE 2
#define PASSPHRASE_MAX 256

typedef enum { SRC_NONE, SRC_UR, SRC_KEYPAD, SRC_FRAME, SRC_WAV } source_kind;

typedef struct {
    source_kind kind;
    const char *value; /* the UR, the tones or the file name */
    char *pin;         /* points into argv */
    char *passphrase;  /* points into argv */
    int testnet;
} options;

static void usage(void)
{
    fputs("usage: dtmf-signer-host keys (--ur UR | --keypad TONES | --frame TONES | --wav FILE)\n"
          "                            [--pin PIN] [--passphrase TEXT] [--testnet]\n"
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

/* Parses the arguments after "keys". 0 on success. */
static int parse(int argc, char **argv, options *o)
{
    int i;

    memset(o, 0, sizeof *o);
    for (i = 2; i < argc; i++) {
        const char *a = argv[i];
        source_kind kind = SRC_NONE;

        if (!strcmp(a, "--testnet")) {
            if (o->testnet)
                return -1;
            o->testnet = 1;
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
    return o->kind == SRC_NONE ? -1 : 0;
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

int main(int argc, char **argv)
{
    options o;
    uint8_t entropy[32];
    char pass[PASSPHRASE_MAX + 1];
    char text[512];
    ds_wallet w;
    int32_t n, r;
    int i;
    int exit_code = EXIT_FAIL;
    const char *pass_arg;

    memset(&w, 0, sizeof w);
    memset(pass, 0, sizeof pass);
    memset(entropy, 0, sizeof entropy);
    if (argc < 2 || strcmp(argv[1], "keys") || parse(argc, argv, &o) != 0) {
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
    r = ds_wallet_load(&w, entropy, (size_t)n, pass_arg, o.testnet);
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
    ds_wipe(entropy, sizeof entropy);
    ds_wipe(pass, sizeof pass);
    ds_wipe(text, sizeof text);
    if (o.passphrase != NULL)
        ds_wipe(o.passphrase, strlen(o.passphrase));
    if (o.pin != NULL)
        ds_wipe(o.pin, strlen(o.pin));
    if (o.value != NULL && o.kind != SRC_WAV) /* the seed as an argument */
        ds_wipe((char *)o.value, strlen(o.value));
    return exit_code;
}
