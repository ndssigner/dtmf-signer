#include <stdint.h>
#include <string.h>

#include "ds_bip32.h"
#include "test.h"

#define H 0x80000000u

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

/* Derives path from master and checks fingerprint and xpub text. */
static void check_key(const ds_hdkey *master, const uint32_t *path, size_t n,
                      const char *fp_hex, const char *xpub)
{
    ds_hdkey k;
    ut_xpub x;
    char text[128];
    uint8_t fp[4];

    CHECK_EQ_INT(ds_bip32_path(master, path, n, &k), DS_OK);
    fp[0] = (uint8_t)(k.fp >> 24);
    fp[1] = (uint8_t)(k.fp >> 16);
    fp[2] = (uint8_t)(k.fp >> 8);
    fp[3] = (uint8_t)k.fp;
    if (fp_hex != NULL) {
        CHECK_EQ_HEX(fp, 4, fp_hex);
    }
    ds_bip32_to_xpub(&k, master->fp, path, n, 0, &x);
    CHECK(ut_xpub_text(&x, "xpub", text, sizeof text) > 0);
    CHECK_EQ_STR(text, xpub);
    ds_hdkey_wipe(&k);
}

static void test_bip32_vector1(void)
{
    uint8_t seed[64];
    ds_hdkey m;
    static const uint32_t p1[] = {H};
    static const uint32_t p2[] = {H, 1, 2 | H, 2, 1000000000u};
    size_t n = unhex("000102030405060708090a0b0c0d0e0f", seed);

    CHECK_EQ_INT(ds_bip32_master(seed, n, &m), DS_OK);
    check_key(&m, NULL, 0, "3442193e",
              "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8");
    check_key(&m, p1, 1, NULL,
              "xpub68Gmy5EdvgibQVfPdqkBBCHxA5htiqg55crXYuXoQRKfDBFA1WEjWgP6LHhwBZeNK1VTsfTFUHCdrfp1bgwQ9xv5ski8PX9rL2dZXvgGDnw");
    check_key(&m, p2, 5, NULL,
              "xpub6H1LXWLaKsWFhvm6RVpEL9P4KfRZSW7abD2ttkWP3SSQvnyA8FSVqNTEcYFgJS2UaFcxupHiYkro49S8yGasTvXEYBVPamhGW6cFJodrTHy");
    ds_hdkey_wipe(&m);
}

static void test_bip32_vector3(void)
{
    uint8_t seed[64];
    ds_hdkey m;
    static const uint32_t p1[] = {H};
    size_t n = unhex("4b381541583be4423346c643850da4b320e46a87ae3d2a4e6da11eba819cd4acba45d239319ac14f863b8d5ab5a0d0c64d2e8a1e7d1457df2e5a3c51c73235be", seed);

    CHECK_EQ_INT(n, 64);
    CHECK_EQ_INT(ds_bip32_master(seed, n, &m), DS_OK);
    check_key(&m, NULL, 0, NULL,
              "xpub661MyMwAqRbcEZVB4dScxMAdx6d4nFc9nvyvH3v4gJL378CSRZiYmhRoP7mBy6gSPSCYk6SzXPTf3ND1cZAceL7SfJ1Z3GC8vBgp2epUt13");
    check_key(&m, p1, 1, NULL,
              "xpub68NZiKmJWnxxS6aaHmn81bvJeTESw724CRDs6HbuccFQN9Ku14VQrADWgqbhhTHBaohPX4CjNLf9fq9MYo6oDaPPLPxSb7gwQN3ih19Zm4Y");
    ds_hdkey_wipe(&m);
}

static void test_bip32_args_and_wipe(void)
{
    uint8_t seed[64];
    ds_hdkey m, k, z;
    static const uint32_t p[] = {H, 1};

    memset(seed, 0x42, sizeof seed);
    memset(&z, 0, sizeof z);
    CHECK_EQ_INT(ds_bip32_master(seed, 15, &m), DS_ERR_ARG);
    CHECK_EQ_INT(ds_bip32_master(seed, 65, &m), DS_ERR_ARG);
    CHECK_EQ_INT(ds_bip32_master(seed, 16, &m), DS_OK);
    CHECK_EQ_INT(m.depth, 0);
    CHECK_EQ_INT(m.parent_fp, 0);
    /* depth 255 cannot go deeper */
    k = m;
    k.depth = 255;
    CHECK_EQ_INT(ds_bip32_child(&k, 0, &k), DS_ERR_ARG);
    /* path with n == 0 is the master itself; child depth and parent fp chain */
    CHECK_EQ_INT(ds_bip32_path(&m, NULL, 0, &k), DS_OK);
    CHECK(memcmp(&k, &m, sizeof k) == 0);
    CHECK_EQ_INT(ds_bip32_path(&m, p, 2, &k), DS_OK);
    CHECK_EQ_INT(k.depth, 2);
    ds_hdkey_wipe(&k);
    CHECK(memcmp(&k, &z, sizeof k) == 0);
    ds_hdkey_wipe(&m);
}

TEST_MAIN(test_bip32_vector1, test_bip32_vector3, test_bip32_args_and_wipe)
