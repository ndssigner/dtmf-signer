#include "ds_hash.h"
#include "test.h"
#include "ur_tones.h"

#include <string.h>

static const char *const MSG112 =
    "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjkl"
    "mnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

static void sha(const char *m, size_t n, uint8_t out[64])
{
    ds_sha512((const uint8_t *)m, n, out);
}

static void test_sha512_fips(void)
{
    uint8_t out[64];
    ds_sha512_ctx c;
    size_t pos = 0, i;
    static const size_t chunks[] = {1, 63, 48};

    sha("", 0, out);
    CHECK_EQ_HEX(out, 64, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    sha("abc", 3, out);
    CHECK_EQ_HEX(out, 64, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    CHECK_EQ_INT(strlen(MSG112), 112);
    sha(MSG112, 112, out);
    CHECK_EQ_HEX(out, 64, "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");

    ds_sha512_init(&c);
    for (i = 0; pos < 112; i++) {
        size_t n = chunks[i % 3];
        if (n > 112 - pos) {
            n = 112 - pos;
        }
        ds_sha512_update(&c, (const uint8_t *)MSG112 + pos, n);
        pos += n;
    }
    ds_sha512_final(&c, out);
    CHECK_EQ_HEX(out, 64, "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
    /* final wipes the context */
    for (i = 0; i < sizeof c; i++) {
        CHECK(((const uint8_t *)&c)[i] == 0);
    }
}

static void test_hmac_sha512_rfc4231(void)
{
    uint8_t out[64], key[131];

    memset(key, 0x0b, 20);
    ds_hmac_sha512(key, 20, (const uint8_t *)"Hi There", 8, out);
    CHECK_EQ_HEX(out, 64, "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854");
    ds_hmac_sha512((const uint8_t *)"Jefe", 4,
                   (const uint8_t *)"what do ya want for nothing?", 28, out);
    CHECK_EQ_HEX(out, 64, "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
    memset(key, 0xaa, 131);
    ds_hmac_sha512(key, 131,
                   (const uint8_t *)"Test Using Larger Than Block-Size Key - Hash Key First", 54, out);
    CHECK_EQ_HEX(out, 64, "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f3526b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598");
}

static void test_ripemd160(void)
{
    uint8_t out[20];

    ds_ripemd160((const uint8_t *)"", 0, out);
    CHECK_EQ_HEX(out, 20, "9c1185a5c5e9fc54612808977ee8f548b2258d31");
    ds_ripemd160((const uint8_t *)"abc", 3, out);
    CHECK_EQ_HEX(out, 20, "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc");
    ds_ripemd160((const uint8_t *)"message digest", 14, out);
    CHECK_EQ_HEX(out, 20, "5d0689ef49d2fae572b881b123a85ffa21595f36");
}

static void test_hash160(void)
{
    static const uint8_t pub[33] = {
        0x03, 0x39, 0xa3, 0x60, 0x13, 0x30, 0x15, 0x97, 0xda, 0xef, 0x41,
        0xfb, 0xe5, 0x93, 0xa0, 0x2c, 0xc5, 0x13, 0xd0, 0xb5, 0x55, 0x27,
        0xec, 0x2d, 0xf1, 0x05, 0x0e, 0x2e, 0x8f, 0xf4, 0x9c, 0x85, 0xc2};
    uint8_t out[20];

    ds_hash160(pub, sizeof pub, out);
    CHECK_EQ_HEX(out, 4, "3442193e");
}

TEST_MAIN(test_sha512_fips, test_hmac_sha512_rfc4231, test_ripemd160,
          test_hash160)
