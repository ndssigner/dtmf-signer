/* Incremental transaction reader and BIP 143 signature hash. */
#include "ds_tx.h"

#include <string.h>

enum {
    S_VERSION,
    S_INCNT0, /* first byte after the version: count, or segwit marker */
    S_FLAG,
    S_INCNT,
    S_IN_OUTPOINT,
    S_IN_SLEN,
    S_IN_SCRIPT,
    S_IN_SEQ,
    S_OUTCNT,
    S_OUT_AMOUNT,
    S_OUT_SLEN,
    S_OUT_SCRIPT,
    S_WIT_COUNT,
    S_WIT_LEN,
    S_WIT_ITEM,
    S_LOCKTIME,
    S_DONE,
    S_ERR
};

/* Which running hashes a byte belongs to. */
#define H_TXID 1u
#define H_PREV 2u
#define H_SEQ 4u
#define H_OUT 8u

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_le64(uint8_t *p, uint64_t v)
{
    put_le32(p, (uint32_t)v);
    put_le32(p + 4, (uint32_t)(v >> 32));
}

void ds_tx_reader_init(ds_tx_reader *r)
{
    memset(r, 0, sizeof *r);
    ds_sha256_init(&r->txid_h);
    ds_sha256_init(&r->prevouts_h);
    ds_sha256_init(&r->sequence_h);
    ds_sha256_init(&r->outputs_h);
    r->state = S_VERSION;
}

static void absorb(ds_tx_reader *r, uint8_t b, unsigned mask)
{
    if (mask & H_TXID) {
        ds_sha256_update(&r->txid_h, &b, 1);
    }
    if (mask & H_PREV) {
        ds_sha256_update(&r->prevouts_h, &b, 1);
    }
    if (mask & H_SEQ) {
        ds_sha256_update(&r->sequence_h, &b, 1);
    }
    if (mask & H_OUT) {
        ds_sha256_update(&r->outputs_h, &b, 1);
    }
}

/* Feeds one byte of a compact-size integer; 1 when the value is complete. */
static int cs_step(ds_tx_reader *r, uint8_t b, uint64_t *v)
{
    if (r->cs_got == 0) {
        r->cs_need = b < 0xfd ? 1 : b == 0xfd ? 3 : b == 0xfe ? 5 : 9;
    }
    r->cs[r->cs_got++] = b;
    if (r->cs_got < r->cs_need) {
        return 0;
    }
    r->cs_got = 0;
    (void)ds_compact_decode(r->cs, r->cs_need, v);
    return 1;
}

/* Double SHA-256 of what the context absorbed; wipes the context. */
static void dsha_final(ds_sha256_ctx *c, uint8_t out[32])
{
    uint8_t t[32];
    ds_sha256_final(c, t);
    ds_sha256_init(c);
    ds_sha256_update(c, t, 32);
    ds_sha256_final(c, out);
}

static int32_t finish(ds_tx_reader *r)
{
    dsha_final(&r->txid_h, r->res.txid);
    dsha_final(&r->prevouts_h, r->res.hash_prevouts);
    dsha_final(&r->sequence_h, r->res.hash_sequence);
    dsha_final(&r->outputs_h, r->res.hash_outputs);
    r->state = S_DONE;
    return r->on_end ? r->on_end(r->user, &r->res) : DS_OK;
}

static void after_outputs(ds_tx_reader *r)
{
    r->fix_got = 0;
    if (r->res.has_witness) {
        r->wit_index = 0;
        r->state = S_WIT_COUNT;
    } else {
        r->state = S_LOCKTIME;
    }
}

static void witness_input_done(ds_tx_reader *r)
{
    if (++r->wit_index == r->res.n_inputs) {
        r->fix_got = 0;
        r->state = S_LOCKTIME;
    } else {
        r->state = S_WIT_COUNT;
    }
}

