#include <stdint.h>
#include <string.h>

#include "ds_session.h"
#include "psbt_vectors.h"
#include "test.h"

#define N_VECTORS (sizeof psbt_vectors / sizeof psbt_vectors[0])
#define BUF 4096

/* ---- a source over a byte array, and a sink into one ---- */

typedef struct {
    const uint8_t *p;
    uint32_t len;
} mem_src;

static int32_t mem_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    mem_src *m = ctx;
    if (off > m->len || n > m->len - off) {
        return DS_ERR_ARG;
    }
    memcpy(buf, m->p + off, n);
    return (int32_t)n;
}

static ds_source source(mem_src *m, const uint8_t *p, size_t len)
{
    ds_source s;
    m->p = p;
    m->len = (uint32_t)len;
    s.read = mem_read;
    s.ctx = m;
    s.len = (uint32_t)len;
    return s;
}

typedef struct {
    uint8_t p[BUF];
    size_t len;
} mem_sink;

static int32_t mem_write(void *ctx, const uint8_t *p, size_t n)
{
    mem_sink *m = ctx;
    if (n > sizeof m->p - m->len) {
        return DS_ERR_SPACE;
    }
    memcpy(m->p + m->len, p, n);
    m->len += n;
    return DS_OK;
}

static ds_sink sink(mem_sink *m)
{
    ds_sink s;
    memset(m, 0, sizeof *m);
    s.write = mem_write;
    s.ctx = m;
    return s;
}

/* ---- helpers ---- */

static const psbt_vector *find(const char *name)
{
    size_t k;
    for (k = 0; k < N_VECTORS; k++) {
        if (strcmp(psbt_vectors[k].name, name) == 0) {
            return &psbt_vectors[k];
        }
    }
    printf("vector not found: %s\n", name);
    ds_failures++;
    return NULL;
}

static int same_wallet(const psbt_vector *a, const psbt_vector *b)
{
    return a->entropy_len == b->entropy_len &&
           memcmp(a->entropy, b->entropy, a->entropy_len) == 0 &&
           a->testnet == b->testnet &&
           ((a->passphrase == NULL && b->passphrase == NULL) ||
            (a->passphrase != NULL && b->passphrase != NULL &&
             strcmp(a->passphrase, b->passphrase) == 0));
}

/* A signable vector other than a, for the same wallet; a itself if none. */
static const psbt_vector *other_for(const psbt_vector *a)
{
    size_t k;
    for (k = 0; k < N_VECTORS; k++) {
        const psbt_vector *v = &psbt_vectors[k];
        if (v != a && v->sign && same_wallet(a, v) &&
            (v->psbt_len != a->psbt_len ||
             memcmp(v->psbt, a->psbt, a->psbt_len) != 0)) {
            return v;
        }
    }
    return a;
}

