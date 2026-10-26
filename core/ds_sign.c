/* PSBT signing (second pass). */
#include "ds_sign.h"

#include <string.h>

#include "ds_hash.h"
#include "ds_psbt_io.h"
#include "ds_secp.h"

#define HARD 0x80000000u
#define I_WITNESS_UTXO 0x01
#define I_PARTIAL_SIG 0x02
#define WIT_P2WPKH 31 /* witness_utxo value: amount, 0x16, 00 14 <hash> */
#define SIG_MAX 73    /* DER (at most 72 bytes) and the sighash byte */

typedef struct {
    ds_psbt_cache c;
    const ds_sink *dst;
    ds_sha256_ctx h; /* of every source byte, in order */
} pass;

/* Writes n bytes to the sink, at most DS_PSBT_CACHE at a time. */
static int32_t put(const ds_sink *d, const uint8_t *p, size_t n)
{
    while (n > 0) {
        size_t k = n < DS_PSBT_CACHE ? n : DS_PSBT_CACHE;
        int32_t e = d->write(d->ctx, p, k);
        if (e < 0) {
            return e;
        }
        p += k;
        n -= k;
    }
    return DS_OK;
}

/* Reads the source bytes [off, off + n), hashes them and, when emit, writes
 * them to the sink. */
static int32_t copy(pass *p, uint32_t off, uint32_t n, int emit)
{
    uint8_t b[DS_PSBT_CACHE];
    int32_t e;

    while (n > 0) {
        uint32_t k = n < DS_PSBT_CACHE ? n : DS_PSBT_CACHE;
        if ((e = ds_psbt_get(&p->c, off, b, k)) < 0) {
            return e;
        }
        ds_sha256_update(&p->h, b, k);
        if (emit && (e = put(p->dst, b, k)) < 0) {
            return e;
        }
        off += k;
        n -= k;
    }
    return DS_OK;
}

static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    int k;
    for (k = 7; k >= 0; k--) {
        v = v << 8 | p[k];
    }
    return v;
}

/* The witness_utxo value of the input map at off, if it is P2WPKH-sized. */
static int32_t find_witness_utxo(ds_psbt_cache *c, uint32_t off,
                                 uint8_t wit[WIT_P2WPKH])
{
    ds_psbt_entry e;
    int32_t r;

    while ((r = ds_psbt_read_entry(c, off, &e)) == 1) {
        if (e.type == I_WITNESS_UTXO && e.key_len == 1) {
            if (e.val_len != WIT_P2WPKH) {
                return DS_ERR_PSBT;
            }
            return ds_psbt_get(c, e.val, wit, WIT_P2WPKH);
        }
        off = e.next;
    }
    return r < 0 ? r : DS_ERR_PSBT; /* none: the review cannot be of this */
}

/* Signs input k, whose witness_utxo value is wit: our public key in pub, the
 * DER signature and SIGHASH_ALL in sig (*sig_len bytes). */
static int32_t sign_input(const ds_wallet *w, const ds_psbt_review *r,
                          uint32_t k, const uint8_t wit[WIT_P2WPKH],
                          uint8_t pub[33], uint8_t sig[SIG_MAX],
                          size_t *sig_len)
{
    const ds_psbt_input *in = &r->in[k];
    ds_hdkey key;
    ds_secp_buf buf;
    secp256k1_context *ctx = NULL;
    secp256k1_ecdsa_signature s;
    uint8_t pkh[20], sighash[32];
    size_t len = SIG_MAX - 1;
    int32_t e;

    memset(&s, 0, sizeof s);
    memset(sighash, 0, sizeof sighash);
    e = ds_bip32_child(&w->account, in->chain, &key);
    if (e == DS_OK && (e = ds_bip32_child(&key, in->index, &key)) == DS_OK) {
        /* Defence in depth: the key must be the one the input pays. */
        ds_hash160(key.pub, sizeof key.pub, pkh);
        if (le64(wit) != in->amount || wit[8] != 22 || wit[9] != 0x00 ||
            wit[10] != 0x14 || memcmp(wit + 11, pkh, 20) != 0) {
            e = DS_ERR_PSBT;
        }
    }
    if (e == DS_OK) {
        ds_bip143_sighash(r->hash_prevouts, r->hash_sequence, r->hash_outputs,
                          r->version, in->outpoint, pkh, in->amount,
                          in->sequence, r->locktime, sighash);
        ctx = ds_secp_create(&buf);
        /* Default nonce: RFC 6979, no extra data. The result is low S. */
        if (ctx == NULL ||
            !secp256k1_ecdsa_sign(ctx, &s, sighash, key.key, NULL, NULL) ||
            !secp256k1_ecdsa_signature_serialize_der(ctx, sig, &len, &s)) {
            e = DS_ERR_DERIVE;
        }
    }
    if (e == DS_OK) {
        sig[len] = 0x01; /* SIGHASH_ALL */
        *sig_len = len + 1;
        memcpy(pub, key.pub, 33);
    }
    ds_secp_destroy(ctx, &buf);
    ds_hdkey_wipe(&key);
    ds_wipe(&s, sizeof s);
    ds_wipe(sighash, sizeof sighash);
    return e;
}

