#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ds_bitcoin.h"
#include "ds_psbt.h"
#include "ds_wallet.h"
#include "psbt_vectors.h"
#include "test.h"

#define N_VECTORS (sizeof psbt_vectors / sizeof psbt_vectors[0])

/* A source over a byte array that records any request outside [0, len). */
typedef struct {
    const uint8_t *p;
    uint32_t len;
    int out_of_range;
    size_t largest;
} mem_src;

static int32_t mem_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    mem_src *m = ctx;
    if (off > m->len || n > m->len - off) {
        m->out_of_range = 1;
        return DS_ERR_ARG;
    }
    if (n > m->largest) {
        m->largest = n;
    }
    memcpy(buf, m->p + off, n);
    return (int32_t)n;
}

static int32_t analyze(const uint8_t *p, size_t len, const ds_wallet *w,
                       ds_psbt_review *rv, mem_src *m)
{
    ds_source src;
    memset(m, 0, sizeof *m);
    m->p = p;
    m->len = (uint32_t)len;
    src.read = mem_read;
    src.ctx = m;
    src.len = (uint32_t)len;
    return ds_psbt_analyze(&src, w, rv);
}

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

static void btc(uint64_t sat, char *out, size_t cap)
{
    snprintf(out, cap, "%llu.%08llu", (unsigned long long)(sat / 100000000u),
             (unsigned long long)(sat % 100000000u));
}

/* The summary the vector implies, built independently of the core. */
static void expected_summary(const psbt_vector *v, char *out, size_t cap)
{
    size_t k, o = 0;
    char amt[32];

    out[0] = '\0';
    for (k = 0; k < v->n_outputs; k++) {
        const psbt_vector_out *x = &v->outputs[k];
        btc(x->amount, amt, sizeof amt);
        if (x->address) {
            o += (size_t)snprintf(out + o, cap - o, "OUTPUT %u: %s BTC to %s",
                                  (unsigned)k, amt, x->address);
        } else {
            o += (size_t)snprintf(out + o, cap - o,
                                  "OUTPUT %u: %s BTC to script, %u bytes",
                                  (unsigned)k, amt,
                                  (unsigned)(strlen(x->script_hex) / 2));
        }
        o += (size_t)snprintf(out + o, cap - o, "%s\n",
                              x->kind == DS_OUT_CHANGE ? ", CHANGE"
                              : x->kind == DS_OUT_OWN  ? ", OWN"
                                                       : "");
    }
    btc(v->fee, amt, sizeof amt);
    o += (size_t)snprintf(out + o, cap - o, "FEE: %s BTC\n", amt);
    if (v->fee_warning) {
        o += (size_t)snprintf(out + o, cap - o, "WARNING: high fee\n");
    }
    for (k = 0; v->inputs_ours[k]; k++) {
        if (v->inputs_ours[k] == '0') {
            o += (size_t)snprintf(out + o, cap - o,
                                  "INPUT %u NOT SIGNED: not ours\n", (unsigned)k);
        }
    }
}

static void check_sign(const psbt_vector *v, const ds_wallet *w)
{
    ds_psbt_review rv;
    mem_src m;
    ds_sha256_ctx h;
    uint8_t digest[32];
    char text[2048], want[2048];
    size_t k, n_in = strlen(v->inputs_ours), ours = 0;
    int before = ds_failures;
    int32_t e = analyze(v->psbt, v->psbt_len, w, &rv, &m);

    CHECK_EQ_INT(e, DS_OK);
    CHECK_EQ_INT(m.out_of_range, 0);
    CHECK(m.largest <= 64);
    if (e != DS_OK) {
        printf("  in vector: %s\n", v->name);
        return;
    }
    CHECK_EQ_INT(rv.fee, v->fee);
    CHECK_EQ_INT(rv.fee_warning, v->fee_warning);
    CHECK_EQ_INT(rv.testnet, v->testnet);
    CHECK_EQ_INT(rv.n_out, v->n_outputs);
    CHECK_EQ_INT(rv.n_in, n_in);
    for (k = 0; k < v->n_outputs && k < rv.n_out; k++) {
        const ds_out *o = &rv.out[k];
        CHECK_EQ_INT(o->amount, v->outputs[k].amount);
        CHECK_EQ_INT(o->kind, v->outputs[k].kind);
        CHECK_EQ_INT(o->script_len, o->full_len);
        if (v->outputs[k].address) {
            char addr[100];
            CHECK(ds_address(o->script, o->script_len, rv.testnet, addr,
                             sizeof addr) > 0);
            CHECK_EQ_STR(addr, v->outputs[k].address);
        } else {
            CHECK_EQ_HEX(o->script, o->script_len, v->outputs[k].script_hex);
        }
    }
    for (k = 0; k < n_in && k < rv.n_in; k++) {
        CHECK_EQ_INT(rv.in[k].ours, v->inputs_ours[k] == '1');
        ours += v->inputs_ours[k] == '1';
    }
    CHECK_EQ_INT(rv.n_ours, ours);

    ds_sha256_init(&h);
    ds_sha256_update(&h, v->psbt, v->psbt_len);
    ds_sha256_final(&h, digest);
    CHECK(memcmp(digest, rv.psbt_sha256, 32) == 0);

    expected_summary(v, want, sizeof want);
    e = ds_psbt_summary_text(&rv, text, sizeof text);
    CHECK_EQ_INT(e, (int32_t)strlen(want));
    CHECK_EQ_STR(text, want);
    if (ds_failures != before) {
        printf("  in vector: %s\n", v->name);
    }
}