/* A session for the vector's wallet, in READY, through the cable. */
static void ready(ds_session *s, const psbt_vector *v)
{
    ds_session_init(s, v->testnet);
    CHECK_EQ_INT(ds_session_seed(s, v->entropy, v->entropy_len, 0, 0, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(ds_session_passphrase(s, v->passphrase, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(s->state, DS_S_READY);
}

static int32_t review(ds_session *s, const uint8_t *p, size_t len, int channel)
{
    mem_src m;
    ds_source src = source(&m, p, len);
    return ds_session_review(s, &src, channel);
}

static int32_t sign(ds_session *s, const uint8_t *p, size_t len, mem_sink *out)
{
    mem_src m;
    ds_source src = source(&m, p, len);
    ds_sink d = sink(out);
    return ds_session_sign(s, &src, &d);
}

static int is_zero(const void *p, size_t n)
{
    const unsigned char *b = p;
    size_t k;
    for (k = 0; k < n; k++) {
        if (b[k] != 0) {
            return 0;
        }
    }
    return 1;
}

/* ---- the policy table ---- */

static void test_policy_table(void)
{
    /* rows: data; columns: AIR, CABLE, CABLE_TERMINAL, LOCAL. The encrypted
     * seed is taken with a 12-character PIN. */
    static const struct {
        int data;
        int32_t air, cable, terminal, local;
    } t[] = {
        {DS_DATA_PSBT, DS_OK, DS_OK, DS_OK, DS_ERR_POLICY},
        {DS_DATA_SEED_ENCRYPTED, DS_WARN_AIR, DS_OK, DS_NEED_CONFIRM, DS_ERR_POLICY},
        {DS_DATA_SEED_PLAIN, DS_ERR_POLICY, DS_OK, DS_NEED_CONFIRM, DS_ERR_POLICY},
        {DS_DATA_PIN, DS_ERR_POLICY, DS_OK, DS_NEED_CONFIRM, DS_OK},
        {DS_DATA_PASSPHRASE, DS_ERR_POLICY, DS_OK, DS_NEED_CONFIRM, DS_OK},
    };
    size_t k;

    for (k = 0; k < sizeof t / sizeof t[0]; k++) {
        CHECK_EQ_INT(ds_policy_accepts(t[k].data, DS_AIR, 12), t[k].air);
        CHECK_EQ_INT(ds_policy_accepts(t[k].data, DS_CABLE, 12), t[k].cable);
        CHECK_EQ_INT(ds_policy_accepts(t[k].data, DS_CABLE_TERMINAL, 12), t[k].terminal);
        CHECK_EQ_INT(ds_policy_accepts(t[k].data, DS_LOCAL, 12), t[k].local);
    }
}

static void test_policy_pin_length_boundary(void)
{
    CHECK_EQ_INT(ds_policy_accepts(DS_DATA_SEED_ENCRYPTED, DS_AIR, 11), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_policy_accepts(DS_DATA_SEED_ENCRYPTED, DS_AIR, 0), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_policy_accepts(DS_DATA_SEED_ENCRYPTED, DS_AIR, 12), DS_WARN_AIR);
    CHECK_EQ_INT(ds_policy_accepts(DS_DATA_SEED_ENCRYPTED, DS_AIR, 13), DS_WARN_AIR);
    /* The PIN length matters only for the encrypted seed on the air. */
    CHECK_EQ_INT(ds_policy_accepts(DS_DATA_SEED_ENCRYPTED, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(ds_policy_accepts(DS_DATA_PSBT, DS_AIR, 0), DS_OK);
}

static void test_policy_unknown(void)
{
    CHECK_EQ_INT(ds_policy_accepts(0, DS_CABLE, 12), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_policy_accepts(99, DS_CABLE, 12), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_policy_accepts(DS_DATA_PSBT, 0, 12), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_policy_accepts(DS_DATA_PSBT, 99, 12), DS_ERR_POLICY);
}

/* ---- seed and passphrase ---- */

static void test_session_init(void)
{
    ds_session s;
    memset(&s, 0xAA, sizeof s);
    ds_session_init(&s, 1);
    CHECK_EQ_INT(s.state, DS_S_EMPTY);
    CHECK_EQ_INT(s.testnet, 1);
    CHECK(is_zero(s.entropy, sizeof s.entropy));
}

static void test_session_seed_policy(void)
{
    ds_session s;
    uint8_t e[16] = {1};

    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_AIR, 0), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_LOCAL, 0), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 1, 11, DS_AIR, 0), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 1, 12, DS_LOCAL, 0), DS_ERR_POLICY);
    /* Refused: the session is untouched, no entropy kept. */
    CHECK_EQ_INT(s.state, DS_S_EMPTY);
    CHECK(is_zero(s.entropy, sizeof s.entropy));
    CHECK_EQ_INT(s.entropy_len, 0);
}

static void test_session_seed_encrypted_by_air_warns(void)
{
    ds_session s;
    uint8_t e[16] = {1};

    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 1, 12, DS_AIR, 0), DS_WARN_AIR);
    CHECK_EQ_INT(s.state, DS_S_SEED);
    ds_session_wipe(&s);
}

static void test_session_seed_bad_length(void)
{
    ds_session s;
    uint8_t e[32] = {1};

    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 15, 0, 0, DS_CABLE, 0), DS_ERR_ARG);
    CHECK_EQ_INT(ds_session_seed(&s, e, 24, 0, 0, DS_CABLE, 0), DS_ERR_ARG);
    CHECK_EQ_INT(ds_session_seed(&s, NULL, 16, 0, 0, DS_CABLE, 0), DS_ERR_ARG);
    CHECK_EQ_INT(s.state, DS_S_EMPTY);
    CHECK(is_zero(s.entropy, sizeof s.entropy));
    CHECK_EQ_INT(ds_session_seed(&s, e, 32, 0, 0, DS_CABLE, 0), DS_OK);
    ds_session_wipe(&s);
}