static void witness_item_done(ds_tx_reader *r)
{
    if (--r->items_left == 0) {
        witness_input_done(r);
    } else {
        r->state = S_WIT_LEN;
    }
}

static int32_t output_done(ds_tx_reader *r)
{
    uint64_t amount = le64(r->fix);
    int32_t e = DS_OK;

    if (r->on_output) {
        e = r->on_output(r->user, r->index, amount, r->script, r->script_len);
    }
    if (++r->index == r->res.n_outputs) {
        after_outputs(r);
    } else {
        r->fix_got = 0;
        r->state = S_OUT_AMOUNT;
    }
    return e;
}

static int32_t step(ds_tx_reader *r, uint8_t b)
{
    uint64_t v;

    switch (r->state) {
    case S_VERSION:
        absorb(r, b, H_TXID);
        r->fix[r->fix_got++] = b;
        if (r->fix_got == 4) {
            r->res.version = le32(r->fix);
            r->fix_got = 0;
            r->state = S_INCNT0;
        }
        return DS_OK;
    case S_INCNT0:
        if (b == 0) { /* segwit marker: not part of the txid */
            r->res.has_witness = 1;
            r->state = S_FLAG;
            return DS_OK;
        }
        r->state = S_INCNT;
        return step(r, b);
    case S_FLAG:
        if (b != 1) {
            return DS_ERR_PSBT;
        }
        r->state = S_INCNT;
        return DS_OK;
    case S_INCNT:
        absorb(r, b, H_TXID);
        if (cs_step(r, b, &v)) {
            if (v > DS_MAX_INPUTS) {
                return DS_ERR_LIMIT;
            }
            if (v == 0) {
                return DS_ERR_PSBT;
            }
            r->res.n_inputs = (uint32_t)v;
            r->index = 0;
            r->fix_got = 0;
            r->state = S_IN_OUTPOINT;
        }
        return DS_OK;
    case S_IN_OUTPOINT:
        absorb(r, b, H_TXID | H_PREV);
        r->fix[r->fix_got++] = b;
        if (r->fix_got == 36) {
            r->state = S_IN_SLEN;
        }
        return DS_OK;
    case S_IN_SLEN:
        absorb(r, b, H_TXID);
        if (cs_step(r, b, &v)) {
            if (v > DS_TX_MAX_SCRIPT) {
                return DS_ERR_PSBT;
            }
            r->script_len = (uint32_t)v;
            r->script_got = 0;
            r->fix_got = 0;
            r->state = v ? S_IN_SCRIPT : S_IN_SEQ;
        }
        return DS_OK;
    case S_IN_SCRIPT:
        absorb(r, b, H_TXID);
        if (++r->script_got == r->script_len) {
            r->fix_got = 0;
            r->state = S_IN_SEQ;
        }
        return DS_OK;
    case S_IN_SEQ:
        absorb(r, b, H_TXID | H_SEQ);
        r->fix[36 + r->fix_got++] = b;
        if (r->fix_got == 4) {
            int32_t e = DS_OK;
            if (r->on_input) {
                e = r->on_input(r->user, r->index, r->fix, le32(r->fix + 36));
            }
            r->fix_got = 0;
            if (++r->index == r->res.n_inputs) {
                r->state = S_OUTCNT;
            } else {
                r->state = S_IN_OUTPOINT;
            }
            return e;
        }
        return DS_OK;
    case S_OUTCNT:
        absorb(r, b, H_TXID);
        if (cs_step(r, b, &v)) {
            if (v > DS_MAX_OUTPUTS) {
                return DS_ERR_LIMIT;
            }
            r->res.n_outputs = (uint32_t)v;
            r->index = 0;
            if (v == 0) {
                after_outputs(r);
            } else {
                r->fix_got = 0;
                r->state = S_OUT_AMOUNT;
            }
        }
        return DS_OK;
    case S_OUT_AMOUNT:
        absorb(r, b, H_TXID | H_OUT);
        r->fix[r->fix_got++] = b;
        if (r->fix_got == 8) {
            r->state = S_OUT_SLEN;
        }
        return DS_OK;
    case S_OUT_SLEN:
        absorb(r, b, H_TXID | H_OUT);
        if (cs_step(r, b, &v)) {
            if (v > DS_TX_MAX_SCRIPT) {
                return DS_ERR_PSBT;
            }
            r->script_len = (uint32_t)v;
            r->script_got = 0;
            if (v == 0) {
                return output_done(r);
            }
            r->state = S_OUT_SCRIPT;
        }
        return DS_OK;
    case S_OUT_SCRIPT:
        absorb(r, b, H_TXID | H_OUT);
        if (r->script_got < DS_TX_SCRIPT_KEEP) {
            r->script[r->script_got] = b;
        }
        if (++r->script_got == r->script_len) {
            return output_done(r);
        }
        return DS_OK;
    case S_WIT_COUNT:
        if (cs_step(r, b, &v)) {
            r->items_left = v;
            if (v == 0) {
                witness_input_done(r);
            } else {
                r->state = S_WIT_LEN;
            }
        }
        return DS_OK;
    case S_WIT_LEN:
        if (cs_step(r, b, &v)) {
            r->skip = v;
            if (v == 0) {
                witness_item_done(r);
            } else {
                r->state = S_WIT_ITEM;
            }
        }
        return DS_OK;
    case S_WIT_ITEM:
        if (--r->skip == 0) {
            witness_item_done(r);
        }
        return DS_OK;
    case S_LOCKTIME:
        absorb(r, b, H_TXID);
        r->fix[r->fix_got++] = b;
        if (r->fix_got == 4) {
            r->res.locktime = le32(r->fix);
            return finish(r);
        }
        return DS_OK;
    default: /* S_DONE: nothing may follow the transaction */
        return DS_ERR_PSBT;
    }
}

