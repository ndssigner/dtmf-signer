#include <stdint.h>
#include <string.h>

#include "ds_wallet.h"
#include "test.h"

#define XPUB84 "xpub6CatWdiZiodmUeTDp8LT5or8nmbKNcuyvz7WyksVFkKB4RHwCD3XyuvPEbvqAQY3rAPshWcMLoP2fMFMKHPJ4ZeZXYVUhLv1VMrjPC7PW6V"

static const uint8_t zero16[16] = {0};

static void test_wallet_bip84(void)
{
    ds_wallet w;
    char text[160];

    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 16, NULL, 0), DS_OK);
    CHECK_EQ_INT(w.seed_fp, 0x73c5da0au);
    CHECK_EQ_INT(w.wallet_fp, 0x73c5da0au);
    CHECK(ds_wallet_xpub(&w, text, sizeof text) > 0);
    CHECK_EQ_STR(text, XPUB84);
    CHECK(ds_wallet_descriptor(&w, text, sizeof text) > 0);
    CHECK(strncmp(text, "wpkh([73c5da0a/84'/0'/0']xpub6CatWdiZ", 37) == 0);
    CHECK(strchr(text, '#') != NULL);
    ds_wallet_wipe(&w);
}

static void test_wallet_seed_fp_ignores_passphrase(void)
{
    ds_wallet w;

    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 16, "TREZOR", 0), DS_OK);
    CHECK_EQ_INT(w.seed_fp, 0x73c5da0au);
    CHECK(w.wallet_fp != 0x73c5da0au);
    ds_wallet_wipe(&w);
}

static void test_wallet_testnet(void)
{
    ds_wallet w;
    char text[160];

    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 16, NULL, 1), DS_OK);
    CHECK(ds_wallet_xpub(&w, text, sizeof text) > 0);
    CHECK(strncmp(text, "tpub", 4) == 0);
    CHECK(ds_wallet_descriptor(&w, text, sizeof text) > 0);
    CHECK(strstr(text, "/84'/1'/0'") != NULL);
    ds_wallet_wipe(&w);
}

static void test_wallet_bad_input_leaves_wiped(void)
{
    ds_wallet w;
    unsigned char zeros[sizeof w];

    memset(&w, 0xAA, sizeof w);
    memset(zeros, 0, sizeof zeros);
    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 16, "caf\xc3\xa9", 0), DS_ERR_PASSPHRASE);
    CHECK(memcmp(&w, zeros, sizeof w) == 0);
    memset(&w, 0xAA, sizeof w);
    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 5, NULL, 0), DS_ERR_ARG);
    CHECK(memcmp(&w, zeros, sizeof w) == 0);
}

static void test_wallet_wipe_clears(void)
{
    ds_wallet w;
    unsigned char zeros[sizeof w];
    size_t i;

    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 16, NULL, 0), DS_OK);
    ds_wallet_wipe(&w);
    memset(zeros, 0, sizeof zeros);
    for (i = 0; i < sizeof w; i++) {
        CHECK(((unsigned char *)&w)[i] == 0);
    }
    CHECK(memcmp(&w, zeros, sizeof w) == 0);
}

static void test_wallet_small_cap_is_ds_error(void)
{
    ds_wallet w;
    char text[160];

    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 16, NULL, 0), DS_OK);
    CHECK_EQ_INT(ds_wallet_xpub(&w, text, 10), DS_ERR_SPACE);
    CHECK_EQ_INT(ds_wallet_descriptor(&w, text, 10), DS_ERR_SPACE);
    CHECK_EQ_INT(ds_wallet_xpub(&w, text, 0), DS_ERR_SPACE);
    ds_wallet_wipe(&w);
}

/* The crypto-account (current = 0) of the account: the same CBOR as
 * ut_account_cbor with wpkh(@0) and the wallet's key, a map whose first entry
 * is the master fingerprint. */
static void test_app_wallet_account_cbor(void)
{
    ds_wallet w;
    ut_xpub x;
    static const char *const scripts[] = {"wpkh(@0)"};
    uint8_t got[512], want[512];
    int32_t n, m;

    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 16, NULL, 0), DS_OK);
    ds_bip32_to_xpub(&w.account, w.wallet_fp, w.path, 3, w.testnet, &x);
    m = ut_account_cbor(w.wallet_fp, scripts, &x, 1, 0, want, sizeof want);
    CHECK(m > 0);
    n = ds_wallet_account_cbor(&w, got, sizeof got);
    CHECK_EQ_INT(n, m);
    CHECK(n == m && m > 0 && memcmp(got, want, (size_t)n) == 0);
    CHECK_EQ_HEX(got, 7, "a2011a73c5da0a");
    CHECK_EQ_INT(ds_wallet_account_cbor(&w, got, 10), DS_ERR_SPACE);
    ds_wallet_wipe(&w);

    /* With a passphrase the master fingerprint is the wallet's. */
    CHECK_EQ_INT(ds_wallet_load(&w, zero16, 16, "TREZOR", 1), DS_OK);
    ds_bip32_to_xpub(&w.account, w.wallet_fp, w.path, 3, w.testnet, &x);
    m = ut_account_cbor(w.wallet_fp, scripts, &x, 1, 0, want, sizeof want);
    n = ds_wallet_account_cbor(&w, got, sizeof got);
    CHECK(m > 0 && n == m && memcmp(got, want, (size_t)n) == 0);
    ds_wallet_wipe(&w);
}

TEST_MAIN(test_wallet_bip84, test_wallet_seed_fp_ignores_passphrase,
          test_wallet_testnet, test_wallet_bad_input_leaves_wiped,
          test_wallet_wipe_clears, test_wallet_small_cap_is_ds_error,
          test_app_wallet_account_cbor)
