#include <stdint.h>
#include <string.h>

#include "ds_bitcoin.h"
#include "ds_hash.h"
#include "ds_tx.h"
#include "test.h"

/* BIP 143, "Native P2WPKH": the unsigned transaction. */
static const char UNSIGNED_HEX[] =
    "0100000002fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f"
    "0000000000eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57"
    "b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f85"
    "c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50ce2"
    "f0167faa815988ac11000000";
/* The same transaction as signed in the BIP: segwit serialization. */
static const char SIGNED_HEX[] =
    "01000000000102fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad"
    "969f00000000494830450221008b9d1dc26ba6a9cb62127b02742fa9d754cd3bebf337f7a5"
    "5d114c8e5cdd30be022040529b194ba3f9281a99f2b1c0a19c0489bc22ede944ccf4ecbab4"
    "cc618ef3ed01eeffffffef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d"
    "57b90ec68a0100000000ffffffff02202cb206000000001976a9148280b37df378db99f66f"
    "85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42dbee7e4dbe6a21b2d50c"
    "e2f0167faa815988ac000247304402203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5"
    "447a12fb1366d7f01cc44a0220573a954c4518331561406f90300e8f3358f51928d43c212a"
    "8caed02de67eebee0121025476c2e83188368da1ff3e292e7acafcdb3566bb0ad253f62fc7"
    "0f07aeee635711000000";
/* Raw (internal order) txid of the signed transaction's non-witness
 * serialization, computed independently with Python hashlib. */
#define SIGNED_TXID \
    "09462d604a3102fae083ada369c795855a28db4bdd3d05358a361cf32a1a15e8"

