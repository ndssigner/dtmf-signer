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
 * written_total at that moment. A conservative bound: the DMA overwrites the
 * slot of the first sample only after ring_n more samples; this answers 1
 * only while fewer than ring_n - DS_RING_MARGIN have been written past it,
 * leaving the margin for the reading itself and the count's lag. Read a
 * batch, then ask; if 0, throw the batch away as lost. */
int ds_ring_intact(uint32_t written_total, uint32_t first_total, uint32_t ring_n);

/* The DMA channel counts its transfers down from DS_DMA_RELOAD to 0 and is
 * then reloaded with the same count (a second channel does it), so its
 * register alone does not say how many samples were written in all.
 * ds_dma_total keeps that total (mod 2^32), counting the reloads in software;
 * poll it at least once per cycle (2^31 samples, about 18 hours).
 * DS_DMA_RELOAD is even and a multiple of any ring size used, so the total
 * keeps the ring index and the line/button parity of the transfers. */
#define DS_DMA_RELOAD 0x80000000u

typedef struct {
    uint32_t base, reload, last;
} ds_dma_total;

void ds_dma_total_init(ds_dma_total *t, uint32_t reload);

/* The samples written so far, given the channel's current transfer count.
 * Equal to the transfers done; at the moment of a reload (count 0, then
 * reload) it does not move. */
uint32_t ds_dma_total_update(ds_dma_total *t, uint32_t count);

#endif