static void test_session_seed_via_terminal_needs_confirmation(void)
{
    ds_session s, empty;
    uint8_t e[16] = {1, 2, 3};

    ds_session_init(&s, 0);
    empty = s;
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE_TERMINAL, 0), DS_NEED_CONFIRM);
    CHECK(memcmp(&s, &empty, sizeof s) == 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 1, 0, DS_CABLE_TERMINAL, 0), DS_NEED_CONFIRM);
    CHECK(memcmp(&s, &empty, sizeof s) == 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE_TERMINAL, 1), DS_OK);
    CHECK_EQ_INT(s.state, DS_S_SEED);
    CHECK_EQ_INT(s.entropy_len, 16);
    CHECK(memcmp(s.entropy, e, 16) == 0);
    ds_session_wipe(&s);

    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 1, 0, DS_CABLE_TERMINAL, 1), DS_OK);
    ds_session_wipe(&s);
}

static void test_session_confirmed_does_not_bypass_policy(void)
{
    ds_session s;
    uint8_t e[16] = {1};

    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_AIR, 1), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 1, 11, DS_AIR, 1), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_LOCAL, 1), DS_ERR_POLICY);
    CHECK_EQ_INT(s.state, DS_S_EMPTY);
}

static void test_session_seed_only_from_empty(void)
{
    ds_session s;
    uint8_t e[16] = {1};

    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_ERR_STATE);
    CHECK_EQ_INT(s.state, DS_S_SEED);
    ds_session_wipe(&s);
}

static void test_session_pin_by_air_rejected(void)
{
    /* A PIN (or passphrase) is data of the passphrase kind here: the
     * session takes the passphrase; by air it is refused. */
    ds_session s;
    uint8_t e[16] = {1};

    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(ds_session_passphrase(&s, "secret", DS_AIR, 0), DS_ERR_POLICY);
    CHECK_EQ_INT(ds_session_passphrase(&s, "secret", DS_AIR, 1), DS_ERR_POLICY);
    CHECK_EQ_INT(s.state, DS_S_SEED);
    CHECK_EQ_INT(ds_policy_accepts(DS_DATA_PIN, DS_AIR, 20), DS_ERR_POLICY);
    ds_session_wipe(&s);
}

static void test_session_passphrase_paths(void)
{
    ds_session s;
    uint8_t e[16] = {0};

    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(ds_session_passphrase(&s, "TREZOR", DS_CABLE_TERMINAL, 0), DS_NEED_CONFIRM);
    CHECK_EQ_INT(s.state, DS_S_SEED);
    CHECK_EQ_INT(ds_session_passphrase(&s, "TREZOR", DS_CABLE_TERMINAL, 1), DS_OK);
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK(s.w.wallet_fp != s.w.seed_fp);
    CHECK_EQ_INT(ds_session_passphrase(&s, NULL, DS_CABLE, 0), DS_ERR_STATE);
    ds_session_wipe(&s);

    /* NULL: no passphrase, wallet equals the seed's. */
    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(ds_session_passphrase(&s, NULL, DS_AIR, 0), DS_OK);
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK_EQ_INT(s.w.wallet_fp, 0x73c5da0au);
    CHECK_EQ_INT(s.w.testnet, 0);
    ds_session_wipe(&s);

    /* Local entry and the testnet choice of ds_session_init. */
    ds_session_init(&s, 1);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(ds_session_passphrase(&s, "TREZOR", DS_LOCAL, 0), DS_OK);
    CHECK_EQ_INT(s.w.testnet, 1);
    ds_session_wipe(&s);
}

static void test_session_passphrase_rejected_can_retry(void)
{
    ds_session s;
    uint8_t e[16] = {0};

    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(ds_session_passphrase(&s, "caf\xc3\xa9", DS_CABLE, 0), DS_ERR_PASSPHRASE);
    CHECK_EQ_INT(s.state, DS_S_SEED);
    CHECK_EQ_INT(ds_session_passphrase(&s, "cafe", DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(s.state, DS_S_READY);
    ds_session_wipe(&s);
}

static void test_session_passphrase_wrong_state(void)
{
    ds_session s;
    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_passphrase(&s, NULL, DS_CABLE, 0), DS_ERR_STATE);
    CHECK_EQ_INT(s.state, DS_S_EMPTY);
}

/* ---- review, confirm, sign ---- */

static void test_session_no_review_in_early_states(void)
{
    const psbt_vector *v = find("one input with change");
    ds_session s;
    uint8_t e[16] = {0};

    if (v == NULL) {
        return;
    }
    ds_session_init(&s, 0);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_ERR_STATE);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_ERR_STATE);
    CHECK_EQ_INT(s.state, DS_S_SEED);
    ds_session_wipe(&s);
}

