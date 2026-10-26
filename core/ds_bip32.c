#include "ds_bip32.h"

#include <string.h>

#include "ds_hash.h"
#include "ds_secp.h"

static int fill_pub(const uint8_t key[32], uint8_t pub[33], uint32_t *fp)
{
    ds_secp_buf buf;
    secp256k1_context *ctx = ds_secp_create(&buf);
    secp256k1_pubkey pk;
    size_t len = 33;
    uint8_t h[20];
    int ok = 0;

    if (ctx != NULL) {
        ok = secp256k1_ec_pubkey_create(ctx, &pk, key) &&
             secp256k1_ec_pubkey_serialize(ctx, pub, &len, &pk,
                                           SECP256K1_EC_COMPRESSED) &&
             len == 33;
    }
    ds_secp_destroy(ctx, &buf);
    ds_wipe(&pk, sizeof pk);
    if (ok) {
        ds_hash160(pub, 33, h);
        *fp = (uint32_t)h[0] << 24 | (uint32_t)h[1] << 16 | (uint32_t)h[2] << 8 | h[3];
    }
    return ok;
}

void ds_hdkey_wipe(ds_hdkey *k)
{
    ds_wipe(k, sizeof *k);
}

int32_t ds_bip32_master(const uint8_t *seed, size_t len, ds_hdkey *out)
{
    static const uint8_t salt[] = {'B', 'i', 't', 'c', 'o', 'i', 'n', ' ', 's', 'e', 'e', 'd'};
    uint8_t i[64];
    ds_hdkey k;
    int32_t r = DS_ERR_DERIVE;

    if (seed == NULL || out == NULL || len < 16 || len > 64) {
        return DS_ERR_ARG;
    }
    ds_hmac_sha512(salt, sizeof salt, seed, len, i);
    memcpy(k.key, i, 32);
    memcpy(k.chain, i + 32, 32);
    k.parent_fp = 0;
    k.depth = 0;
    if (secp256k1_ec_seckey_verify(secp256k1_context_static, k.key) &&
        fill_pub(k.key, k.pub, &k.fp)) {
        *out = k;
        r = DS_OK;
    }
    ds_wipe(i, sizeof i);
    ds_wipe(&k, sizeof k);
    return r;
}

int32_t ds_bip32_child(const ds_hdkey *parent, uint32_t index, ds_hdkey *out)
{
    uint8_t data[37], i[64];
    ds_hdkey k;
    int32_t r = DS_ERR_DERIVE;

    if (parent == NULL || out == NULL || parent->depth == 255) {
        return DS_ERR_ARG;
    }
    if (index & 0x80000000u) {
        data[0] = 0x00;
        memcpy(data + 1, parent->key, 32);
    } else {
        memcpy(data, parent->pub, 33);
    }
    data[33] = (uint8_t)(index >> 24);
    data[34] = (uint8_t)(index >> 16);
    data[35] = (uint8_t)(index >> 8);
    data[36] = (uint8_t)index;
    ds_hmac_sha512(parent->chain, 32, data, sizeof data, i);

    memcpy(k.key, parent->key, 32);
    /* Fails if IL >= n or the sum is zero. */
    if (secp256k1_ec_seckey_tweak_add(secp256k1_context_static, k.key, i)) {
        memcpy(k.chain, i + 32, 32);
        k.parent_fp = parent->fp;
        k.depth = (uint8_t)(parent->depth + 1);
        if (fill_pub(k.key, k.pub, &k.fp)) {
            *out = k;
            r = DS_OK;
        }
    }
    ds_wipe(data, sizeof data);
    ds_wipe(i, sizeof i);
    ds_wipe(&k, sizeof k);
    return r;
}

int32_t ds_bip32_path(const ds_hdkey *master, const uint32_t *path, size_t n,
                      ds_hdkey *out)
{
    ds_hdkey cur;
    size_t s;
    int32_t r = DS_OK;

    if (master == NULL || out == NULL || (n > 0 && path == NULL)) {
        if (out != NULL) {
            ds_hdkey_wipe(out);
        }
        return DS_ERR_ARG;
    }
    cur = *master;
    for (s = 0; s < n && r == DS_OK; s++) {
        r = ds_bip32_child(&cur, path[s], &cur);
    }
    if (r == DS_OK) {
        *out = cur;
    } else {
        ds_hdkey_wipe(out);
    }
    ds_wipe(&cur, sizeof cur);
    return r;
}

void ds_bip32_to_xpub(const ds_hdkey *k, uint32_t source_fp, const uint32_t *path,
                      size_t n, int testnet, ut_xpub *out)
{
    memcpy(out->key, k->pub, 33);
    memcpy(out->chain, k->chain, 32);
    out->parent_fp = k->parent_fp;
    out->source_fp = source_fp;
    out->path = path;
    out->depth = n;
    out->testnet = testnet;
}