static void test_psbt_vectors(void)
{
    size_t k;
    for (k = 0; k < N_VECTORS; k++) {
        const psbt_vector *v = &psbt_vectors[k];
        ds_wallet w;
        if (!load(v, &w)) {
            continue;
        }
        if (v->sign) {
            check_sign(v, &w);
        } else {
            ds_psbt_review rv;
            mem_src m;
            int32_t e = analyze(v->psbt, v->psbt_len, &w, &rv, &m);
            if (e != v->error) {
                printf("vector \"%s\": got %d, want %d\n", v->name, (int)e,
                       (int)v->error);
                ds_failures++;
            }
            CHECK_EQ_INT(m.out_of_range, 0);
        }
        ds_wallet_wipe(&w);
    }
}

/* Every proper prefix of a valid PSBT is rejected without reading past it. */
static void test_psbt_truncated(void)
{
    size_t k;
    for (k = 0; k < N_VECTORS; k++) {
        const psbt_vector *v = &psbt_vectors[k];
        ds_wallet w;
        size_t n;
        if (!v->sign || !load(v, &w)) {
            continue;
        }
        for (n = 0; n < v->psbt_len; n++) {
            ds_psbt_review rv;
            mem_src m;
            int32_t e = analyze(v->psbt, n, &w, &rv, &m);
            if (e >= 0 || m.out_of_range) {
                printf("vector \"%s\", prefix %u: got %d, out of range %d\n",
                       v->name, (unsigned)n, (int)e, m.out_of_range);
                ds_failures++;
                break;
            }
        }
        ds_wallet_wipe(&w);
    }
}

static void test_psbt_trailing_byte(void)
{
    const psbt_vector *v = find("two inputs with non_witness_utxo");
    uint8_t buf[2048];
    ds_wallet w;
    ds_psbt_review rv;
    mem_src m;

    if (!v || !load(v, &w)) {
        return;
    }
    CHECK(v->psbt_len < sizeof buf);
    memcpy(buf, v->psbt, v->psbt_len);
    buf[v->psbt_len] = 0;
    CHECK_EQ_INT(analyze(buf, v->psbt_len + 1, &w, &rv, &m), DS_ERR_PSBT);
    buf[0] ^= 1; /* bad magic */
    CHECK_EQ_INT(analyze(buf, v->psbt_len, &w, &rv, &m), DS_ERR_PSBT);
    ds_wallet_wipe(&w);
}

/* Offset of the value of the nth witness_utxo pair holding a P2WPKH script
 * (key 01 01, value length 0x1f), or 0. */
static size_t find_witness_utxo(const uint8_t *p, size_t len, unsigned nth)
{
    size_t k;
    for (k = 5; k + 3 + 0x1f <= len; k++) {
        if (p[k] == 1 && p[k + 1] == 1 && p[k + 2] == 0x1f &&
            p[k + 11] == 0x16 && p[k + 12] == 0 && p[k + 13] == 0x14 &&
            nth-- == 0) {
            return k + 3;
        }
    }
    return 0;
}

static void put_le64(uint8_t *p, uint64_t v)
{
    unsigned k;
    for (k = 0; k < 8; k++) {
        p[k] = (uint8_t)(v >> (8 * k));
    }
}

/* One input, witness_utxo only: its amount sets the fee. The outputs are
 * 60000 sat external and 39000 sat change, so 10 % is a 6000 sat fee. */
static void test_psbt_fee_boundaries(void)
{
    const psbt_vector *v = find("one input with change");
    uint8_t buf[2048];
    ds_wallet w;
    ds_psbt_review rv;
    mem_src m;
    size_t at;

    if (!v || !load(v, &w)) {
        return;
    }
    memcpy(buf, v->psbt, v->psbt_len);
    at = find_witness_utxo(buf, v->psbt_len, 0);
    CHECK(at != 0 && find_witness_utxo(buf, v->psbt_len, 1) == 0);
    CHECK_EQ_HEX(buf + at, 8, "a086010000000000"); /* 100000 sat */

    put_le64(buf + at, 99000 + 6000);
    CHECK_EQ_INT(analyze(buf, v->psbt_len, &w, &rv, &m), DS_OK);
    CHECK_EQ_INT(rv.fee, 6000);
    CHECK_EQ_INT(rv.fee_warning, 0);
    put_le64(buf + at, 99000 + 6001);
    CHECK_EQ_INT(analyze(buf, v->psbt_len, &w, &rv, &m), DS_OK);
    CHECK_EQ_INT(rv.fee_warning, 1);
    put_le64(buf + at, 99000);
    CHECK_EQ_INT(analyze(buf, v->psbt_len, &w, &rv, &m), DS_OK);
    CHECK_EQ_INT(rv.fee, 0);
    CHECK_EQ_INT(rv.fee_warning, 0);
    put_le64(buf + at, 99000 - 1);
    CHECK_EQ_INT(analyze(buf, v->psbt_len, &w, &rv, &m), DS_ERR_AMOUNTS);
    put_le64(buf + at, 2100000000000001ull); /* above 21 000 000 BTC */
    CHECK_EQ_INT(analyze(buf, v->psbt_len, &w, &rv, &m), DS_ERR_AMOUNTS);
    ds_wallet_wipe(&w);
}