static void test_session_review_channels(void)
{
    const psbt_vector *v = find("one input with change");
    ds_session s;

    if (v == NULL) {
        return;
    }
    ready(&s, v);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_LOCAL), DS_ERR_POLICY);
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_AIR), DS_OK);
    CHECK_EQ_INT(s.state, DS_S_REVIEW);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE_TERMINAL), DS_OK);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(s.r.fee, v->fee);
    ds_session_wipe(&s);
}

static void test_session_review_error_leaves_ready(void)
{
    const psbt_vector *v = find("one input with change");
    const psbt_vector *bad = find("SIGHASH_NONE");
    ds_session s;

    if (v == NULL || bad == NULL) {
        return;
    }
    ready(&s, v);
    CHECK_EQ_INT(review(&s, bad->psbt, bad->psbt_len, DS_CABLE), bad->error);
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK(is_zero(&s.r, sizeof s.r));
    ds_session_wipe(&s);
}

static void test_session_no_sign_without_review(void)
{
    const psbt_vector *v = find("one input with change");
    ds_session s;
    mem_sink out;

    if (v == NULL) {
        return;
    }
    ready(&s, v);
    CHECK_EQ_INT(sign(&s, v->psbt, v->psbt_len, &out), DS_ERR_STATE);
    CHECK_EQ_INT(out.len, 0);
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_ERR_STATE);
    CHECK_EQ_INT(s.state, DS_S_READY);
    ds_session_wipe(&s);

    ds_session_init(&s, 0);
    CHECK_EQ_INT(sign(&s, v->psbt, v->psbt_len, &out), DS_ERR_STATE);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_ERR_STATE);
    CHECK_EQ_INT(s.state, DS_S_EMPTY);
}

static void test_session_no_sign_without_confirm(void)
{
    const psbt_vector *v = find("one input with change");
    ds_session s;
    mem_sink out;

    if (v == NULL) {
        return;
    }
    ready(&s, v);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(sign(&s, v->psbt, v->psbt_len, &out), DS_ERR_STATE);
    CHECK_EQ_INT(out.len, 0);
    CHECK_EQ_INT(s.state, DS_S_REVIEW); /* the review is kept */
    ds_session_wipe(&s);
}