static size_t unhex(const char *hex, uint8_t *out)
{
    size_t n = strlen(hex) / 2, i;
    for (i = 0; i < n; i++) {
        unsigned v = 0;
        int j;
        for (j = 0; j < 2; j++) {
            char c = hex[2 * i + j];
            v = v << 4 | (unsigned)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        out[i] = (uint8_t)v;
    }
    return n;
}

typedef struct {
    uint32_t n_in, n_out;
    uint8_t outpoint[DS_MAX_INPUTS][36];
    uint32_t sequence[DS_MAX_INPUTS];
    uint64_t amount[DS_MAX_OUTPUTS];
    uint8_t script[DS_MAX_OUTPUTS][DS_TX_SCRIPT_KEEP];
    size_t script_len[DS_MAX_OUTPUTS];
    int ends;
} rec;

static int32_t on_in(void *u, uint32_t i, const uint8_t op[36], uint32_t seq)
{
    rec *c = u;
    CHECK_EQ_INT(i, c->n_in);
    memcpy(c->outpoint[i], op, 36);
    c->sequence[i] = seq;
    c->n_in++;
    return DS_OK;
}

static int32_t on_out(void *u, uint32_t i, uint64_t amt, const uint8_t *s,
                      size_t len)
{
    rec *c = u;
    CHECK_EQ_INT(i, c->n_out);
    c->amount[i] = amt;
    c->script_len[i] = len;
    memcpy(c->script[i], s, len < DS_TX_SCRIPT_KEEP ? len : DS_TX_SCRIPT_KEEP);
    c->n_out++;
    return DS_OK;
}

static int32_t on_end(void *u, const ds_tx_result *res)
{
    rec *c = u;
    (void)res;
    c->ends++;
    return DS_OK;
}

static void setup(ds_tx_reader *r, rec *c)
{
    memset(c, 0, sizeof *c);
    ds_tx_reader_init(r);
    r->on_input = on_in;
    r->on_output = on_out;
    r->on_end = on_end;
    r->user = c;
}

static int32_t feed_chunks(ds_tx_reader *r, const uint8_t *tx, size_t n,
                           size_t chunk)
{
    size_t off = 0;
    while (off < n) {
        size_t k = n - off < chunk ? n - off : chunk;
        int32_t e = ds_tx_reader_feed(r, tx + off, k);
        if (e < 0) {
            return e;
        }
        off += k;
    }
    return DS_OK;
}

static void test_bip143_native_p2wpkh(void)
{
    static const size_t chunks[] = {1, 7, 1000};
    uint8_t tx[400], sig[32], pkh[20];
    size_t n = unhex(UNSIGNED_HEX, tx), k;

    /* hash160 of the second input's public key (BIP text). */
    {
        uint8_t pub[33];
        unhex("025476c2e83188368da1ff3e292e7acafcdb3566bb0ad253f62fc70f07aeee6357",
              pub);
        ds_hash160(pub, 33, pkh);
    }
    CHECK_EQ_HEX(pkh, 20, "1d0f172a0ecb48aee1be1f2687d2963ae33f71a1");

    for (k = 0; k < 3; k++) {
        ds_tx_reader r;
        rec c;
        const ds_tx_result *res;

        setup(&r, &c);
        CHECK_EQ_INT(feed_chunks(&r, tx, n, chunks[k]), DS_OK);
        CHECK_EQ_INT(ds_tx_reader_done(&r), 1);
        CHECK_EQ_INT(c.ends, 1);
        res = ds_tx_reader_result(&r);
        CHECK_EQ_INT(res->version, 1);
        CHECK_EQ_INT(res->locktime, 0x11);
        CHECK_EQ_INT(res->n_inputs, 2);
        CHECK_EQ_INT(res->n_outputs, 2);
        CHECK_EQ_INT(res->has_witness, 0);
        CHECK_EQ_HEX(res->hash_prevouts, 32,
                     "96b827c8483d4e9b96712b6713a7b68d6e8003a781feba36c31143470b4efd37");
        CHECK_EQ_HEX(res->hash_sequence, 32,
                     "52b0a642eea2fb7ae638c36f6252b6750293dbe574a806984b8e4d8548339a3b");
        CHECK_EQ_HEX(res->hash_outputs, 32,
                     "863ef3e1a92afbfdb97f31ad0fc7683ee943e9abcf2501590ff8f6551f47e5e5");

        CHECK_EQ_INT(c.n_in, 2);
        CHECK_EQ_INT(c.sequence[0], 0xffffffee);
        CHECK_EQ_INT(c.sequence[1], 0xffffffff);
        CHECK_EQ_HEX(c.outpoint[1], 36,
                     "ef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a01000000");
        CHECK_EQ_INT(c.n_out, 2);
        CHECK_EQ_INT(c.amount[0], 112340000);
        CHECK_EQ_INT(c.amount[1], 223450000);
        CHECK_EQ_INT(c.script_len[0], 25);
        CHECK_EQ_HEX(c.script[1], 25, "76a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac");

        ds_bip143_sighash(res->hash_prevouts, res->hash_sequence,
                          res->hash_outputs, res->version, c.outpoint[1], pkh,
                          600000000, c.sequence[1], res->locktime, sig);
        CHECK_EQ_HEX(sig, 32,
                     "c37af31116d1b27caf68aae9e3ac82f1477929014d5b917657d0eb49478cb670");
    }
}

static void test_txid_with_witness(void)
{
    static const size_t chunks[] = {1, 5, 1000};
    uint8_t tx[400];
    size_t n = unhex(SIGNED_HEX, tx), k;

    for (k = 0; k < 3; k++) {
        ds_tx_reader r;
        rec c;
        const ds_tx_result *res;

        setup(&r, &c);
        CHECK_EQ_INT(feed_chunks(&r, tx, n, chunks[k]), DS_OK);
        CHECK_EQ_INT(ds_tx_reader_done(&r), 1);
        res = ds_tx_reader_result(&r);
        CHECK_EQ_INT(res->has_witness, 1);
        CHECK_EQ_INT(res->locktime, 0x11);
        CHECK_EQ_HEX(res->txid, 32, SIGNED_TXID);
        /* The witness changes none of the BIP 143 hashes either. */
        CHECK_EQ_HEX(res->hash_prevouts, 32,
                     "96b827c8483d4e9b96712b6713a7b68d6e8003a781feba36c31143470b4efd37");
        CHECK_EQ_HEX(res->hash_outputs, 32,
                     "863ef3e1a92afbfdb97f31ad0fc7683ee943e9abcf2501590ff8f6551f47e5e5");
    }
}

static void test_tx_truncated(void)
{
    uint8_t tx[400];
    size_t n, cut, v;

    for (v = 0; v < 2; v++) {
        n = unhex(v ? SIGNED_HEX : UNSIGNED_HEX, tx);
        for (cut = 0; cut < n; cut++) {
            ds_tx_reader r;
            rec c;
            setup(&r, &c);
            CHECK_EQ_INT(ds_tx_reader_feed(&r, tx, cut), DS_OK);
            CHECK_EQ_INT(ds_tx_reader_done(&r), 0);
            CHECK_EQ_INT(c.ends, 0);
        }
    }
}

static void test_tx_errors(void)
{
    ds_tx_reader r;
    rec c;
    uint8_t tx[400];
    size_t n = unhex(UNSIGNED_HEX, tx);

    /* Bytes after the end of the transaction. */
    setup(&r, &c);
    CHECK_EQ_INT(ds_tx_reader_feed(&r, tx, n), DS_OK);
    CHECK_EQ_INT(ds_tx_reader_feed(&r, tx, 1), DS_ERR_PSBT);
    CHECK_EQ_INT(ds_tx_reader_done(&r), DS_ERR_PSBT);

    /* Marker 00 with a flag other than 01. */
    setup(&r, &c);
    unhex("010000000002", tx);
    CHECK_EQ_INT(ds_tx_reader_feed(&r, tx, 6), DS_ERR_PSBT);
    CHECK_EQ_INT(ds_tx_reader_feed(&r, tx, 1), DS_ERR_PSBT); /* sticky */

    /* Callbacks are optional. */
    setup(&r, &c);
    r.on_input = NULL;
    r.on_output = NULL;
    r.on_end = NULL;
    n = unhex(UNSIGNED_HEX, tx);
    CHECK_EQ_INT(ds_tx_reader_feed(&r, tx, n), DS_OK);
    CHECK_EQ_INT(ds_tx_reader_done(&r), 1);
}

static int32_t fail_cb(void *u, uint32_t i, const uint8_t op[36], uint32_t s)
{
    (void)u; (void)i; (void)op; (void)s;
    return DS_ERR_STATE;
}

static void test_tx_callback_error(void)
{
    ds_tx_reader r;
    rec c;
    uint8_t tx[400];
    size_t n = unhex(UNSIGNED_HEX, tx);

    setup(&r, &c);
    r.on_input = fail_cb;
    CHECK_EQ_INT(ds_tx_reader_feed(&r, tx, n), DS_ERR_STATE);
    CHECK_EQ_INT(ds_tx_reader_done(&r), DS_ERR_STATE);
}

static void test_tx_huge_counts(void)
{
    ds_tx_reader r;
    rec c;
    uint8_t b[128];
    size_t n;

    /* Input count 2^64-1. */
    setup(&r, &c);
    n = unhex("01000000ffffffffffffffffff", b);
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, n), DS_ERR_LIMIT);
    CHECK_EQ_INT(ds_tx_reader_done(&r), DS_ERR_LIMIT);

    /* One more than the limit. */
    setup(&r, &c);
    b[0] = 1; b[1] = b[2] = b[3] = 0; b[4] = DS_MAX_INPUTS + 1;
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, 5), DS_ERR_LIMIT);

    /* Exactly the limit is fine so far. */
    setup(&r, &c);
    b[4] = DS_MAX_INPUTS;
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, 5), DS_OK);

    /* One input with an empty script, then output count 2^64-1. */
    setup(&r, &c);
    n = unhex("0100000001"
              "0000000000000000000000000000000000000000000000000000000000000000"
              "00000000" "00" "ffffffff" "ffffffffffffffffff", b);
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, n), DS_ERR_LIMIT);

    /* Script length 2^32-1 in an output. */
    setup(&r, &c);
    n = unhex("0100000001"
              "0000000000000000000000000000000000000000000000000000000000000000"
              "00000000" "00" "ffffffff" "01" "0000000000000000" "feffffffff", b);
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, n), DS_ERR_PSBT);
}

