#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ds_psbt.h"
#include "ds_sign.h"
#include "ds_wallet.h"
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

/* Takes up to cap bytes; past fail_at bytes (when not 0) fails with
 * DS_ERR_SPACE. */
typedef struct {
    uint8_t p[BUF];
    size_t len, calls, largest, fail_at;
} mem_sink;

static int32_t mem_write(void *ctx, const uint8_t *p, size_t n)
{
    mem_sink *m = ctx;
    m->calls++;
    if (n > m->largest) {
        m->largest = n;
    }
    if (n > sizeof m->p - m->len ||
        (m->fail_at != 0 && m->len + n > m->fail_at)) {
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

static int load(const psbt_vector *v, ds_wallet *w)
{
    int32_t e = ds_wallet_load(w, v->entropy, v->entropy_len, v->passphrase,
                               v->testnet);
    CHECK_EQ_INT(e, DS_OK);
    return e == DS_OK;
}

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

static size_t unhex(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = strlen(hex) / 2, k;
    if (n > cap) {
        return 0;
    }
    for (k = 0; k < n; k++) {
        unsigned b;
        sscanf(hex + 2 * k, "%2x", &b);
        out[k] = (uint8_t)b;
    }
    return n;
}

static int32_t analyze(const uint8_t *p, size_t len, const ds_wallet *w,
                       ds_psbt_review *rv)
{
    mem_src m;
    ds_source s = source(&m, p, len);
    return ds_psbt_analyze(&s, w, rv);
}

static int32_t sign(const uint8_t *p, size_t len, const ds_wallet *w,
                    const ds_psbt_review *rv, mem_sink *out)
{
    mem_src m;
    ds_source s = source(&m, p, len);
    ds_sink d = sink(out);
    return ds_psbt_sign(&s, w, rv, &d);
}

/* ---- an independent PSBT walker ---- */

typedef struct {
    size_t key, key_len, val, val_len, next;
} pair;

static size_t compact(const uint8_t *p, size_t len, size_t *off)
{
    size_t v = 0, n, k;
    if (*off >= len) {
        return (size_t)-1;
    }
    n = p[*off] < 0xfd ? 0 : p[*off] == 0xfd ? 2 : 4;
    if (n == 0) {
        return p[(*off)++];
    }
    for (k = 0; k < n; k++) {
        v |= (size_t)p[*off + 1 + k] << (8 * k);
    }
    *off += 1 + n;
    return v;
}

/* 1 and *e for a pair at off, 0 at a separator (e->next past it), -1 if
 * malformed. */
static int pair_at(const uint8_t *p, size_t len, size_t off, pair *e)
{
    size_t n = compact(p, len, &off);
    if (n == (size_t)-1) {
        return -1;
    }
    if (n == 0) {
        e->next = off;
        return 0;
    }
    e->key = off;
    e->key_len = n;
    off += n;
    if ((n = compact(p, len, &off)) == (size_t)-1 || n > len - off) {
        return -1;
    }
    e->val = off;
    e->val_len = n;
    e->next = off + n;
    return 1;
}

/* Whether e is a partial signature under the public key pub (33 bytes). */
static int is_sig_of(const uint8_t *p, const pair *e, const uint8_t *pub)
{
    return pub != NULL && e->key_len == 34 && p[e->key] == 0x02 &&
           memcmp(p + e->key + 1, pub, 33) == 0;
}

/* The next pair of a map that is not our signature, or the separator. */
static int next_other(const uint8_t *p, size_t len, size_t *off,
                      const uint8_t *pub, pair *e)
{
    int r;
    while ((r = pair_at(p, len, *off, e)) == 1 && is_sig_of(p, e, pub)) {
        *off = e->next;
    }
    return r;
}

/* Checks the signed PSBT against the one it came from (n_in inputs, n_out
 * outputs): each of our inputs (sigs[k].pubkey_hex) has exactly one
 * signature under our key, the expected one; every other pair is the
 * original's, in the original order. Our signatures in the original are
 * ignored. */
static void check_signed(const uint8_t *orig, size_t olen, const uint8_t *sgn,
                         size_t slen, size_t n_in, size_t n_out,
                         const psbt_vector_sig *sigs, const char *name)
{
    size_t a = 5, b = 5, m;
    int before = ds_failures;

    CHECK(slen >= 5 && memcmp(sgn, orig, 5) == 0);
    for (m = 0; m < 1 + n_in + n_out && ds_failures == before; m++) {
        uint8_t pub[33], sig[80];
        const uint8_t *our = NULL;
        size_t sig_len = 0, found = 0;
        pair x, y;
        int rx, ry;

        memset(&x, 0, sizeof x);
        memset(&y, 0, sizeof y);

        if (m >= 1 && m <= n_in && sigs[m - 1].pubkey_hex != NULL) {
            CHECK_EQ_INT(unhex(sigs[m - 1].pubkey_hex, pub, sizeof pub), 33);
            sig_len = unhex(sigs[m - 1].sig_hex, sig, sizeof sig);
            our = pub;
        }
        for (;;) {
            /* Our signatures in the signed PSBT: count and check them. */
            while ((ry = pair_at(sgn, slen, b, &y)) == 1 &&
                   is_sig_of(sgn, &y, our)) {
                found++;
                CHECK_EQ_INT(y.val_len, sig_len);
                CHECK(y.val_len == sig_len &&
                      memcmp(sgn + y.val, sig, sig_len) == 0);
                b = y.next;
            }
            rx = next_other(orig, olen, &a, our, &x);
            CHECK(rx >= 0 && ry >= 0);
            CHECK_EQ_INT(rx, ry);
            if (rx != 1 || ry != 1) {
                break;
            }
            CHECK(x.next - a == y.next - b &&
                  memcmp(orig + a, sgn + b, x.next - a) == 0);
            a = x.next;
            b = y.next;
        }
        CHECK_EQ_INT(found, our != NULL ? 1 : 0);
        a = x.next;
        b = y.next;
    }
    CHECK_EQ_INT(a, olen);
    CHECK_EQ_INT(b, slen);
    if (ds_failures != before) {
        printf("  in: %s, maps checked: %u\n", name, (unsigned)m);
    }
}

/* Copies the PSBT with a pair (key, val) inserted at the separator of map m
 * (0 global, then inputs and outputs). Returns the new length. */
static size_t insert_pair(const uint8_t *p, size_t len, size_t m,
                          const uint8_t *key, size_t klen, const uint8_t *val,
                          size_t vlen, uint8_t *out)
{
    size_t off = 5, k = 0, o;
    pair e;
    int r;

    for (;;) {
        r = pair_at(p, len, off, &e);
        CHECK(r >= 0);
        if (r < 0) {
            return 0;
        }
        if (r == 0 && k++ == m) {
            break;
        }
        off = e.next;
    }
    off = e.next - 1; /* the separator */
    memcpy(out, p, off);
    o = off;
    out[o++] = (uint8_t)klen; /* both under 0xfd */
    memcpy(out + o, key, klen);
    o += klen;
    out[o++] = (uint8_t)vlen;
    memcpy(out + o, val, vlen);
    o += vlen;
    memcpy(out + o, p + off, len - off);
    return o + len - off;
}

/* ---- tests ---- */

static void test_sign_vectors(void)
{
    size_t k;
    for (k = 0; k < N_VECTORS; k++) {
        const psbt_vector *v = &psbt_vectors[k];
        ds_wallet w;
        ds_psbt_review rv;
        static mem_sink out;
        int32_t e;

        if (!v->sign || !load(v, &w)) {
            continue;
        }
        CHECK_EQ_INT(analyze(v->psbt, v->psbt_len, &w, &rv), DS_OK);
        e = sign(v->psbt, v->psbt_len, &w, &rv, &out);
        CHECK_EQ_INT(e, DS_OK);
        CHECK(out.largest <= 64);
        if (e == DS_OK) {
            check_signed(v->psbt, v->psbt_len, out.p, out.len, rv.n_in,
                         rv.n_out, v->signatures, v->name);
        } else {
            printf("  in vector: %s\n", v->name);
        }
        ds_wallet_wipe(&w);
    }
}

/* Unknown keys and other keys' signatures are copied unchanged. */
static void test_sign_other_keys(void)
{
    static const uint8_t prop_key[] = {0xfc, 0x03, 'd', 's', 't', 0x01};
    static const uint8_t unknown_key[] = {0x1f, 0xaa};
    static const uint8_t val[] = {'v', 'a', 'l', 'u', 'e'};
    static const uint8_t other_sig[] = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02,
                                        0x01, 0x01, 0x01};
    const psbt_vector *v = find("foreign input is not signed");
    static uint8_t a[BUF], b[BUF];
    static mem_sink out;
    uint8_t other[34];
    ds_wallet w;
    ds_psbt_review rv;
    size_t n;

    if (!v || !load(v, &w)) {
        return;
    }
    other[0] = 0x02;
    other[1] = 0x02;
    memset(other + 2, 0x77, 32);
    /* Global map, both inputs (ours and the foreign one), the output. */
    n = insert_pair(v->psbt, v->psbt_len, 0, prop_key, sizeof prop_key, val,
                    sizeof val, a);
    n = insert_pair(a, n, 1, other, sizeof other, other_sig, sizeof other_sig,
                    b);
    n = insert_pair(b, n, 1, unknown_key, sizeof unknown_key, val, sizeof val,
                    a);
    n = insert_pair(a, n, 2, other, sizeof other, other_sig, sizeof other_sig,
                    b);
    n = insert_pair(b, n, 3, prop_key, sizeof prop_key, val, sizeof val, a);
    CHECK_EQ_INT(n, v->psbt_len + 2 * (2 + sizeof prop_key + sizeof val) +
                        2 * (2 + sizeof other + sizeof other_sig) +
                        (2 + sizeof unknown_key + sizeof val));
    CHECK_EQ_INT(analyze(a, n, &w, &rv), DS_OK);
    CHECK_EQ_INT(rv.n_ours, 1);
    CHECK_EQ_INT(sign(a, n, &w, &rv, &out), DS_OK);
    check_signed(a, n, out.p, out.len, rv.n_in, rv.n_out, v->signatures,
                 "other keys");
    ds_wallet_wipe(&w);
}

/* Signing a signed PSBT again replaces our signatures, wherever they are in
 * the map: one signature per input of ours, the same one. */
static void test_sign_replaces_ours(void)
{
    static const uint8_t unknown_key[] = {0x1f, 0xbb};
    static const uint8_t val[] = {0x01};
    static const uint8_t stale[] = {0x30, 0x06, 0x02, 0x01, 0x01, 0x02,
                                    0x01, 0x01, 0x01};
    const psbt_vector *v = find("two inputs with non_witness_utxo");
    static uint8_t a[BUF], b[BUF];
    static mem_sink first, second;
    uint8_t key[34];
    ds_wallet w;
    ds_psbt_review rv;
    size_t n, k;

    if (!v || !load(v, &w)) {
        return;
    }
    CHECK_EQ_INT(analyze(v->psbt, v->psbt_len, &w, &rv), DS_OK);
    CHECK_EQ_INT(rv.n_ours, 2);
    CHECK_EQ_INT(sign(v->psbt, v->psbt_len, &w, &rv, &first), DS_OK);
    CHECK_EQ_INT(analyze(first.p, first.len, &w, &rv), DS_OK);
    CHECK_EQ_INT(sign(first.p, first.len, &w, &rv, &second), DS_OK);
    CHECK_EQ_INT(second.len, first.len);
    CHECK(memcmp(second.p, first.p, first.len) == 0);
    check_signed(first.p, first.len, second.p, second.len, rv.n_in, rv.n_out,
                 v->signatures, "signed twice");

    /* A stale signature of ours, followed by another pair, in each input. */
    memcpy(a, v->psbt, v->psbt_len);
    n = v->psbt_len;
    for (k = 0; k < 2; k++) {
        key[0] = 0x02;
        CHECK_EQ_INT(unhex(v->signatures[k].pubkey_hex, key + 1, 33), 33);
        n = insert_pair(a, n, 1 + k, key, sizeof key, stale, sizeof stale, b);
        n = insert_pair(b, n, 1 + k, unknown_key, sizeof unknown_key, val,
                        sizeof val, a);
    }
    CHECK_EQ_INT(analyze(a, n, &w, &rv), DS_OK);
    CHECK_EQ_INT(sign(a, n, &w, &rv, &second), DS_OK);
    check_signed(a, n, second.p, second.len, rv.n_in, rv.n_out, v->signatures,
                 "stale signature");
    CHECK_EQ_INT(second.len, n - 2 * (2 + sizeof key + sizeof stale) +
                                 (first.len - v->psbt_len));
    ds_wallet_wipe(&w);
}

/* The source no longer holds the reviewed PSBT: nothing is written. */
static void test_sign_source_changed(void)
{
    const psbt_vector *v = find("one input with change");
    static uint8_t buf[BUF];
    static mem_sink out;
    ds_wallet w;
    ds_psbt_review rv;
    size_t k;

    if (!v || !load(v, &w)) {
        return;
    }
    memcpy(buf, v->psbt, v->psbt_len);
    /* The first output's amount (60000 sat) in the unsigned transaction. */
    for (k = 0; k + 8 <= v->psbt_len; k++) {
        if (memcmp(buf + k, "\x60\xea\x00\x00\x00\x00\x00\x00", 8) == 0) {
            break;
        }
    }
    CHECK(k + 8 <= v->psbt_len);
    buf[k + 1] = 0x50;
    CHECK_EQ_INT(analyze(buf, v->psbt_len, &w, &rv), DS_OK); /* still valid */
    CHECK_EQ_INT(analyze(v->psbt, v->psbt_len, &w, &rv), DS_OK);
    CHECK_EQ_INT(sign(buf, v->psbt_len, &w, &rv, &out), DS_ERR_PSBT);
    CHECK_EQ_INT(out.calls, 0);
    CHECK_EQ_INT(out.len, 0);
    /* Another length, the same prefix. */
    CHECK_EQ_INT(sign(v->psbt, v->psbt_len - 1, &w, &rv, &out), DS_ERR_PSBT);
    CHECK_EQ_INT(out.calls, 0);
    ds_wallet_wipe(&w);
}

/* A source whose content switches to another buffer once it has been read
 * to the end (after the first hash of the signing pass). */
typedef struct {
    const uint8_t *first, *then;
    uint32_t len;
    int switched;
} changing_src;

static int32_t changing_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    changing_src *m = ctx;
    if (off > m->len || n > m->len - off) {
        return DS_ERR_ARG;
    }
    memcpy(buf, (m->switched ? m->then : m->first) + off, n);
    if (off + n == m->len) {
        m->switched = 1;
    }
    return (int32_t)n;
}