static void test_session_new_psbt_resets_confirm(void)
{
    const psbt_vector *a = find("one input with change");
    const psbt_vector *b;
    ds_session s;
    mem_sink out;

    if (a == NULL) {
        return;
    }
    b = other_for(a);
    ready(&s, a);
    CHECK_EQ_INT(review(&s, a->psbt, a->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_OK);
    CHECK_EQ_INT(s.state, DS_S_CONFIRMED);
    CHECK_EQ_INT(review(&s, b->psbt, b->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(s.state, DS_S_REVIEW);
    CHECK_EQ_INT(sign(&s, b->psbt, b->psbt_len, &out), DS_ERR_STATE);
    CHECK_EQ_INT(out.len, 0);
    ds_session_wipe(&s);
}

static void test_session_failed_review_also_resets_confirm(void)
{
    const psbt_vector *a = find("one input with change");
    const psbt_vector *bad = find("SIGHASH_NONE");
    ds_session s;
    mem_sink out;

    if (a == NULL || bad == NULL) {
        return;
    }
    ready(&s, a);
    CHECK_EQ_INT(review(&s, a->psbt, a->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_OK);
    CHECK(review(&s, bad->psbt, bad->psbt_len, DS_CABLE) < 0);
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK_EQ_INT(sign(&s, a->psbt, a->psbt_len, &out), DS_ERR_STATE);

    /* Same for a PSBT refused by the policy. */
    CHECK_EQ_INT(review(&s, a->psbt, a->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_OK);
    CHECK_EQ_INT(review(&s, a->psbt, a->psbt_len, DS_LOCAL), DS_ERR_POLICY);
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK_EQ_INT(sign(&s, a->psbt, a->psbt_len, &out), DS_ERR_STATE);
    ds_session_wipe(&s);
}

static void test_session_reviewing_same_psbt_again_unconfirms(void)
{
    const psbt_vector *a = find("one input with change");
    ds_session s;

    if (a == NULL) {
        return;
    }
    ready(&s, a);
    CHECK_EQ_INT(review(&s, a->psbt, a->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_OK);
    CHECK_EQ_INT(review(&s, a->psbt, a->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(s.state, DS_S_REVIEW);
    ds_session_wipe(&s);
}

static void test_session_sign_then_ready(void)
{
    size_t k, signed_ok = 0;

    for (k = 0; k < N_VECTORS; k++) {
        const psbt_vector *v = &psbt_vectors[k];
        ds_session s;
        mem_sink out;

        if (!v->sign) {
            continue;
        }
        ready(&s, v);
        CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_OK);
        CHECK_EQ_INT(ds_session_confirm(&s), DS_OK);
        CHECK_EQ_INT(sign(&s, v->psbt, v->psbt_len, &out), DS_OK);
        CHECK(out.len > v->psbt_len);
        CHECK_EQ_INT(s.state, DS_S_READY);
        CHECK(is_zero(&s.r, sizeof s.r));
        /* The confirmation is spent: a second signature needs a new one. */
        CHECK_EQ_INT(sign(&s, v->psbt, v->psbt_len, &out), DS_ERR_STATE);
        CHECK_EQ_INT(out.len, 0);
        ds_session_wipe(&s);
        signed_ok++;
    }
    CHECK(signed_ok > 0);
}

static void test_session_sign_failure_returns_to_ready(void)
{
    const psbt_vector *v = find("one input with change");
    ds_session s;
    mem_sink out;
    uint8_t changed[BUF];

    if (v == NULL || v->psbt_len > sizeof changed) {
        return;
    }
    ready(&s, v);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_OK);
    memcpy(changed, v->psbt, v->psbt_len);
    changed[v->psbt_len - 1] ^= 1; /* a source that is no longer the review */
    CHECK_EQ_INT(sign(&s, changed, v->psbt_len, &out), DS_ERR_PSBT);
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK_EQ_INT(sign(&s, v->psbt, v->psbt_len, &out), DS_ERR_STATE);

    /* A failing sink does the same. */
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_OK);
    {
        mem_src m;
        ds_source src = source(&m, v->psbt, v->psbt_len);
        ds_sink d;
        mem_sink small;
        d = sink(&small);
        small.len = sizeof small.p; /* full: the first write fails */
        CHECK_EQ_INT(ds_session_sign(&s, &src, &d), DS_ERR_SPACE);
    }
    CHECK_EQ_INT(s.state, DS_S_READY);
    ds_session_wipe(&s);
}

static void test_session_cancel(void)
{
    const psbt_vector *v = find("one input with change");
    ds_session s;
    mem_sink out;
    uint8_t e[16] = {0};

    if (v == NULL) {
        return;
    }
    ready(&s, v);
    CHECK_EQ_INT(ds_session_cancel(&s), DS_OK); /* nothing to cancel */
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(ds_session_cancel(&s), DS_OK);
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK(is_zero(&s.r, sizeof s.r));
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_OK);
    CHECK_EQ_INT(ds_session_cancel(&s), DS_OK);
    CHECK_EQ_INT(s.state, DS_S_READY);
    CHECK_EQ_INT(sign(&s, v->psbt, v->psbt_len, &out), DS_ERR_STATE);
    ds_session_wipe(&s);

    /* Cancel never moves an unready session to READY. */
    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_cancel(&s), DS_ERR_STATE);
    CHECK_EQ_INT(s.state, DS_S_EMPTY);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(ds_session_cancel(&s), DS_ERR_STATE);
    CHECK_EQ_INT(s.state, DS_S_SEED);
    ds_session_wipe(&s);
}

static void test_session_wipe_zeroes(void)
{
    const psbt_vector *v = find("BIP-39 passphrase");
    ds_session s;
    size_t states;

    if (v == NULL) {
        return;
    }
    for (states = 0; states < 4; states++) {
        ready(&s, v);
        if (states >= 1) {
            CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_OK);
        }
        if (states >= 2) {
            CHECK_EQ_INT(ds_session_confirm(&s), DS_OK);
        }
        CHECK(s.entropy_len == v->entropy_len && s.state != DS_S_EMPTY);
        ds_session_wipe(&s);
        CHECK(is_zero(&s, sizeof s));
        CHECK_EQ_INT(s.state, 0);
    }

    /* Also from SEED and after a use. */
    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(&s, v->entropy, v->entropy_len, 0, 0, DS_CABLE, 0), DS_OK);
    ds_session_wipe(&s);
    CHECK(is_zero(&s, sizeof s));
}

static void test_session_wipe_then_nothing_works(void)
{
    const psbt_vector *v = find("one input with change");
    ds_session s;
    mem_sink out;

    if (v == NULL) {
        return;
    }
    ready(&s, v);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_OK);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_OK);
    ds_session_wipe(&s);
    CHECK_EQ_INT(sign(&s, v->psbt, v->psbt_len, &out), DS_ERR_STATE);
    CHECK_EQ_INT(review(&s, v->psbt, v->psbt_len, DS_CABLE), DS_ERR_STATE);
    CHECK_EQ_INT(ds_session_confirm(&s), DS_ERR_STATE);
}

