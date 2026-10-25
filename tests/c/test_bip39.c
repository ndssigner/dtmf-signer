#include <stdint.h>
#include <string.h>

#include "ds_bip39.h"
#include "test.h"

static void check_vector(uint8_t fill, size_t len, const char *phrase, const char *seed_hex)
{
    uint8_t ent[32], seed[64];
    char out[256];
    int32_t n;

    memset(ent, fill, sizeof ent);
    n = ds_bip39_mnemonic(ent, len, out, sizeof out);
    CHECK_EQ_INT(n, (long long)strlen(phrase));
    CHECK_EQ_STR(out, phrase);
    CHECK_EQ_INT(ds_bip39_seed(ent, len, "TREZOR", seed), DS_OK);
    CHECK_EQ_HEX(seed, 64, seed_hex);
}

static void test_bip39_zero_12(void)
{
    check_vector(0x00, 16,
                 "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about",
                 "c55257c360c07c72029aebc1b53c05ed0362ada38ead3e3e9efa3708e53495531f09a6987599d18264c1e1c92f2cf141630c7a3c4ab7c81b2f001698e7463b04");
}

static void test_bip39_7f_12(void)
{
    check_vector(0x7f, 16,
                 "legal winner thank year wave sausage worth useful legal winner thank yellow",
                 "2e8905819b8723fe2c1d161860e5ee1830318dbf49a83bd451cfb8440c28bd6fa457fe1296106559a3c80937a1c1069be3a3a5bd381ee6260e8d9739fce1f607");
}

static void test_bip39_ff_24(void)
{
    check_vector(0xff, 32,
                 "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo vote",
                 "dd48c104698c30cfe2b6142103248622fb7bb0ff692eebb00089b32d22484e1613912f0a5b694407be899ffd31ed3992c456cdf60f5d4564b8ba3f05a69890ad");
}

static void test_bip39_rejects_non_ascii(void)
{
    uint8_t ent[16] = {0}, a[64], b[64];
    char longpw[258];

    memset(a, 0x55, sizeof a);
    CHECK_EQ_INT(ds_bip39_seed(ent, 16, "contrase\xc3\xb1" "a", a), DS_ERR_PASSPHRASE);
    CHECK_EQ_INT(ds_bip39_seed(ent, 16, "tab\there", a), DS_ERR_PASSPHRASE);
    memset(longpw, 'a', 257);
    longpw[257] = '\0';
    CHECK_EQ_INT(ds_bip39_seed(ent, 16, longpw, a), DS_ERR_PASSPHRASE);
    longpw[256] = '\0';
    CHECK_EQ_INT(ds_bip39_seed(ent, 16, longpw, a), DS_OK);
    CHECK_EQ_INT(ds_bip39_seed(ent, 16, "", a), DS_OK);
    CHECK_EQ_INT(ds_bip39_seed(ent, 16, NULL, b), DS_OK);
    CHECK(memcmp(a, b, 64) == 0);
}

static void test_bip39_bad_len(void)
{
    uint8_t ent[32] = {0}, seed[64];
    char out[256], small[10];

    CHECK_EQ_INT(ds_bip39_mnemonic(ent, 20, out, sizeof out), DS_ERR_ARG);
    CHECK_EQ_INT(ds_bip39_seed(ent, 20, NULL, seed), DS_ERR_ARG);
    CHECK_EQ_INT(ds_bip39_mnemonic(ent, 16, small, sizeof small), DS_ERR_SPACE);
    /* exact fit: phrase length + NUL */
    CHECK_EQ_INT(ds_bip39_mnemonic(ent, 16, out, 93), DS_ERR_SPACE);
    CHECK_EQ_INT(ds_bip39_mnemonic(ent, 16, out, 94), 93);
}

TEST_MAIN(test_bip39_zero_12, test_bip39_7f_12, test_bip39_ff_24,
          test_bip39_rejects_non_ascii, test_bip39_bad_len)