static void test_tx_long_output_script(void)
{
    /* One input, one output with a 100-byte script of 0x51 (OP_1). */
    uint8_t tx[160], head[] = {1, 0, 0, 0, 1};
    size_t n = 0, k;
    ds_tx_reader r;
    rec c;
    uint8_t expect[32];

    memcpy(tx, head, 5); n = 5;
    memset(tx + n, 0, 36); n += 36;
    tx[n++] = 0;                      /* empty scriptSig */
    memset(tx + n, 0xff, 4); n += 4;  /* sequence */
    tx[n++] = 1;                      /* one output */
    memset(tx + n, 0, 8); tx[n] = 7; n += 8;
    tx[n++] = 100;
    memset(tx + n, 0x51, 100); n += 100;
    memset(tx + n, 0, 4); n += 4;

    for (k = 1; k <= 1000; k *= 7) {
        setup(&r, &c);
        CHECK_EQ_INT(feed_chunks(&r, tx, n, k), DS_OK);
        CHECK_EQ_INT(ds_tx_reader_done(&r), 1);
        CHECK_EQ_INT(c.script_len[0], 100);
        CHECK_EQ_INT(c.amount[0], 7);
        CHECK_EQ_INT(c.script[0][41], 0x51);
        /* The txid covers the whole script, not just the kept part. */
        ds_sha256d(tx, n, expect);
        CHECK(memcmp(ds_tx_reader_result(&r)->txid, expect, 32) == 0);
    }
}

