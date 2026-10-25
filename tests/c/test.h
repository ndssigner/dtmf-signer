/* Minimal test helpers: each failure prints file:line and bumps ds_failures. */
#ifndef DS_TEST_H
#define DS_TEST_H

#include <stdio.h>
#include <string.h>

static int ds_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);  \
            ds_failures++;                                                   \
        }                                                                    \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                   \
    do {                                                                     \
        long long ds_a_ = (long long)(a), ds_b_ = (long long)(b);            \
        if (ds_a_ != ds_b_) {                                                \
            printf("%s:%d: %s == %s failed: %lld != %lld\n", __FILE__,       \
                   __LINE__, #a, #b, ds_a_, ds_b_);                          \
            ds_failures++;                                                   \
        }                                                                    \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                   \
    do {                                                                     \
        const char *ds_a_ = (a), *ds_b_ = (b);                               \
        if (strcmp(ds_a_, ds_b_) != 0) {                                     \
            printf("%s:%d: %s == %s failed: \"%s\" != \"%s\"\n", __FILE__,   \
                   __LINE__, #a, #b, ds_a_, ds_b_);                          \
            ds_failures++;                                                   \
        }                                                                    \
    } while (0)

/* Compares len bytes of buf against a lowercase hex string of 2*len digits. */
static inline int ds_test_hex_eq(const unsigned char *buf, size_t len, const char *hex)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    if (strlen(hex) != 2 * len) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (hex[2 * i] != digits[buf[i] >> 4] ||
            hex[2 * i + 1] != digits[buf[i] & 15]) {
            return 0;
        }
    }
    return 1;
}

#define CHECK_EQ_HEX(buf, len, hex)                                          \
    do {                                                                     \
        if (!ds_test_hex_eq((const unsigned char *)(buf), (len), (hex))) {   \
            printf("%s:%d: %s != hex %s\n", __FILE__, __LINE__, #buf, hex);  \
            ds_failures++;                                                   \
        }                                                                    \
    } while (0)

/* TEST_MAIN(test_a, test_b, ...): runs each void(void) test; exit status is
 * nonzero if any check failed. */
#define TEST_MAIN(...)                                                       \
    int main(void)                                                           \
    {                                                                        \
        void (*const ds_tests_[])(void) = {__VA_ARGS__};                     \
        size_t ds_i_;                                                        \
        for (ds_i_ = 0; ds_i_ < sizeof ds_tests_ / sizeof ds_tests_[0];      \
             ds_i_++) {                                                      \
            ds_tests_[ds_i_]();                                              \
        }                                                                    \
        if (ds_failures) {                                                   \
            printf("%d check(s) failed\n", ds_failures);                     \
        }                                                                    \
        return ds_failures != 0;                                             \
    }

#endif
