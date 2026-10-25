#include "ds_wallet.h"

#include <string.h>

#include "ds_bip39.h"

#define HARD 0x80000000u

static uint32_t fp_of_seed(const uint8_t seed[64], int32_t *err)
{
    ds_hdkey m;
    uint32_t fp = 0;

    *err = ds_bip32_master(seed, 64, &m);
    if (*err == DS_OK) {
        fp = m.fp;
    }
    ds_hdkey_wipe(&m);
    return fp;
}

int32_t ds_wallet_load(ds_wallet *w, const uint8_t *entropy, size_t len,
                       const char *passphrase, int testnet)
{
    uint8_t seed[64];
    uint8_t plain[64];
    ds_hdkey master;
    int32_t err;
    int has_pass = passphrase != NULL && passphrase[0] != '\0';

    ds_wipe(w, sizeof *w);
    ds_wipe(&master, sizeof master);

    /* Seed fingerprint: empty passphrase. */
    err = ds_bip39_seed(entropy, len, NULL, plain);
    if (err != DS_OK) {
        goto done;
    }
    w->seed_fp = fp_of_seed(plain, &err);
    if (err != DS_OK) {
        goto done;
    }

    /* Wallet fingerprint and account: the given passphrase (reuse if empty). */
    if (has_pass) {
        err = ds_bip39_seed(entropy, len, passphrase, seed);
        if (err != DS_OK) {
            goto done;
        }
    } else {
        memcpy(seed, plain, sizeof seed);
    }
    err = ds_bip32_master(seed, sizeof seed, &master);
    if (err != DS_OK) {
        goto done;
    }
    w->wallet_fp = master.fp;
    w->testnet = testnet != 0;
    w->path[0] = 84u | HARD;
    w->path[1] = (w->testnet ? 1u : 0u) | HARD;
    w->path[2] = 0u | HARD;
    err = ds_bip32_path(&master, w->path, 3, &w->account);

done:
    ds_wipe(seed, sizeof seed);
    ds_wipe(plain, sizeof plain);
    ds_hdkey_wipe(&master);
    if (err != DS_OK) {
        ds_wallet_wipe(w);
        return err;
    }
    return DS_OK;
}

int32_t ds_wallet_xpub(const ds_wallet *w, char *out, size_t cap)
{
    ut_xpub x;

    ds_bip32_to_xpub(&w->account, w->wallet_fp, w->path, 3, w->testnet, &x);
    return ut_xpub_text(&x, "xpub", out, cap);
}

int32_t ds_wallet_descriptor(const ds_wallet *w, char *out, size_t cap)
{
    ut_xpub x;

    ds_bip32_to_xpub(&w->account, w->wallet_fp, w->path, 3, w->testnet, &x);
    return ut_descriptor_text("wpkh(@0)", &x, out, cap);
}

void ds_wallet_wipe(ds_wallet *w)
{
    ds_wipe(w, sizeof *w);
}