/* A transaction with n_in inputs and n_out outputs, all empty scripts. */
static size_t counts_tx(uint8_t *b, unsigned n_in, unsigned n_out)
{
    size_t o = 0, k;
    memcpy(b, "\x02\x00\x00\x00", 4);
    o = 4;
    b[o++] = (uint8_t)n_in;
    for (k = 0; k < n_in; k++) {
        memset(b + o, (int)k, 36);
        b[o + 36] = 0;
        memset(b + o + 37, 0xff, 4);
        o += 41;
    }
    b[o++] = (uint8_t)n_out;
    for (k = 0; k < n_out; k++) {
        memset(b + o, 0, 9);
        o += 9;
    }
    memset(b + o, 0, 4);
    return o + 4;
}

/* The limits are per reader: init sets DS_MAX_*, the caller may change them. */
static void test_tx_reader_limits(void)
{
    uint8_t b[3000];
    ds_tx_reader r;
    size_t n;

    ds_tx_reader_init(&r);
    CHECK_EQ_INT(r.max_inputs, DS_MAX_INPUTS);
    CHECK_EQ_INT(r.max_outputs, DS_MAX_OUTPUTS);

    n = counts_tx(b, 1, DS_MAX_OUTPUTS + 8);
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, n), DS_ERR_LIMIT);
    ds_tx_reader_init(&r);
    r.max_outputs = 100000;
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, n), DS_OK);
    CHECK_EQ_INT(ds_tx_reader_done(&r), 1);
    CHECK_EQ_INT(ds_tx_reader_result(&r)->n_outputs, DS_MAX_OUTPUTS + 8);

    n = counts_tx(b, DS_MAX_INPUTS + 8, 1);
    ds_tx_reader_init(&r);
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, n), DS_ERR_LIMIT);
    ds_tx_reader_init(&r);
    r.max_inputs = 100000;
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, n), DS_OK);
    CHECK_EQ_INT(ds_tx_reader_done(&r), 1);
    CHECK_EQ_INT(ds_tx_reader_result(&r)->n_inputs, DS_MAX_INPUTS + 8);

    /* Lower limits apply too. */
    n = counts_tx(b, 2, 2);
    ds_tx_reader_init(&r);
    r.max_inputs = 1;
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, n), DS_ERR_LIMIT);
    ds_tx_reader_init(&r);
    r.max_outputs = 1;
    CHECK_EQ_INT(ds_tx_reader_feed(&r, b, n), DS_ERR_LIMIT);
}

TEST_MAIN(test_bip143_native_p2wpkh, test_txid_with_witness, test_tx_truncated,
          test_tx_errors, test_tx_callback_error, test_tx_huge_counts,
          test_tx_long_output_script, test_tx_reader_limits)