static void test_session_wiped_is_unusable_until_init(void)
{
    uint8_t e[16] = {0};
    ds_session s;

    /* A testnet session, wiped: seeding it again must not give mainnet. */
    ds_session_init(&s, 1);
    ds_session_wipe(&s);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_ERR_STATE);
    CHECK_EQ_INT(ds_session_passphrase(&s, NULL, DS_CABLE, 0), DS_ERR_STATE);
    CHECK(is_zero(&s, sizeof s));

    /* init after wipe: a testnet wallet again. */
    ds_session_init(&s, 1);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(ds_session_passphrase(&s, NULL, DS_CABLE, 0), DS_OK);
    CHECK_EQ_INT(s.w.testnet, 1);
    ds_session_wipe(&s);
}

static void test_session_derive_failure_wipes_and_blocks(void)
{
    uint8_t e[16] = {0};
    ds_session s;

    ds_session_init(&s, 1);
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_OK);
    s.entropy_len = 5; /* makes the derivation fail with DS_ERR_ARG */
    CHECK_EQ_INT(ds_session_passphrase(&s, NULL, DS_CABLE, 0), DS_ERR_ARG);
    CHECK(is_zero(&s, sizeof s));
    CHECK_EQ_INT(ds_session_seed(&s, e, 16, 0, 0, DS_CABLE, 0), DS_ERR_STATE);
    CHECK(is_zero(&s, sizeof s));
}

static void test_session_null_arguments(void)
{
    ds_session s;
    ds_source src;
    ds_sink d;
    uint8_t e[16] = {0};

    memset(&src, 0, sizeof src);
    memset(&d, 0, sizeof d);
    ds_session_init(&s, 0);
    CHECK_EQ_INT(ds_session_seed(NULL, e, 16, 0, 0, DS_CABLE, 0), DS_ERR_ARG);
    CHECK_EQ_INT(ds_session_passphrase(NULL, NULL, DS_CABLE, 0), DS_ERR_ARG);
    CHECK_EQ_INT(ds_session_review(NULL, &src, DS_CABLE), DS_ERR_ARG);
    CHECK_EQ_INT(ds_session_review(&s, NULL, DS_CABLE), DS_ERR_ARG);
    CHECK_EQ_INT(ds_session_confirm(NULL), DS_ERR_ARG);
    CHECK_EQ_INT(ds_session_sign(NULL, &src, &d), DS_ERR_ARG);
    CHECK_EQ_INT(ds_session_sign(&s, NULL, &d), DS_ERR_ARG);
    CHECK_EQ_INT(ds_session_sign(&s, &src, NULL), DS_ERR_ARG);
    CHECK_EQ_INT(ds_session_cancel(NULL), DS_ERR_ARG);
    ds_session_wipe(NULL);
    ds_session_init(NULL, 0);
}

TEST_MAIN(test_policy_table, test_policy_pin_length_boundary,
          test_policy_unknown, test_session_init, test_session_seed_policy,
          test_session_seed_encrypted_by_air_warns,
          test_session_seed_bad_length,
          test_session_seed_via_terminal_needs_confirmation,
          test_session_confirmed_does_not_bypass_policy,
          test_session_seed_only_from_empty, test_session_pin_by_air_rejected,
          test_session_passphrase_paths,
          test_session_passphrase_rejected_can_retry,
          test_session_passphrase_wrong_state,
          test_session_no_review_in_early_states, test_session_review_channels,
          test_session_review_error_leaves_ready,
          test_session_no_sign_without_review,
          test_session_no_sign_without_confirm,
          test_session_new_psbt_resets_confirm,
          test_session_failed_review_also_resets_confirm,
          test_session_reviewing_same_psbt_again_unconfirms,
          test_session_sign_then_ready,
          test_session_sign_failure_returns_to_ready, test_session_cancel,
          test_session_wipe_zeroes, test_session_wipe_then_nothing_works,
          test_session_wiped_is_unusable_until_init,
          test_session_derive_failure_wipes_and_blocks,
          test_session_null_arguments)