/* The source changes while it is copied: the copy's hash catches it. */
static void test_sign_source_changes_while_copying(void)
{
    const psbt_vector *v = find("one input with change");
    static uint8_t buf[BUF];
    static mem_sink out;
    ds_wallet w;
    ds_psbt_review rv;
    changing_src c;
    ds_source src;
    ds_sink dst;

    if (!v || !load(v, &w)) {
        return;
    }
    memcpy(buf, v->psbt, v->psbt_len);
    buf[v->psbt_len - 2] ^= 1; /* inside the last output's derivation */
    CHECK_EQ_INT(analyze(v->psbt, v->psbt_len, &w, &rv), DS_OK);
    c.first = v->psbt;
    c.then = buf;
    c.len = (uint32_t)v->psbt_len;
    c.switched = 0;
    src.read = changing_read;
    src.ctx = &c;
    src.len = c.len;
    dst = sink(&out);
    CHECK_EQ_INT(ds_psbt_sign(&src, &w, &rv, &dst), DS_ERR_PSBT);
    CHECK_EQ_INT(c.switched, 1);
    ds_wallet_wipe(&w);
}

static void test_sign_sink_error(void)
{
    const psbt_vector *v = find("two inputs with non_witness_utxo");
    static mem_sink out;
    ds_wallet w;
    ds_psbt_review rv;
    mem_src m;
    ds_source src;
    ds_sink dst;
    size_t full, at;

    if (!v || !load(v, &w)) {
        return;
    }
    CHECK_EQ_INT(analyze(v->psbt, v->psbt_len, &w, &rv), DS_OK);
    CHECK_EQ_INT(sign(v->psbt, v->psbt_len, &w, &rv, &out), DS_OK);
    full = out.len;
    src = source(&m, v->psbt, v->psbt_len);
    for (at = 1; at < full; at += 37) {
        dst = sink(&out);
        out.fail_at = at;
        CHECK_EQ_INT(ds_psbt_sign(&src, &w, &rv, &dst), DS_ERR_SPACE);
        CHECK(out.len <= at);
    }
    dst = sink(&out);
    out.fail_at = full;
    CHECK_EQ_INT(ds_psbt_sign(&src, &w, &rv, &dst), DS_OK);
    CHECK_EQ_INT(out.len, full);
    ds_wallet_wipe(&w);
}

