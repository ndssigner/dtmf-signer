/* See line_io.h. */
#include "line_io.h"

/* Keeps the compiler from moving the buffer's stores past the index that
 * publishes them (the interrupt reads them on the same core). */
#define DS_BARRIER() __asm__ volatile("" ::: "memory")

void ds_hook_init(ds_hook *h)
{
    h->sum = 0;
    h->n = 0;
    h->off_hook = 0;
}

int ds_hook_feed(ds_hook *h, uint16_t raw, uint16_t off_below, uint16_t on_above)
{
    uint32_t avg;

    h->sum += raw;
    if (++h->n < DS_HOOK_AVERAGE) {
        return 0;
    }
    avg = h->sum / DS_HOOK_AVERAGE;
    h->sum = 0;
    h->n = 0;
    if (avg < off_below) {
        h->off_hook = 1;
    } else if (avg > on_above) {
        h->off_hook = 0;
    }
    return 1;
}

void ds_outq_init(ds_outq *o, int16_t *buf, uint32_t n)
{
    o->buf = buf;
    o->n = n;
    o->w = 0;
    o->r = 0;
    o->underruns = 0;
}

uint32_t ds_outq_level(const ds_outq *o)
{
    return o->w - o->r;
}

uint32_t ds_outq_room(const ds_outq *o, uint32_t lead)
{
    uint32_t level = ds_outq_level(o);

    if (lead > o->n) {
        lead = o->n;
    }
    return level >= lead ? 0 : lead - level;
}

void ds_outq_push(ds_outq *o, const int16_t *x, uint32_t k)
{
    uint32_t w = o->w;

    for (uint32_t i = 0; i < k; i++) {
        o->buf[(w + i) & (o->n - 1)] = x[i];
    }
    DS_BARRIER();
    o->w = w + k;
}

int16_t ds_outq_pop(ds_outq *o)
{
    uint32_t r = o->r;
    int16_t s;

    if (o->w == r) {
        o->underruns++;
        return 0;
    }
    DS_BARRIER();
    s = o->buf[r & (o->n - 1)];
    o->r = r + 1;
    return s;
}

uint16_t ds_outq_pwm(int16_t s, uint16_t wrap)
{
    return (uint16_t)(((uint32_t)((int32_t)s + 32768) * ((uint32_t)wrap + 1u)) >> 16);
}
