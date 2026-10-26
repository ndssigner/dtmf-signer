#include "adc_ring.h"

size_t ds_ring_take(ds_ring *r, uint32_t written_total, uint32_t ring_n,
                    uint32_t *start, uint32_t *lost)
{
    uint32_t avail = written_total - r->read_total; /* wraps correctly */
    uint32_t limit = ring_n - DS_RING_MARGIN;

    *lost = 0;
    if (avail > limit) {
        /* Keep the newest samples, beginning at an even position so the line
         * and button samples do not swap; one more is given up if needed. */
        uint32_t keep_from = written_total - limit;

        if (keep_from & 1u) {
            keep_from++;
        }
        *lost = keep_from - r->read_total;
        r->read_total = keep_from;
        avail = written_total - keep_from;
    }
    avail &= ~1u; /* an odd sample waits for its pair */
    *start = r->read_total % ring_n;
    r->read_total += avail;
    return avail;
}

uint32_t ds_ring_hold(const ds_ring *r, uint32_t written_total)
{
    uint32_t pending = written_total - r->read_total; /* wraps correctly */

    return pending < DS_RING_HOLD ? r->read_total : written_total - DS_RING_HOLD;
}

int ds_ring_intact(uint32_t written_total, uint32_t first_total, uint32_t ring_n)
{
    return (uint32_t)(written_total - first_total) <= ring_n - DS_RING_MARGIN;
}

void ds_dma_total_init(ds_dma_total *t, uint32_t reload)
{
    t->base = 0;
    t->reload = reload;
    t->last = reload;
}

uint32_t ds_dma_total_update(ds_dma_total *t, uint32_t count)
{
    if (count > t->last) { /* the count went back up: a reload happened */
        t->base += t->reload;
    }
    t->last = count;
    return t->base + (t->reload - count);
}
