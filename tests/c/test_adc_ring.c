/* The ADC ring bookkeeping of the Pico port: pure arithmetic, no SDK. */
#include <stdint.h>

#include "adc_ring.h"
#include "test.h"

#define N 4096u

static void test_no_loss(void) {
    ds_ring r = {0};
    uint32_t start = 99, lost = 99;

    CHECK_EQ_INT(ds_ring_take(&r, 0, N, &start, &lost), 0);
    CHECK_EQ_INT(lost, 0);
    CHECK_EQ_INT(ds_ring_take(&r, 100, N, &start, &lost), 100);
    CHECK_EQ_INT(start, 0);
    CHECK_EQ_INT(lost, 0);
    /* An odd number written: the odd sample waits, so the count stays even. */
    CHECK_EQ_INT(ds_ring_take(&r, 201, N, &start, &lost), 100);
    CHECK_EQ_INT(start, 100);
    CHECK_EQ_INT(ds_ring_take(&r, 201, N, &start, &lost), 0);
    CHECK_EQ_INT(ds_ring_take(&r, 300, N, &start, &lost), 100);
    CHECK_EQ_INT(start, 200);
    CHECK_EQ_INT(lost, 0);
}

static void test_wraps_the_ring_index(void) {
    ds_ring r;
    uint32_t start, lost;

    r.read_total = N - 10;
    CHECK_EQ_INT(ds_ring_take(&r, N + 30, N, &start, &lost), 40);
    CHECK_EQ_INT(start, N - 10);
    CHECK_EQ_INT(ds_ring_take(&r, 2 * N - 70, N, &start, &lost), N - 100);
    CHECK_EQ_INT(start, 30);
    CHECK_EQ_INT(lost, 0);
}

static void test_overflow_is_detected(void) {
    ds_ring r = {0};
    uint32_t start = 1, lost = 0;
    size_t n;

    /* The consumer stalled while the DMA wrote ring_n + 100 samples. */
    n = ds_ring_take(&r, N + 100, N, &start, &lost);
    CHECK(lost > 0);
    CHECK(n <= N - 64);
    CHECK_EQ_INT(start & 1u, 0);
    CHECK_EQ_INT(n & 1u, 0);
    CHECK_EQ_INT(lost + n, N + 100);
    CHECK_EQ_INT(r.read_total, N + 100);

    /* Exactly at the limit: nothing lost; one past it: lost. */
    r.read_total = 0;
    n = ds_ring_take(&r, N - 64, N, &start, &lost);
    CHECK_EQ_INT(lost, 0);
    CHECK_EQ_INT(n, N - 64);
    r.read_total = 0;
    n = ds_ring_take(&r, N - 64 + 2, N, &start, &lost);
    CHECK(lost > 0);
    CHECK(n <= N - 64);

    /* An odd written total after a long stall: parity is kept. */
    r.read_total = 10;
    n = ds_ring_take(&r, 10 + 3 * N + 7, N, &start, &lost);
    CHECK(lost > 0);
    CHECK(n <= N - 64);
    CHECK_EQ_INT(start & 1u, 0);
    CHECK_EQ_INT(n & 1u, 0);
    CHECK_EQ_INT(r.read_total & 1u, 0);
}

static void test_counter_wraps_32_bits(void) {
    ds_ring r;
    uint32_t start, lost;
    size_t n;

    r.read_total = 0xFFFFFFF0u;
    n = ds_ring_take(&r, 0x00000010u, N, &start, &lost); /* 32 new samples */
    CHECK_EQ_INT(n, 32);
    CHECK_EQ_INT(lost, 0);
    CHECK_EQ_INT(start, 0xFFFFFFF0u % N);
    CHECK_EQ_INT(r.read_total, 0x10u);

    /* An overflow across the wrap. */
    r.read_total = 0xFFFFFF00u;
    n = ds_ring_take(&r, 0x00000200u, N, &start, &lost);
    CHECK_EQ_INT(lost + n, 0x300u);
    CHECK(n <= N - 64);
    CHECK_EQ_INT(start & 1u, 0);
}

static void test_intact_after_reading(void) {
    /* A batch read from position 1000 is intact while the DMA has written
     * at most ring_n - margin samples since: its oldest slot is not yet
     * overwritten. */
    CHECK(ds_ring_intact(1000 + 100, 1000, N));
    CHECK(ds_ring_intact(1000 + N - DS_RING_MARGIN, 1000, N));
    CHECK(!ds_ring_intact(1000 + N - DS_RING_MARGIN + 1, 1000, N));
    CHECK(!ds_ring_intact(1000 + N, 1000, N));
    CHECK(!ds_ring_intact(1000 + 3 * N, 1000, N));
    /* Across the 32-bit wrap of the counter. */
    CHECK(ds_ring_intact(0x20u, 0xFFFFFFF0u, N));
    CHECK(!ds_ring_intact(0x20u + N, 0xFFFFFFF0u, N));
}

TEST_MAIN(test_no_loss, test_wraps_the_ring_index, test_overflow_is_detected,
          test_counter_wraps_32_bits, test_intact_after_reading)
