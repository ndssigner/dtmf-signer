/* PSBT analysis (first pass) and review summary. */
#include "ds_psbt.h"

#include <string.h>

#include "ds_hash.h"
#include "ds_psbt_io.h"

#define HARD 0x80000000u
#define MAX_MONEY 2100000000000000ull /* 21 000 000 BTC in sat */
#define FEE_LIMIT 1000000u            /* sat: always a high fee above this */

/* PSBT key types we interpret. */
#define G_UNSIGNED_TX 0x00
#define G_VERSION 0xfb
#define I_NON_WITNESS_UTXO 0x00
#define I_WITNESS_UTXO 0x01
#define I_SIGHASH 0x03
#define I_BIP32 0x06
#define O_BIP32 0x02

/* ---- bounded reads from the source ---- */

int32_t ds_psbt_get(ds_psbt_cache *c, uint32_t off, uint8_t *out, size_t n)
{
    const ds_source *s = c->src;

    if (off > s->len || n > s->len - off) {
        return DS_ERR_PSBT;
    }
    while (n > 0) {
        size_t k;
        if (off < c->base || off - c->base >= c->fill) {
            uint32_t want = s->len - off < DS_PSBT_CACHE ? s->len - off
                                                         : DS_PSBT_CACHE;
            int32_t got = s->read(s->ctx, off, c->buf, want);
            if (got < 0) {
                c->fill = 0;
                return got;
            }
            if ((uint32_t)got != want) {
                c->fill = 0;
                return DS_ERR_PSBT;
            }
            c->base = off;
            c->fill = want;
        }
        k = c->fill - (off - c->base);
        if (k > n) {
            k = n;
        }
        memcpy(out, c->buf + (off - c->base), k);
        out += k;
        off += (uint32_t)k;
        n -= k;
    }
    return DS_OK;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

int32_t ds_psbt_get_compact(ds_psbt_cache *c, uint32_t *off, uint64_t *v)
{
    uint8_t b[9];
    size_t n;
    int32_t e = ds_psbt_get(c, *off, b, 1);

    if (e < 0) {
        return e;
    }
    n = b[0] < 0xfd ? 1 : b[0] == 0xfd ? 3 : b[0] == 0xfe ? 5 : 9;
    if (n > 1 && (e = ds_psbt_get(c, *off + 1, b + 1, n - 1)) < 0) {
        return e;
    }
    (void)ds_compact_decode(b, n, v);
    *off += (uint32_t)n;
    return DS_OK;
}

/* Whether [a, a + n) and [b, b + n) hold the same bytes. */
static int32_t same_bytes(ds_psbt_cache *c, uint32_t a, uint32_t b, uint32_t n)
{
    uint8_t x[32], y[32];

    while (n > 0) {
        uint32_t k = n < 32 ? n : 32;
        int32_t e = ds_psbt_get(c, a, x, k);
        if (e < 0 || (e = ds_psbt_get(c, b, y, k)) < 0) {
            return e;
        }
        if (memcmp(x, y, k) != 0) {
            return 0;
        }
        a += k;
        b += k;
        n -= k;
    }
    return 1;
}

/* ---- key-value maps ---- */

int32_t ds_psbt_read_entry(ds_psbt_cache *c, uint32_t off, ds_psbt_entry *e)
{
    uint64_t v;
    uint32_t len = c->src->len;
    int32_t r = ds_psbt_get_compact(c, &off, &v);

    if (r < 0) {
        return r;
    }
    if (v == 0) {
        e->next = off;
        return 0;
    }
    if (v > len - off) {
        return DS_ERR_PSBT;
    }
    e->key = off;
    e->key_len = (uint32_t)v;
    if ((r = ds_psbt_get(c, off, &e->type, 1)) < 0) {
        return r;
    }
    off += e->key_len;
    if ((r = ds_psbt_get_compact(c, &off, &v)) < 0) {
        return r;
    }
    if (v > len - off) {
        return DS_ERR_PSBT;
    }
    e->val = off;
    e->val_len = (uint32_t)v;
    e->next = off + e->val_len;
    return 1;
}

/* Checks the map at start (count, no repeated key) and sets *end just past
 * its separator. */
static int32_t scan_map(ds_psbt_cache *c, uint32_t start, uint32_t *end)
{
    ds_psbt_entry a, b;
    uint32_t off = start, count = 0;
    int32_t r;

    while ((r = ds_psbt_read_entry(c, off, &a)) == 1) {
        if (++count > DS_PSBT_MAX_KEYS) {
            return DS_ERR_LIMIT;
        }
        off = a.next;
    }
    if (r < 0) {
        return r;
    }
    *end = a.next;

    for (off = start; ds_psbt_read_entry(c, off, &a) == 1; off = a.next) {
        uint32_t o2;
        for (o2 = a.next; ds_psbt_read_entry(c, o2, &b) == 1; o2 = b.next) {
            if (a.key_len == b.key_len) {
                r = same_bytes(c, a.key, b.key, a.key_len);
                if (r < 0) {
                    return r;
                }
                if (r) {
                    return DS_ERR_PSBT;
                }
            }
        }
    }
    return DS_OK;
}

/* ---- wallet derivations ---- */

#define NOT_OURS 0
#define OUR_PATH 1 /* our fingerprint and the account path + /chain/index */

/* Classifies a BIP 32 derivation value (fingerprint and path). */
static int32_t classify(ds_psbt_cache *c, const ds_psbt_entry *e,
                        const ds_wallet *w, uint32_t *chain, uint32_t *index)
{
    uint8_t v[4 + 4 * 5];
    uint32_t n, fp, k, p[5];
    int32_t r;

    if (e->val_len < 4 || e->val_len % 4 != 0) {
        return DS_ERR_PSBT;
    }
    n = e->val_len / 4 - 1;
    if ((r = ds_psbt_get(c, e->val, v, n < 5 ? e->val_len : sizeof v)) < 0) {
        return r;
    }
    fp = (uint32_t)v[0] << 24 | (uint32_t)v[1] << 16 | (uint32_t)v[2] << 8 |
         (uint32_t)v[3];
    if (fp != w->wallet_fp) {
        return NOT_OURS;
    }
    for (k = 0; k < n && k < 5; k++) {
        p[k] = le32(v + 4 + 4 * k);
    }
    if (n >= 2 && p[1] != w->path[1]) {
        return DS_ERR_NETWORK; /* our fingerprint, another coin type */
    }
    if (n != 5 || p[0] != w->path[0] || p[2] != w->path[2] || p[3] > 1 ||
        p[4] >= HARD) {
        return NOT_OURS;
    }
    *chain = p[3];
    *index = p[4];
    return OUR_PATH;
}

/* The P2WPKH script of account/chain/index. */
static int32_t our_script(const ds_wallet *w, uint32_t chain, uint32_t index,
                          uint8_t script[22])
{
    ds_hdkey k;
    int32_t e;

    ds_wipe(&k, sizeof k);
    e = ds_bip32_child(&w->account, chain, &k);
    if (e == DS_OK) {
        e = ds_bip32_child(&k, index, &k);
    }
    if (e == DS_OK) {
        script[0] = 0x00;
        script[1] = 0x14;
        ds_hash160(k.pub, sizeof k.pub, script + 2);
    }
    ds_hdkey_wipe(&k);
    return e;
}

/* ---- the unsigned transaction and previous transactions ---- */

static int32_t on_unsigned_input(void *user, uint32_t i, const uint8_t op[36],
                                 uint32_t sequence)
{
    ds_psbt_review *rv = user;
    memcpy(rv->in[i].outpoint, op, 36);
    rv->in[i].sequence = sequence;
    return DS_OK;
}

static int32_t on_unsigned_output(void *user, uint32_t i, uint64_t amount,
                                  const uint8_t *script, size_t len)
{
    ds_psbt_review *rv = user;
    ds_out *o = &rv->out[i];

    if (amount > MAX_MONEY) {
        return DS_ERR_AMOUNTS;
    }
    o->amount = amount;
    o->full_len = (uint16_t)len; /* len <= DS_TX_MAX_SCRIPT */
    o->script_len = (uint8_t)(len < DS_TX_SCRIPT_KEEP ? len : DS_TX_SCRIPT_KEEP);
    memcpy(o->script, script, o->script_len);
    o->kind = DS_OUT_EXTERNAL;
    return DS_OK;
}

/* Streams [off, off + len) through a transaction reader, one byte per feed so
 * that callbacks know the position (*pos) of the byte that triggered them. */
static int32_t read_tx(ds_psbt_cache *c, uint32_t off, uint32_t len,
                       ds_tx_reader *r, uint32_t *pos)
{
    uint8_t b[DS_PSBT_CACHE];
    uint32_t end = off + len;
    int32_t e;

    while (off < end) {
        uint32_t k;
        uint32_t n = end - off < DS_PSBT_CACHE ? end - off : DS_PSBT_CACHE;
        if ((e = ds_psbt_get(c, off, b, n)) < 0) {
            return e;
        }
        for (k = 0; k < n; k++) {
            *pos = off + k;
            if ((e = ds_tx_reader_feed(r, b + k, 1)) < 0) {
                return e;
            }
        }
        off += n;
    }
    e = ds_tx_reader_done(r);
    return e < 0 ? e : e == 1 ? DS_OK : DS_ERR_PSBT;
}

static uint32_t compact_size(uint64_t v)
{
    return v < 0xfd ? 1 : v <= 0xffff ? 3 : v <= 0xffffffffu ? 5 : 9;
}

static int32_t parse_unsigned_tx(ds_psbt_cache *c, const ds_psbt_entry *e,
                                 ds_psbt_review *rv)
{
    ds_tx_reader r;
    const ds_tx_result *res;
    uint32_t pos, k, expect;
    int32_t err;

    ds_tx_reader_init(&r);
    r.on_input = on_unsigned_input;
    r.on_output = on_unsigned_output;
    r.user = rv;
    if ((err = read_tx(c, e->val, e->val_len, &r, &pos)) < 0) {
        return err;
    }
    res = ds_tx_reader_result(&r);
    if (res->has_witness) {
        return DS_ERR_PSBT;
    }
    /* Its length with empty scriptSigs: anything else had a scriptSig. */
    expect = 4 + compact_size(res->n_inputs) + 41 * res->n_inputs +
             compact_size(res->n_outputs) + 4;
    for (k = 0; k < res->n_outputs; k++) {
        expect += 8 + compact_size(rv->out[k].full_len) + rv->out[k].full_len;
    }
    if (expect != e->val_len) {
        return DS_ERR_PSBT;
    }
    rv->n_in = res->n_inputs;
    rv->n_out = res->n_outputs;
    rv->version = res->version;
    rv->locktime = res->locktime;
    memcpy(rv->hash_prevouts, res->hash_prevouts, 32);
    memcpy(rv->hash_sequence, res->hash_sequence, 32);
    memcpy(rv->hash_outputs, res->hash_outputs, 32);
    return DS_OK;
}

typedef struct {
    uint32_t want;       /* output index wanted */
    const uint32_t *pos; /* position of the byte being fed */
    int found;
    uint64_t amount;
    uint32_t script, script_len; /* the script's place in the source */
    uint8_t txid[32];
} prev_out;

static int32_t on_prev_output(void *user, uint32_t i, uint64_t amount,
                              const uint8_t *script, size_t len)
{
    prev_out *p = user;
    (void)script;
    if (i == p->want) {
        p->found = 1;
        p->amount = amount;
        p->script_len = (uint32_t)len;
        p->script = *p->pos + 1 - (uint32_t)len; /* fed its last byte */
    }
    return DS_OK;
}

static int32_t on_prev_end(void *user, const ds_tx_result *res)
{
    prev_out *p = user;
    memcpy(p->txid, res->txid, 32);
    return DS_OK;
}

/* Reads a non_witness_utxo: its txid must be the outpoint's, and it must have
 * the output the outpoint names. */
static int32_t check_prev_tx(ds_psbt_cache *c, const ds_psbt_entry *e,
                             const uint8_t op[36], prev_out *p)
{
    ds_tx_reader r;
    uint32_t pos = 0;
    int32_t err;

    memset(p, 0, sizeof *p);
    p->want = le32(op + 32);
    p->pos = &pos;
    ds_tx_reader_init(&r);
    r.on_output = on_prev_output;
    r.on_end = on_prev_end;
    r.user = p;
    /* Only streamed: nothing is stored per entry, so a large cap suffices. */
    r.max_inputs = DS_PSBT_PREV_MAX;
    r.max_outputs = DS_PSBT_PREV_MAX;
    if ((err = read_tx(c, e->val, e->val_len, &r, &pos)) < 0) {
        return err;
    }
    if (memcmp(p->txid, op, 32) != 0 || !p->found) {
        return DS_ERR_AMOUNTS;
    }
    return DS_OK;
}

/* ---- input and output maps ---- */

static int32_t analyze_input(ds_psbt_cache *c, uint32_t start,
                             const ds_wallet *w, ds_psbt_review *rv,
                             uint32_t i)
{
    ds_psbt_input *in = &rv->in[i];
    ds_psbt_entry e, nonwit, wit, sighash;
    int has_nonwit = 0, has_wit = 0, has_sighash = 0;
    uint32_t off, wit_script = 0, wit_script_len = 0;
    uint64_t wit_amount = 0, v;
    uint8_t b[22];
    int32_t r;

    memset(&nonwit, 0, sizeof nonwit);
    memset(&wit, 0, sizeof wit);
    memset(&sighash, 0, sizeof sighash);
    for (off = start; (r = ds_psbt_read_entry(c, off, &e)) == 1; off = e.next) {
        ds_psbt_entry *keep = e.type == I_NON_WITNESS_UTXO ? &nonwit
                              : e.type == I_WITNESS_UTXO   ? &wit
                              : e.type == I_SIGHASH        ? &sighash
                                                           : NULL;
        if (keep == NULL) {
            continue;
        }
        if (e.key_len != 1) {
            return DS_ERR_PSBT;
        }
        *keep = e;
        has_nonwit |= keep == &nonwit;
        has_wit |= keep == &wit;
        has_sighash |= keep == &sighash;
    }
    if (r < 0) {
        return r;
    }

    if (has_sighash) {
        if (sighash.val_len != 4) {
            return DS_ERR_PSBT;
        }
        if ((r = ds_psbt_get(c, sighash.val, b, 4)) < 0) {
            return r;
        }
        if (le32(b) != 1) {
            return DS_ERR_SIGHASH;
        }
    }

    if (has_wit) { /* amount, then the length-prefixed script */
        off = wit.val + 8;
        if (wit.val_len < 9 || (r = ds_psbt_get(c, wit.val, b, 8)) < 0) {
            return wit.val_len < 9 ? DS_ERR_PSBT : r;
        }
        wit_amount = le64(b);
        if ((r = ds_psbt_get_compact(c, &off, &v)) < 0) {
            return r;
        }
        if (off > wit.val + wit.val_len ||
            v != (uint64_t)(wit.val + wit.val_len - off)) {
            return DS_ERR_PSBT;
        }
        wit_script = off;
        wit_script_len = (uint32_t)v;
        in->amount = wit_amount;
    }

    if (has_nonwit) {
        prev_out p;
        if ((r = check_prev_tx(c, &nonwit, in->outpoint, &p)) < 0) {
            return r;
        }
        if (has_wit) {
            if (p.amount != wit_amount || p.script_len != wit_script_len) {
                return DS_ERR_AMOUNTS;
            }
            r = same_bytes(c, p.script, wit_script, wit_script_len);
            if (r <= 0) {
                return r < 0 ? r : DS_ERR_AMOUNTS;
            }
        }
        in->amount = p.amount;
    } else if (rv->n_in >= 2 || !has_wit) {
        return DS_ERR_AMOUNTS;
    }
    if (in->amount > MAX_MONEY) {
        return DS_ERR_AMOUNTS;
    }
    rv->total_in += in->amount;

    /* Ours: witness_utxo is P2WPKH of a key we derive for a declared path. */
    if (has_wit && wit_script_len == 22) {
        if ((r = ds_psbt_get(c, wit_script, b, 22)) < 0) {
            return r;
        }
    } else {
        b[0] = 0xff; /* matches no P2WPKH script */
    }
    for (off = start; ds_psbt_read_entry(c, off, &e) == 1; off = e.next) {
        uint32_t chain, index;
        uint8_t mine[22];
        if (e.type != I_BIP32) {
            continue;
        }
        if (e.key_len != 34 && e.key_len != 66) {
            return DS_ERR_PSBT;
        }
        if ((r = classify(c, &e, w, &chain, &index)) < 0) {
            return r;
        }
        if (r != OUR_PATH || in->ours || b[0] != 0x00 || b[1] != 0x14) {
            continue;
        }
        if ((r = our_script(w, chain, index, mine)) < 0) {
            return r;
        }
        if (memcmp(mine, b, 22) == 0) {
            in->ours = 1;
            in->chain = (uint8_t)chain;
            in->index = index;
            rv->n_ours++;
        }
    }
    return DS_OK;
}

static int32_t analyze_output(ds_psbt_cache *c, uint32_t start,
                              const ds_wallet *w, ds_out *o)
{
    ds_psbt_entry e;
    uint32_t off;
    int32_t r;

    for (off = start; (r = ds_psbt_read_entry(c, off, &e)) == 1; off = e.next) {
        uint32_t chain, index;
        uint8_t mine[22];
        if (e.type != O_BIP32) {
            continue;
        }
        if (e.key_len != 34 && e.key_len != 66) {
            return DS_ERR_PSBT;
        }
        if ((r = classify(c, &e, w, &chain, &index)) < 0) {
            return r;
        }
        if (r != OUR_PATH) {
            continue;
        }
        if ((r = our_script(w, chain, index, mine)) < 0) {
            return r;
        }
        if (o->full_len != 22 || memcmp(o->script, mine, 22) != 0) {
            return DS_ERR_CHANGE; /* our path, somebody else's script */
        }
        o->kind = chain == 1 ? DS_OUT_CHANGE : DS_OUT_OWN;
    }
    return r < 0 ? r : DS_OK;
}

/* ---- the whole PSBT ---- */

int32_t ds_psbt_hash_source(ds_psbt_cache *c, uint8_t out[32])
{
    ds_sha256_ctx h;
    uint8_t b[DS_PSBT_CACHE];
    uint32_t off = 0, len = c->src->len;
    int32_t e = DS_OK;

    ds_sha256_init(&h);
    while (off < len) {
        uint32_t n = len - off < DS_PSBT_CACHE ? len - off : DS_PSBT_CACHE;
        if ((e = ds_psbt_get(c, off, b, n)) < 0) {
            break;
        }
        ds_sha256_update(&h, b, n);
        off += n;
    }
    ds_sha256_final(&h, out);
    return e;
}

static int32_t analyze(ds_psbt_cache *c, const ds_wallet *w, ds_psbt_review *rv)
{
    static const uint8_t magic[5] = {0x70, 0x73, 0x62, 0x74, 0xff};
    uint8_t b[5];
    ds_psbt_entry e, tx;
    int has_tx = 0;
    uint32_t off, end, k;
    uint64_t total_out = 0, external = 0;
    uint8_t again[32];
    int32_t r;

    memset(&tx, 0, sizeof tx);
    if ((r = ds_psbt_hash_source(c, rv->psbt_sha256)) < 0) {
        return r;
    }
    if ((r = ds_psbt_get(c, 0, b, 5)) < 0) {
        return r;
    }
    if (memcmp(b, magic, 5) != 0) {
        return DS_ERR_PSBT;
    }

    /* Global map. */
    if ((r = scan_map(c, 5, &end)) < 0) {
        return r;
    }
    for (off = 5; ds_psbt_read_entry(c, off, &e) == 1; off = e.next) {
        if (e.type == G_VERSION) {
            if (e.key_len != 1 || e.val_len != 4) {
                return DS_ERR_PSBT;
            }
            if ((r = ds_psbt_get(c, e.val, b, 4)) < 0) {
                return r;
            }
            if (le32(b) != 0) {
                return DS_ERR_UNSUPPORTED;
            }
        } else if (e.type == G_UNSIGNED_TX) {
            if (e.key_len != 1) {
                return DS_ERR_PSBT;
            }
            tx = e;
            has_tx = 1;
        }
    }
    if (!has_tx) {
        return DS_ERR_PSBT;
    }
    if ((r = parse_unsigned_tx(c, &tx, rv)) < 0) {
        return r;
    }

    for (k = 0; k < rv->n_in; k++) {
        off = end;
        if ((r = scan_map(c, off, &end)) < 0 ||
            (r = analyze_input(c, off, w, rv, k)) < 0) {
            return r;
        }
    }
    for (k = 0; k < rv->n_out; k++) {
        off = end;
        if ((r = scan_map(c, off, &end)) < 0 ||
            (r = analyze_output(c, off, w, &rv->out[k])) < 0) {
            return r;
        }
        total_out += rv->out[k].amount;
        if (rv->out[k].kind == DS_OUT_EXTERNAL) {
            external += rv->out[k].amount;
        }
    }
    if (end != c->src->len) {
        return DS_ERR_PSBT; /* bytes after the last map */
    }

    /* Amounts are at most MAX_MONEY each: these sums cannot overflow. */
    if (total_out > rv->total_in) {
        return DS_ERR_AMOUNTS;
    }
    rv->fee = rv->total_in - total_out;
    rv->fee_warning = rv->fee > FEE_LIMIT || (external > 0 && rv->fee * 10 > external);
    rv->testnet = w->testnet;

    /* The bytes analyzed must be the bytes hashed: hash the source again,
     * bypassing the cache, and compare. */
    c->fill = 0;
    if ((r = ds_psbt_hash_source(c, again)) < 0) {
        return r;
    }
    if (memcmp(again, rv->psbt_sha256, 32) != 0) {
        return DS_ERR_PSBT; /* the source changed during the analysis */
    }
    return DS_OK;
}

int32_t ds_psbt_analyze(const ds_source *src, const ds_wallet *w,
                        ds_psbt_review *out)
{
    ds_psbt_cache c;
    int32_t r;

    if (out == NULL) {
        return DS_ERR_ARG;
    }
    memset(out, 0, sizeof *out);
    if (src == NULL || src->read == NULL || w == NULL) {
        return DS_ERR_ARG;
    }
    memset(&c, 0, sizeof c);
    c.src = src;
    r = analyze(&c, w, out);
    if (r < 0) {
        memset(out, 0, sizeof *out);
    }
    return r;
}

/* ---- summary ---- */

typedef struct {
    char *p;
    size_t cap, len;
    int full;
} text;

static void put(text *t, const char *s)
{
    size_t n = strlen(s);
    if (t->full || n >= t->cap - t->len) {
        t->full = 1;
        return;
    }
    memcpy(t->p + t->len, s, n + 1);
    t->len += n;
}

static void put_uint(text *t, uint64_t v, unsigned min_digits)
{
    char d[24];
    size_t n = sizeof d - 1;

    d[n] = '\0';
    do {
        d[--n] = (char)('0' + v % 10);
        v /= 10;
    } while (v > 0 || sizeof d - 1 - n < min_digits);
    put(t, d + n);
}

static void put_btc(text *t, uint64_t sat)
{
    put_uint(t, sat / 100000000u, 1);
    put(t, ".");
    put_uint(t, sat % 100000000u, 8);
    put(t, " BTC");
}

int32_t ds_psbt_summary_text(const ds_psbt_review *r, char *out, size_t cap)
{
    text t;
    uint32_t k;

    if (r == NULL || out == NULL || cap == 0 || r->n_out > DS_MAX_OUTPUTS ||
        r->n_in > DS_MAX_INPUTS) {
        return DS_ERR_ARG;
    }
    t.p = out;
    t.cap = cap;
    t.len = 0;
    t.full = 0;
    out[0] = '\0';
    for (k = 0; k < r->n_out; k++) {
        const ds_out *o = &r->out[k];
        char addr[96];
        put(&t, "OUTPUT ");
        put_uint(&t, k + 1, 1);
        put(&t, ": ");
        put_btc(&t, o->amount);
        put(&t, " to ");
        if (o->full_len <= DS_TX_SCRIPT_KEEP &&
            ds_address(o->script, o->full_len, r->testnet, addr, sizeof addr) > 0) {
            put(&t, addr);
        } else {
            put(&t, "script, ");
            put_uint(&t, o->full_len, 1);
            put(&t, " bytes");
        }
        put(&t, o->kind == DS_OUT_CHANGE ? ", CHANGE\n"
                : o->kind == DS_OUT_OWN  ? ", OWN\n"
                                         : "\n");
    }
    put(&t, "FEE: ");
    put_btc(&t, r->fee);
    put(&t, "\n");
    if (r->fee_warning) {
        put(&t, "WARNING: high fee\n");
    }
    for (k = 0; k < r->n_in; k++) {
        if (!r->in[k].ours) {
            put(&t, "INPUT ");
            put_uint(&t, k + 1, 1);
            put(&t, " NOT SIGNED: not ours\n");
        }
    }
    if (t.full) {
        out[0] = '\0';
        return DS_ERR_SPACE;
    }
    return (int32_t)t.len;
}