/* Whether the pair e is a partial signature under our key pub. */
static int32_t is_our_sig(ds_psbt_cache *c, const ds_psbt_entry *e,
                          const uint8_t pub[33])
{
    uint8_t k[34];
    int32_t r;

    if (e->type != I_PARTIAL_SIG || e->key_len != 34) {
        return 0;
    }
    if ((r = ds_psbt_get(c, e->key, k, sizeof k)) < 0) {
        return r;
    }
    return memcmp(k + 1, pub, 33) == 0;
}

/* Copies the map at *off, advancing *off past its separator. When pub is not
 * NULL, drops our old signature and adds sig before the separator. */
static int32_t copy_map(pass *p, uint32_t *off, const uint8_t *pub,
                        const uint8_t *sig, size_t sig_len)
{
    ds_psbt_entry e;
    int32_t r;

    while ((r = ds_psbt_read_entry(&p->c, *off, &e)) == 1) {
        int32_t drop = pub != NULL ? is_our_sig(&p->c, &e, pub) : 0;
        if (drop < 0) {
            return drop;
        }
        if ((r = copy(p, *off, e.next - *off, !drop)) < 0) {
            return r;
        }
        *off = e.next;
    }
    if (r < 0) {
        return r;
    }
    if (pub != NULL) {
        uint8_t head[2] = {34, I_PARTIAL_SIG}, n = (uint8_t)sig_len;
        if ((r = put(p->dst, head, 2)) < 0 || (r = put(p->dst, pub, 33)) < 0 ||
            (r = put(p->dst, &n, 1)) < 0 ||
            (r = put(p->dst, sig, sig_len)) < 0) {
            return r;
        }
    }
    r = copy(p, *off, e.next - *off, 1); /* the separator */
    *off = e.next;
    return r;
}

static int32_t stream(pass *p, const ds_wallet *w, const ds_psbt_review *r)
{
    uint8_t wit[WIT_P2WPKH], pub[33], sig[SIG_MAX];
    size_t sig_len = 0;
    uint32_t off = 5, m, n_maps = 1 + r->n_in + r->n_out;
    int32_t e = copy(p, 0, 5, 1); /* the magic */

    for (m = 0; m < n_maps && e == DS_OK; m++) {
        int ours = m >= 1 && m <= r->n_in && r->in[m - 1].ours;
        if (ours) {
            e = find_witness_utxo(&p->c, off, wit);
            if (e == DS_OK) {
                e = sign_input(w, r, m - 1, wit, pub, sig, &sig_len);
            }
            if (e < 0) {
                break;
            }
        }
        e = copy_map(p, &off, ours ? pub : NULL, sig, sig_len);
    }
    if (e == DS_OK && off != p->c.src->len) {
        e = DS_ERR_PSBT; /* bytes after the last map */
    }
    ds_wipe(sig, sizeof sig);
    return e;
}

int32_t ds_psbt_sign(const ds_source *src, const ds_wallet *w,
                     const ds_psbt_review *r, const ds_sink *dst)
{
    pass p;
    uint8_t digest[32];
    uint32_t k;
    int32_t e;

    if (src == NULL || src->read == NULL || w == NULL || r == NULL ||
        dst == NULL || dst->write == NULL || r->n_in > DS_MAX_INPUTS ||
        r->n_out > DS_MAX_OUTPUTS) {
        return DS_ERR_ARG;
    }
    for (k = 0; k < r->n_in; k++) {
        if (r->in[k].ours && (r->in[k].chain > 1 || r->in[k].index >= HARD)) {
            return DS_ERR_ARG;
        }
    }
    memset(&p, 0, sizeof p);
    p.c.src = src;
    p.dst = dst;

    /* Still the reviewed PSBT? Checked before anything is written. */
    if ((e = ds_psbt_hash_source(&p.c, digest)) < 0) {
        return e;
    }
    if (memcmp(digest, r->psbt_sha256, 32) != 0) {
        return DS_ERR_PSBT;
    }

    /* Copy and sign, hashing again what is read (bypassing the cache). */
    p.c.fill = 0;
    ds_sha256_init(&p.h);
    e = stream(&p, w, r);
    ds_sha256_final(&p.h, digest);
    if (e == DS_OK && memcmp(digest, r->psbt_sha256, 32) != 0) {
        e = DS_ERR_PSBT; /* the source changed while it was copied */
    }
    return e;
}
