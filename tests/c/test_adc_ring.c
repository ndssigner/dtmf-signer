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

/* The newest pair is held back for the next take: the DMA may count a
 * sample a moment before its write lands. */
static void test_hold_back_newest_pair(void) {
    ds_ring r = {0};
    uint32_t start = 99, lost = 99;

    /* Fewer than a pair past what was taken: nothing new, no overflow. */
    CHECK_EQ_INT(ds_ring_hold(&r, 0), 0);
    CHECK_EQ_INT(ds_ring_hold(&r, 1), 0);
    CHECK_EQ_INT(ds_ring_take(&r, ds_ring_hold(&r, 1), N, &start, &lost), 0);
    CHECK_EQ_INT(lost, 0);
    CHECK_EQ_INT(ds_ring_hold(&r, 2), 0);
    CHECK_EQ_INT(ds_ring_take(&r, ds_ring_hold(&r, 3), N, &start, &lost), 0);
    CHECK_EQ_INT(lost, 0);
    /* 100 written: 98 taken, the pair 98-99 waits. */
    CHECK_EQ_INT(ds_ring_take(&r, ds_ring_hold(&r, 100), N, &start, &lost), 98);
    CHECK_EQ_INT(start, 0);
    CHECK_EQ_INT(lost, 0);
    /* 101 written: still nothing (98-99 is now the newest whole pair). */
    CHECK_EQ_INT(ds_ring_take(&r, ds_ring_hold(&r, 101), N, &start, &lost), 0);
    /* 102: the pair 98-99 comes, from an even position. */
    CHECK_EQ_INT(ds_ring_take(&r, ds_ring_hold(&r, 102), N, &start, &lost), 2);
    CHECK_EQ_INT(start, 98);
    CHECK_EQ_INT(r.read_total, 100);
    /* Never reads the slot written last: whatever was written, the take ends
     * at least DS_RING_HOLD before it. */
    {
        uint32_t w;
        for (w = 100; w < 100 + 3 * N; w += 37) {
            size_t n = ds_ring_take(&r, ds_ring_hold(&r, w), N, &start, &lost);
            CHECK(n == 0 || w - r.read_total >= DS_RING_HOLD);
            CHECK_EQ_INT(n & 1u, 0);
            CHECK_EQ_INT(r.read_total & 1u, 0);
            CHECK_EQ_INT(lost, 0);
        }
    }
    /* Across the 32-bit wrap. */
    r.read_total = 0xFFFFFFFEu;
    CHECK_EQ_INT(ds_ring_hold(&r, 0xFFFFFFFFu), 0xFFFFFFFEu);
    CHECK_EQ_INT(ds_ring_hold(&r, 0x00000004u), 0x00000002u);
    CHECK_EQ_INT(ds_ring_take(&r, ds_ring_hold(&r, 0x4u), N, &start, &lost), 4);
    CHECK_EQ_INT(lost, 0);
    /* A stall still counts as an overflow. */
    r.read_total = 0;
    CHECK(ds_ring_take(&r, ds_ring_hold(&r, N + 100), N, &start, &lost) <= N - 64);
    CHECK(lost > 0);
    CHECK_EQ_INT(start & 1u, 0);
    CHECK_EQ_INT(r.read_total, N + 98);
}

#define RELOAD 0x80000000u

/* The DMA's count as it runs: from RELOAD down to 0, then back to RELOAD. */
static uint32_t count_after(uint64_t transfers) {
    return RELOAD - (uint32_t)(transfers % RELOAD);
}

static void test_dma_total_across_reloads(void) {
    ds_dma_total t;
    uint64_t w;
    uint32_t prev = 0, total;

    ds_dma_total_init(&t, RELOAD);
    CHECK_EQ_INT(ds_dma_total_update(&t, RELOAD), 0);

    /* Walk across three reloads in uneven steps, polling at least once per
     * cycle: the total is exactly the number of transfers (mod 2^32), and
     * the ring index and parity follow. */
    for (w = 0; w < 3ull * RELOAD + 1000; w += 0x3FFFFF37ull) {
        total = ds_dma_total_update(&t, count_after(w));
        CHECK_EQ_INT(total, (uint32_t)w);
        CHECK_EQ_INT(total % N, w % N);
        CHECK_EQ_INT(total & 1u, w & 1u);
        CHECK(total - prev < RELOAD || w == 0);
        prev = total;
    }

    /* A reload between two reads: just before the end of a cycle (count 1),
     * then just after it. */
    ds_dma_total_init(&t, RELOAD);
    CHECK_EQ_INT(ds_dma_total_update(&t, 1), RELOAD - 1);
    CHECK_EQ_INT(ds_dma_total_update(&t, 0), RELOAD);          /* all done, not yet reloaded */
    CHECK_EQ_INT(ds_dma_total_update(&t, RELOAD), RELOAD);     /* reloaded: same total */
    CHECK_EQ_INT(ds_dma_total_update(&t, RELOAD - 10), RELOAD + 10);
    /* Read once before the reload and once well after it. */
    ds_dma_total_init(&t, RELOAD);
    CHECK_EQ_INT(ds_dma_total_update(&t, 5), RELOAD - 5);
    CHECK_EQ_INT(ds_dma_total_update(&t, RELOAD - 100), RELOAD + 100);
}

TEST_MAIN(test_no_loss, test_wraps_the_ring_index, test_overflow_is_detected,
          test_counter_wraps_32_bits, test_intact_after_reading,
          test_dma_total_across_reloads, test_hold_back_newest_pair)
