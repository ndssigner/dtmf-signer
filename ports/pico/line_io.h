/* The voice firmware's line I/O, pure C with no SDK so the host tests can
 * run it: the hook read from the line's voltage, and the queue of samples
 * the PWM interrupt plays into the line. */
#ifndef DS_LINE_IO_H
#define DS_LINE_IO_H

#include <stdint.h>

/* The hook. The line's voltage reaches the ADC through a divider: high with
 * the handset down (the current source's open-circuit voltage), low with it
 * lifted (the telephone holds the line at a few volts). The readings are
 * averaged in groups of DS_HOOK_AVERAGE (1 ms at 16 kHz), which also
 * cancels the tones riding on the voltage, and compared with two
 * thresholds (hysteresis). The timing (debounce, hook flash) is ds_voice's. */
#define DS_HOOK_AVERAGE 16u

typedef struct {
    uint32_t sum, n;
    int off_hook; /* 1: off-hook; starts on-hook */
} ds_hook;

void ds_hook_init(ds_hook *h);

/* One 12-bit reading. Returns 1 when it completed an average (off_hook is
 * then up to date): off-hook once the average is under off_below, on-hook
 * once it is over on_above, unchanged in between. */
int ds_hook_feed(ds_hook *h, uint16_t raw, uint16_t off_below, uint16_t on_above);

/* The output queue: the main loop pushes what ds_voice says, the PWM
 * interrupt pops one sample per period. Single producer, single consumer;
 * n must be a power of two. When it runs dry (the main loop is busy with a
 * long job) the line gets silence, and the underruns are counted. */
typedef struct {
    int16_t *buf;
    uint32_t n;
    volatile uint32_t w, r; /* totals pushed and popped (wrap at 32 bits) */
    volatile uint32_t underruns;
} ds_out;

void ds_out_init(ds_out *o, int16_t *buf, uint32_t n);

/* Samples waiting to be played. */
uint32_t ds_out_level(const ds_out *o);

/* How many to push to have lead waiting (0 when there are already that
 * many), never more than fit. */
uint32_t ds_out_room(const ds_out *o, uint32_t lead);

/* Appends k samples (k <= ds_out_room's answer). */
void ds_out_push(ds_out *o, const int16_t *x, uint32_t k);

/* The next sample, or 0 (silence) when none is waiting. */
int16_t ds_out_pop(ds_out *o);

/* The PWM level for a sample: a counter running 0..wrap, mid scale at 0. */
uint16_t ds_out_pwm(int16_t s, uint16_t wrap);

#endif
