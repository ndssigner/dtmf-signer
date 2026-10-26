#include <stdint.h>
#include <string.h>

#include "ds_bip32.h"
#include "ds_bip39.h"
#include "ds_bitcoin.h"
#include "ds_hash.h"
#include "test.h"

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

static void test_sha256_fips(void)
{
    uint8_t d[32], big[1000];
    ds_sha256_ctx c;
    size_t i;
    static const char m448[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

    ds_sha256_init(&c);
    ds_sha256_final(&c, d);
    CHECK_EQ_HEX(d, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    ds_sha256_init(&c);
    ds_sha256_update(&c, (const uint8_t *)"abc", 3);
    ds_sha256_final(&c, d);
    CHECK_EQ_HEX(d, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    ds_sha256_init(&c);
    ds_sha256_update(&c, (const uint8_t *)m448, strlen(m448));
    ds_sha256_final(&c, d);
    CHECK_EQ_HEX(d, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    /* Same message in uneven pieces. */
    ds_sha256_init(&c);
    ds_sha256_update(&c, (const uint8_t *)m448, 1);
    ds_sha256_update(&c, (const uint8_t *)m448 + 1, 0);
    ds_sha256_update(&c, (const uint8_t *)m448 + 1, 40);
    ds_sha256_update(&c, (const uint8_t *)m448 + 41, strlen(m448) - 41);
    ds_sha256_final(&c, d);
    CHECK_EQ_HEX(d, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    /* One million 'a' (FIPS 180-2), fed in 1000-byte pieces. */
    memset(big, 'a', sizeof big);
    ds_sha256_init(&c);
    for (i = 0; i < 1000; i++) {
        ds_sha256_update(&c, big, sizeof big);
    }
    ds_sha256_final(&c, d);
    CHECK_EQ_HEX(d, 32, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    /* final wipes the context */
    CHECK(c.len == 0 && c.fill == 0 && c.h[0] == 0);

    /* sha256d("abc") = sha256(sha256("abc")) */
    ds_sha256d((const uint8_t *)"abc", 3, d);
    CHECK_EQ_HEX(d, 32, "4f8b42c22dd3729b519ba6f68d2da7cc5b2d606d05daed5ad5128cc03e6c6358");
}

static void test_compact(void)
{
    uint8_t buf[9], in[9];
    uint64_t v;
    size_t n;
    static const struct {
        uint64_t v;
        const char *hex;
    } t[] = {
        {0, "00"},
        {0xfc, "fc"},
        {0xfd, "fdfd00"},
        {0x0100, "fd0001"},
        {0xffff, "fdffff"},
        {0x10000, "fe00000100"},
        {0xffffffffu, "feffffffff"},
        {0x100000000ull, "ff0000000001000000"},
        {0x0123456789abcdefull, "ffefcdab8967452301"},
    };
    size_t i, k;

    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        n = ds_compact_encode(t[i].v, buf);
        CHECK_EQ_INT(n, strlen(t[i].hex) / 2);
        CHECK_EQ_HEX(buf, n, t[i].hex);
        unhex(t[i].hex, in);
        CHECK_EQ_INT(ds_compact_decode(in, n, &v), (long long)n);
        CHECK(v == t[i].v);
        /* extra trailing bytes do not matter */
        CHECK_EQ_INT(ds_compact_decode(in, 9, &v), (long long)n);
        /* every truncation fails */
        for (k = 0; k < n; k++) {
            CHECK_EQ_INT(ds_compact_decode(in, k, &v), DS_ERR_PSBT);
        }
    }
}

static void check_addr(const char *script_hex, int testnet, const char *want)
{
    uint8_t s[80];
    char out[100];
    size_t n = unhex(script_hex, s);

    CHECK_EQ_INT(ds_address(s, n, testnet, out, sizeof out), (long long)strlen(want));
    CHECK_EQ_STR(out, want);
}

/* Vectors from the BIP 173 and BIP 350 texts (valid segwit addresses; the
 * uppercase BIP 173 one is given in lowercase, the encoder's output). */
static void test_address_bip173_350(void)
{
    check_addr("0014751e76e8199196d454941c45d1b3a323f1433bd6", 0,
               "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4");
    check_addr("00201863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262", 1,
               "tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sl5k7");
    check_addr("00201863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262", 0,
               "bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3");
    check_addr("0020000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433", 1,
               "tb1qqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvsesrxh6hy");
    check_addr("5120000000c4a5cad46221b2a187905e5266362b99d5e91c6ce24d165dab93e86433", 1,
               "tb1pqqqqp399et2xygdj5xreqhjjvcmzhxw4aywxecjdzew6hylgvsesf3hn0c");
    check_addr("512079be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798", 0,
               "bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0");
}

/* Expected values computed independently with Python hashlib: version byte
 * + HASH160, then base58check. P2SH is the P2SH-P2WPKH of the same key. */
static void test_address_base58(void)
{
    uint8_t pub[33], h[20], redeem[22], s[25];
    char out[64];

    unhex("0339a36013301597daef41fbe593a02cc513d0b55527ec2df1050e2e8ff49c85c2", pub);
    ds_hash160(pub, 33, h);
    CHECK_EQ_HEX(h, 20, "3442193e1bb70916e914552172cd4e2dbc9df811");
    unhex("76a914", s);
    memcpy(s + 3, h, 20);
    s[23] = 0x88;
    s[24] = 0xac;
    CHECK_EQ_INT(ds_address(s, 25, 0, out, sizeof out), 34);
    CHECK_EQ_STR(out, "15mKKb2eos1hWa6tisdPwwDC1a5J1y9nma");
    CHECK_EQ_INT(ds_address(s, 25, 1, out, sizeof out), 34);
    CHECK_EQ_STR(out, "mkHGce7dctSxHgaWSSbmmrRWsZfzz7MxMk");

    redeem[0] = 0x00;
    redeem[1] = 0x14;
    memcpy(redeem + 2, h, 20);
    ds_hash160(redeem, 22, h);
    s[0] = 0xa9;
    s[1] = 0x14;
    memcpy(s + 2, h, 20);
    s[22] = 0x87;
    CHECK_EQ_INT(ds_address(s, 23, 0, out, sizeof out), 34);
    CHECK_EQ_STR(out, "3PpgpssV7mcAGpZRWiCWhodUTnjpoSZg7a");
    CHECK_EQ_INT(ds_address(s, 23, 1, out, sizeof out), 35);
    CHECK_EQ_STR(out, "2NFNttcoWjE7WUcByBqpPKkcjg8wzgnU5HE");

    /* leading zero bytes become '1's */
    memset(s, 0, sizeof s);
    s[0] = 0x76; s[1] = 0xa9; s[2] = 0x14; s[23] = 0x88; s[24] = 0xac;
    CHECK_EQ_INT(ds_address(s, 25, 0, out, sizeof out), 27);
    CHECK_EQ_STR(out, "1111111111111111111114oLvT2");
}

static void test_address_errors(void)
{
    uint8_t s[34];
    char out[100], small[10];

    unhex("0014751e76e8199196d454941c45d1b3a323f1433bd6", s);
    CHECK_EQ_INT(ds_address(s, 22, 0, small, sizeof small), DS_ERR_SPACE);
    CHECK_EQ_INT(ds_address(s, 22, 0, out, 42), DS_ERR_SPACE);
    CHECK_EQ_INT(ds_address(s, 22, 0, out, 43), 42);
    CHECK_EQ_INT(ds_address(s, 21, 0, out, sizeof out), DS_ERR_UNSUPPORTED);
    CHECK_EQ_INT(ds_address(s, 0, 0, out, sizeof out), DS_ERR_UNSUPPORTED);
    s[0] = 0x6a; /* OP_RETURN-like */
    CHECK_EQ_INT(ds_address(s, 22, 0, out, sizeof out), DS_ERR_UNSUPPORTED);
}

/* BIP 84 first receiving address of the "abandon ... about" wallet. */
static void test_address_bip84(void)
{
    static const uint32_t H = 0x80000000u;
    const uint32_t path[] = {84 | H, 0 | H, 0 | H, 0, 0};
    uint8_t ent[16], seed[64], script[22];
    ds_hdkey m, k;
    char out[64];

    memset(ent, 0, sizeof ent);
    CHECK_EQ_INT(ds_bip39_seed(ent, 16, "", seed), DS_OK);
    CHECK_EQ_INT(ds_bip32_master(seed, 64, &m), DS_OK);
    CHECK_EQ_INT(ds_bip32_path(&m, path, 5, &k), DS_OK);
    script[0] = 0x00;
    script[1] = 0x14;
    ds_hash160(k.pub, 33, script + 2);
    CHECK_EQ_INT(ds_address(script, 22, 0, out, sizeof out), 42);
    CHECK_EQ_STR(out, "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu");
    ds_hdkey_wipe(&k);
    ds_hdkey_wipe(&m);
    ds_wipe(seed, sizeof seed);
}

TEST_MAIN(test_sha256_fips, test_compact, test_address_bip173_350,
          test_address_base58, test_address_errors, test_address_bip84)
