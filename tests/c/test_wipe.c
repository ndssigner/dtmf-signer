#include "ds_core.h"
#include "test.h"

static void test_wipe_zeroes(void)
{
    unsigned char buf[64];
    size_t i;
    int all_zero = 1;
    memset(buf, 0xAA, sizeof buf);
    ds_wipe(buf, sizeof buf);
    for (i = 0; i < sizeof buf; i++) {
        if (buf[i] != 0) {
            all_zero = 0;
        }
    }
    CHECK(all_zero);
}

/* Every result of ds_core.h has its name. */
static void test_core_error_names(void)
{
    static const struct {
        int32_t code;
        const char *name;
    } names[] = {
        {DS_OK, "DS_OK"},
        {DS_ERR_ARG, "DS_ERR_ARG"},
        {DS_ERR_SPACE, "DS_ERR_SPACE"},
        {DS_ERR_PASSPHRASE, "DS_ERR_PASSPHRASE"},
        {DS_ERR_DERIVE, "DS_ERR_DERIVE"},
        {DS_ERR_SEED, "DS_ERR_SEED"},
        {DS_ERR_AUDIO, "DS_ERR_AUDIO"},
        {DS_ERR_PSBT, "DS_ERR_PSBT"},
        {DS_ERR_UNSUPPORTED, "DS_ERR_UNSUPPORTED"},
        {DS_ERR_AMOUNTS, "DS_ERR_AMOUNTS"},
        {DS_ERR_SIGHASH, "DS_ERR_SIGHASH"},
        {DS_ERR_NETWORK, "DS_ERR_NETWORK"},
        {DS_ERR_CHANGE, "DS_ERR_CHANGE"},
        {DS_ERR_LIMIT, "DS_ERR_LIMIT"},
        {DS_ERR_STATE, "DS_ERR_STATE"},
        {DS_ERR_POLICY, "DS_ERR_POLICY"},
        {DS_WARN_AIR, "DS_WARN_AIR"},
        {DS_NEED_CONFIRM, "DS_NEED_CONFIRM"},
    };
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        CHECK_EQ_STR(ds_error_name(names[i].code), names[i].name);
    }
    CHECK_EQ_STR(ds_error_name(-100), "unknown error");
    CHECK_EQ_STR(ds_error_name(42), "unknown error");
}

TEST_MAIN(test_wipe_zeroes, test_core_error_names)