int32_t ds_tx_reader_feed(ds_tx_reader *r, const uint8_t *p, size_t n)
{
    size_t i;

    if (r->state == S_ERR) {
        return r->err;
    }
    for (i = 0; i < n; i++) {
        int32_t e = step(r, p[i]);
        if (e < 0) {
            r->state = S_ERR;
            r->err = e;
            return e;
        }
    }
    return DS_OK;
}

int32_t ds_tx_reader_done(const ds_tx_reader *r)
{
    if (r->state == S_ERR) {
        return r->err;
    }
    return r->state == S_DONE ? 1 : 0;
}

const ds_tx_result *ds_tx_reader_result(const ds_tx_reader *r)
{
    return &r->res;
}

void ds_bip143_sighash(const uint8_t hash_prevouts[32],
                       const uint8_t hash_sequence[32],
                       const uint8_t hash_outputs[32], uint32_t version,
                       const uint8_t outpoint[36], const uint8_t pubkey_hash[20],
                       uint64_t amount, uint32_t sequence, uint32_t locktime,
                       uint8_t out[32])
{
    uint8_t m[182];
    size_t o = 0;

    put_le32(m + o, version); o += 4;
    memcpy(m + o, hash_prevouts, 32); o += 32;
    memcpy(m + o, hash_sequence, 32); o += 32;
    memcpy(m + o, outpoint, 36); o += 36;
    /* scriptCode of P2WPKH: the P2PKH script, length-prefixed. */
    m[o++] = 0x19;
    m[o++] = 0x76;
    m[o++] = 0xa9;
    m[o++] = 0x14;
    memcpy(m + o, pubkey_hash, 20); o += 20;
    m[o++] = 0x88;
    m[o++] = 0xac;
    put_le64(m + o, amount); o += 8;
    put_le32(m + o, sequence); o += 4;
    memcpy(m + o, hash_outputs, 32); o += 32;
    put_le32(m + o, locktime); o += 4;
    put_le32(m + o, 1); o += 4; /* SIGHASH_ALL */
    ds_sha256d(m, o, out);
}