/* A review that does not match the PSBT's inputs is refused. */
static void test_sign_review_mismatch(void)
{
    const psbt_vector *v = find("one input with change");
    static mem_sink out;
    ds_wallet w;
    ds_psbt_review rv, bad;

    if (!v || !load(v, &w)) {
        return;
    }
    CHECK_EQ_INT(analyze(v->psbt, v->psbt_len, &w, &rv), DS_OK);
    bad = rv;
    bad.in[0].index ^= 1; /* another key: not the witness_utxo's script */
    CHECK_EQ_INT(sign(v->psbt, v->psbt_len, &w, &bad, &out), DS_ERR_PSBT);
    bad = rv;
    bad.in[0].amount += 1; /* not the witness_utxo's amount */
    CHECK_EQ_INT(sign(v->psbt, v->psbt_len, &w, &bad, &out), DS_ERR_PSBT);
    bad = rv;
    bad.in[0].chain = 2;
    CHECK_EQ_INT(sign(v->psbt, v->psbt_len, &w, &bad, &out), DS_ERR_ARG);
    CHECK_EQ_INT(out.calls, 0);
    bad = rv;
    bad.in[0].index = 0x80000000u;
    CHECK_EQ_INT(sign(v->psbt, v->psbt_len, &w, &bad, &out), DS_ERR_ARG);
    bad = rv;
    bad.n_in = DS_MAX_INPUTS + 1;
    CHECK_EQ_INT(sign(v->psbt, v->psbt_len, &w, &bad, &out), DS_ERR_ARG);
    bad = rv;
    bad.n_in = 2; /* the PSBT has one input map, then the outputs' */
    CHECK(sign(v->psbt, v->psbt_len, &w, &bad, &out) < 0);
    CHECK_EQ_INT(sign(v->psbt, v->psbt_len, &w, &rv, &out), DS_OK);
    ds_wallet_wipe(&w);
}