/* Two inputs: witness_utxo with the right amount but another script than the
 * non_witness_utxo output is rejected. */
static void test_psbt_witness_script_mismatch(void)
{
    const psbt_vector *v = find("two inputs with non_witness_utxo");
    uint8_t buf[2048];
    ds_wallet w;
    ds_psbt_review rv;
    mem_src m;
    unsigned nth;

    if (!v || !load(v, &w)) {
        return;
    }
    for (nth = 0; nth < 2; nth++) {
        size_t at;
        memcpy(buf, v->psbt, v->psbt_len);
        at = find_witness_utxo(buf, v->psbt_len, nth);
        CHECK(at != 0);
        if (at == 0) {
            break;
        }
        buf[at + 8 + 3 + 19] ^= 1; /* last byte of the key hash */
        CHECK_EQ_INT(analyze(buf, v->psbt_len, &w, &rv, &m), DS_ERR_AMOUNTS);
    }
    ds_wallet_wipe(&w);
}

/* A PSBT with only the global map: an unsigned transaction with n_in inputs
 * and n_out P2WPKH outputs. */
static size_t limits_psbt(uint8_t *p, unsigned n_in, unsigned n_out)
{
    size_t o = 0, tx_len = 4 + 1 + 41u * n_in + 1 + 31u * n_out + 4, k;

    memcpy(p, "psbt\xff", 5);
    o = 5;
    p[o++] = 1;
    p[o++] = 0; /* key: unsigned transaction */
    p[o++] = 0xfd;
    p[o++] = (uint8_t)tx_len;
    p[o++] = (uint8_t)(tx_len >> 8);
    memcpy(p + o, "\x02\x00\x00\x00", 4);
    o += 4;
    p[o++] = (uint8_t)n_in;
    for (k = 0; k < n_in; k++) {
        memset(p + o, (int)k + 1, 32);
        memset(p + o + 32, 0, 4);
        p[o + 36] = 0;
        memset(p + o + 37, 0xff, 4);
        o += 41;
    }
    p[o++] = (uint8_t)n_out;
    for (k = 0; k < n_out; k++) {
        memset(p + o, 0, 8);
        p[o] = 0x10;
        p[o + 8] = 22;
        p[o + 9] = 0;
        p[o + 10] = 20;
        memset(p + o + 11, 0x33, 20);
        o += 31;
    }
    memset(p + o, 0, 4);
    o += 4;
    p[o++] = 0; /* end of the global map; no input or output maps follow */
    return o;
}

static void test_psbt_limits(void)
{
    const psbt_vector *v = find("one input with change");
    uint8_t buf[3000];
    ds_wallet w;
    ds_psbt_review rv;
    mem_src m;
    size_t n;

    if (!v || !load(v, &w)) {
        return;
    }
    n = limits_psbt(buf, 33, 1);
    CHECK_EQ_INT(analyze(buf, n, &w, &rv, &m), DS_ERR_LIMIT);
    n = limits_psbt(buf, 1, 33);
    CHECK_EQ_INT(analyze(buf, n, &w, &rv, &m), DS_ERR_LIMIT);
    /* Within the limits the same PSBT only lacks its input and output maps. */
    n = limits_psbt(buf, 32, 32);
    CHECK_EQ_INT(analyze(buf, n, &w, &rv, &m), DS_ERR_PSBT);
    CHECK_EQ_INT(m.out_of_range, 0);
    ds_wallet_wipe(&w);
}

static void test_psbt_review_size(void)
{
    CHECK(sizeof(ds_psbt_review) <= 4096);
}

static void test_psbt_summary_space(void)
{
    const psbt_vector *v = find("one input with change");
    ds_wallet w;
    ds_psbt_review rv;
    mem_src m;
    char text[512];
    int32_t len;

    if (!v || !load(v, &w)) {
        return;
    }
    CHECK_EQ_INT(analyze(v->psbt, v->psbt_len, &w, &rv, &m), DS_OK);
    len = ds_psbt_summary_text(&rv, text, sizeof text);
    CHECK(len > 0);
    if (len > 0) {
        CHECK_EQ_INT(ds_psbt_summary_text(&rv, text, (size_t)len), DS_ERR_SPACE);
        CHECK_EQ_INT(ds_psbt_summary_text(&rv, text, (size_t)len + 1), len);
    }
    ds_wallet_wipe(&w);
}

TEST_MAIN(test_psbt_vectors, test_psbt_truncated, test_psbt_trailing_byte,
          test_psbt_fee_boundaries, test_psbt_witness_script_mismatch,
          test_psbt_limits, test_psbt_review_size, test_psbt_summary_space)
