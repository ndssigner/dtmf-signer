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

TEST_MAIN(test_wipe_zeroes)