static void test_sign_args(void)
{
    const psbt_vector *v = find("one input with change");
    static mem_sink out;
    ds_wallet w;
    ds_psbt_review rv;
    mem_src m;
    ds_source src, nosrc;
    ds_sink dst, nodst;

    if (!v || !load(v, &w)) {
        return;
    }
    CHECK_EQ_INT(analyze(v->psbt, v->psbt_len, &w, &rv), DS_OK);
    src = source(&m, v->psbt, v->psbt_len);
    dst = sink(&out);
    nosrc = src;
    nosrc.read = NULL;
    nodst = dst;
    nodst.write = NULL;
    CHECK_EQ_INT(ds_psbt_sign(NULL, &w, &rv, &dst), DS_ERR_ARG);
    CHECK_EQ_INT(ds_psbt_sign(&nosrc, &w, &rv, &dst), DS_ERR_ARG);
    CHECK_EQ_INT(ds_psbt_sign(&src, NULL, &rv, &dst), DS_ERR_ARG);
    CHECK_EQ_INT(ds_psbt_sign(&src, &w, NULL, &dst), DS_ERR_ARG);
    CHECK_EQ_INT(ds_psbt_sign(&src, &w, &rv, NULL), DS_ERR_ARG);
    CHECK_EQ_INT(ds_psbt_sign(&src, &w, &rv, &nodst), DS_ERR_ARG);
    CHECK_EQ_INT(out.calls, 0);
    ds_wallet_wipe(&w);
}

TEST_MAIN(test_sign_vectors, test_sign_other_keys, test_sign_replaces_ours,
          test_sign_source_changed, test_sign_source_changes_while_copying,
          test_sign_sink_error, test_sign_review_mismatch, test_sign_args)
