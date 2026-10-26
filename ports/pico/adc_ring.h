/* The bookkeeping of the ADC's DMA ring, pure arithmetic with no SDK so the
 * host tests can run it.
 *
 * The ADC runs round-robin over two inputs and the DMA writes every sample,
 * in order, into a ring of ring_n 16-bit samples (ring_n even): the samples
 * at even positions of the stream are the first input (the line), the odd
 * ones the second (the buttons). The consumer keeps read_total, the number
 * of samples it has taken since the start; the DMA's count of samples
 * written is written_total (0xFFFFFFFF minus the channel's transfer count).
 * Both wrap at 32 bits; only their difference is used. */
#ifndef DS_ADC_RING_H
#define DS_ADC_RING_H

#include <stddef.h>
#include <stdint.h>

/* Stays even: samples are taken in (line, buttons) pairs. */
typedef struct {
    uint32_t read_total;
} ds_ring;

/* Margin kept below the ring's size: the DMA may write this far ahead of
 * what the consumer is reading from while it reads. */
#define DS_RING_MARGIN 64

/* How many new samples there are, from which ring index (*start), taking
 * them in the ring's order (they may wrap past the end: the caller indexes
 * modulo ring_n). The count is even and *start is the ring index of an even
 * stream position. If more than ring_n - DS_RING_MARGIN samples are waiting,
 * the consumer was too slow: the oldest are given up as lost (*lost counts
 * them) and the newest ones, from an even position, are returned. Returns
 * the count and advances read_total past it. */
size_t ds_ring_take(ds_ring *r, uint32_t written_total, uint32_t ring_n,
                    uint32_t *start, uint32_t *lost);

/* 1 if samples read starting at stream position first_total are still what
 * the DMA wrote there, judged after reading them with the DMA's count
 * written_total at that moment: it has not written ring_n - DS_RING_MARGIN
 * samples past first_total, so it has not begun overwriting the slot of the
 * first one. Read a batch, then ask; if 0, throw the batch away as lost. */
int ds_ring_intact(uint32_t written_total, uint32_t first_total, uint32_t ring_n);

#endif
