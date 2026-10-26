/* DTMF-Signer on the Raspberry Pi Pico (RP2040).
 *
 * The device logic is ds_app (app/); this file is the hardware around it:
 *  - the ADC runs round-robin over the line-in (GP27) and the button ladder
 *    (GP28), 32 kHz in all, and a DMA channel writes every sample into a
 *    ring: even positions of the stream are the line, odd ones the buttons;
 *  - the loop drains the ring, feeds ds_app, and draws and echoes what it
 *    says;
 *  - the USB serial is output only: the echo is printed, nothing is read.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "adc_ring.h"
#include "board.h"
#include "display.h"
#include "ds_app.h"
#include "ds_btn.h"
#include "ds_core.h"

/* Samples of the line handed to ds_app_audio at once. */
#define AUDIO_CHUNK 256
#define STACK_REPORT_MS 5000
#define STACK_PAINT 0xC5C5C5C5u

/* The ring, aligned to its size so the DMA can wrap its write address. */
static volatile uint16_t ring[DS_RING_SAMPLES] __attribute__((aligned(1u << DS_RING_BYTES_LOG2)));
static int dma_chan, dma_reload_chan;
static uint32_t dma_count_reload = DS_DMA_RELOAD;
static ds_dma_total dma_total;

/* Scratch for pump_samples: the chunk as copied from the ring (line, ladder
 * pairs) and as handed to the app. Static, and wiped before a reboot. */
static uint16_t raw[2 * AUDIO_CHUNK];
static int16_t block[AUDIO_CHUNK];
static ds_ring ring_state;

/* Big and long-lived: never on the stack. */
static ds_app app;
static ds_btn btn;
static uint8_t fb[8][128];

/* The ladder's decimator: counts the ladder samples since the start (one per
 * millisecond once decimated) and where we are in the 1:16. */
static uint32_t ladder_ms;
static uint32_t ladder_phase;

/* The stack, from its low end (__StackBottom) to its top (__StackTop): the
 * whole 8 KB region of both scratch banks (CMakeLists.txt), so the watermark
 * sees all of it. Core 1 is never started (it would need a stack there). -- */
extern char __StackBottom[];
extern char __StackTop[];

/* Paints the unused part of the stack, so stack_free can tell later how deep
 * it went. Only below the stack pointer (a margin keeps clear of this very
 * function's frame). */
static void stack_paint(void)
{
    uint32_t *p = (uint32_t *)__StackBottom;
    uint32_t *sp;

    __asm volatile("mov %0, sp" : "=r"(sp));
    for (; p + 16 < sp; p++) {
        *p = STACK_PAINT;
    }
}

/* Bytes at the low end of the stack that were never touched. */
static uint32_t stack_free(void)
{
    const uint32_t *p = (const uint32_t *)__StackBottom;
    const uint32_t *end = (const uint32_t *)__StackTop;
    uint32_t n = 0;

    while (p + n < end && p[n] == STACK_PAINT) {
        n++;
    }
    return n * 4u;
}

/* The ADC and its DMA ring. ---------------------------------------------- */
static void adc_dma_start(void)
{
    dma_channel_config c;

    adc_init();
    adc_gpio_init(DS_PIN_LINE);
    adc_gpio_init(DS_PIN_LADDER);
    adc_select_input(DS_ADC_LINE);  /* the round-robin starts here ... */
    adc_set_round_robin((1u << DS_ADC_LINE) | (1u << DS_ADC_LADDER));
    adc_fifo_setup(true, true, 1, false, false); /* FIFO, DREQ at 1 sample, 12 bits */
    /* 48 MHz ADC clock: one conversion every 48e6 / 32000 cycles. */
    adc_set_clkdiv(48000000.0f / DS_ADC_RATE_TOTAL - 1);

    dma_chan = dma_claim_unused_channel(true);
    c = dma_channel_get_default_config((uint)dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, DS_RING_BYTES_LOG2); /* wrap the write address */
    channel_config_set_dreq(&c, DREQ_ADC);
    /* The RP2040 has no endless mode and a channel cannot chain to itself, so
     * the count (DS_DMA_RELOAD, even and a multiple of the ring size) is
     * renewed by a second channel: when the first finishes it chains to the
     * second, which copies the full count into the first's trigger register.
     * The first restarts at once, its write address carrying on in the ring;
     * the ADC's FIFO holds the few samples that arrive meanwhile. The reloads
     * are counted in software (ds_dma_total) so the written total keeps
     * growing without a step. */
    dma_reload_chan = dma_claim_unused_channel(true);
    {
        dma_channel_config r = dma_channel_get_default_config((uint)dma_reload_chan);

        channel_config_set_transfer_data_size(&r, DMA_SIZE_32);
        channel_config_set_read_increment(&r, false);
        channel_config_set_write_increment(&r, false);
        dma_channel_configure((uint)dma_reload_chan, &r,
                              &dma_hw->ch[dma_chan].al1_transfer_count_trig,
                              &dma_count_reload, 1, false);
    }
    channel_config_set_chain_to(&c, (uint)dma_reload_chan);
    ds_dma_total_init(&dma_total, DS_DMA_RELOAD);
    dma_channel_configure((uint)dma_chan, &c, ring, &adc_hw->fifo, DS_DMA_RELOAD, true);
    adc_run(true);
}

static uint32_t ring_written_total(void)
{
    return ds_dma_total_update(&dma_total, dma_hw->ch[dma_chan].transfer_count);
}

/* Which button the ladder reading (12 bits) is: DS_BTN_NONE to DS_BTN_RIGHT. */
static int ladder_class(uint16_t raw)
{
    static const uint16_t limits[] = DS_LADDER_LIMITS;
    int i;

    for (i = 0; i < 5; i++) {
        if (raw < limits[i]) {
            return i + 1;
        }
    }
    return DS_BTN_NONE;
}

/* Forgets everything about the buttons: after a gap in the samples no event
 * may come from a press that began before it. */
static void ladder_reset(void)
{
    ds_btn_init(&btn);
    ladder_phase = 0;
}

/* Hands the new samples to ds_app: the line in chunks, the ladder one in 16
 * to ds_btn, whose events go to ds_app_button at once.
 *
 * The DMA keeps writing while we work, so nothing read from the ring is
 * trusted until it is checked: each chunk is copied out, then ds_ring_intact
 * says whether the DMA has begun to overwrite what was copied. If it has, or
 * if the ring had already overflowed, the samples are given up as lost
 * (ds_app_audio_lost, which may itself take long, so a fresh look at the
 * ring follows) and the buttons restart from zero, so no event is made of
 * samples from both sides of a gap. */
static void pump_samples(void)
{
    uint32_t start, lost, first_total;
    size_t n, i;
    int round;

    for (round = 0;; round++) {
        n = ds_ring_take(&ring_state, ring_written_total(), DS_RING_SAMPLES,
                         &start, &lost);
        if (lost == 0) {
            break;
        }
        ladder_reset();
        /* lost / 2 samples of the line. */
        ds_app_audio_lost(&app, lost / 2);
        if (round == 3) {
            /* Never spin here: the buttons (the wipe) must stay served. The
             * samples of this last take are kept, not dropped; they are
             * checked chunk by chunk below and given up as lost if the DMA
             * has caught up with them. */
            break;
        }
    }
    first_total = ring_state.read_total - (uint32_t)n;

    for (i = 0; i < n;) {
        size_t pairs = 0, k = 0, j;

        /* Copy out up to a chunk. */
        while (i + 2 * pairs < n && pairs < AUDIO_CHUNK) {
            uint32_t pos = (start + (uint32_t)(i + 2 * pairs)) % DS_RING_SAMPLES;

            raw[2 * pairs] = ring[pos] & 0x0FFF;
            raw[2 * pairs + 1] = ring[(pos + 1) % DS_RING_SAMPLES] & 0x0FFF;
            pairs++;
        }
        /* Still what the DMA wrote? Else give up this chunk and all that
         * follows it in this batch: a fresh look at the ring comes next. */
        if (!ds_ring_intact(ring_written_total(), first_total + (uint32_t)i,
                            DS_RING_SAMPLES)) {
            uint32_t gone = (uint32_t)(n - i);

            ladder_reset();
            ds_app_audio_lost(&app, gone / 2);
            break;
        }
        i += 2 * pairs;

        for (j = 0; j < pairs; j++) {
            block[k++] = (int16_t)(((int32_t)raw[2 * j] - 2048) * 16);
            if (++ladder_phase == DS_LADDER_DECIMATE) {
                int kind = 0;
                int b;

                ladder_phase = 0;
                /* The clock follows the samples, not the wall time, so the
                 * debounce and the hold times are exact. */
                b = ds_btn_feed(&btn, ladder_class(raw[2 * j + 1]), ladder_ms++,
                                &kind);
                if (b != DS_BTN_NONE) {
                    /* Deliver now: the audio before it first, so the app
                     * sees events and audio in the order they happened. */
                    if (k > 0) {
                        ds_app_audio(&app, block, k);
                        k = 0;
                    }
                    ds_app_button(&app, b, kind);
                }
            }
        }
        if (k > 0) {
            ds_app_audio(&app, block, k);
        }
    }
}

/* After a wipe the SRAM survives the watchdog reset: stop the DMA and zero
 * what held line audio. */
static void scrub_buffers(void)
{
    size_t i;

    adc_run(false);
    dma_channel_abort((uint)dma_chan);
    dma_channel_abort((uint)dma_reload_chan);
    for (i = 0; i < DS_RING_SAMPLES; i++) {
        ring[i] = 0;
    }
    ds_wipe(block, sizeof block);
    ds_wipe(raw, sizeof raw);
}

int main(void)
{
    static char line[DS_APP_ECHO_MAX];
    uint32_t now, next_report, last_contrast = 0x100;
    int first = 1;

    stack_paint();
    stdio_init_all();
    gpio_init(DS_PIN_LED);
    gpio_set_dir(DS_PIN_LED, GPIO_OUT);
    ds_display_init();
    ds_btn_init(&btn);
    ds_app_init(&app, DS_AUDIO_RATE);
    adc_dma_start();
    next_report = to_ms_since_boot(get_absolute_time()) + STACK_REPORT_MS;

    for (;;) {
        int32_t got;

        /* 1-3: the new samples: the line to the receiver, the ladder to the
         * debouncer, and the buttons' events to the app. Lost samples are
         * reported so the listening screen counts them. */
        pump_samples();

        /* 4: the clock. The heavy work the app was asked for runs here, after
         * the tick that put "Working..." on screen: that screen was drawn at
         * the end of the previous iteration. */
        now = to_ms_since_boot(get_absolute_time());
        ds_app_tick(&app, now);

        /* 5: draw what changed NOW, before the next tick. ds_app.h makes the
         * signing review depend on this: its arming delay counts from the
         * first tick after the screen was handed over, and a button event is
         * only as old as the ring lag (< 200 ms). The tick comes before the
         * draw in the loop but the draw always comes before the next tick;
         * nothing between the take_dirty and the flush ticks the app. */
        if (ds_app_take_dirty(&app) || first) {
            uint8_t c;

            first = 0;
            ds_app_draw(&app, fb);
            ds_display_flush((const uint8_t(*)[128])fb);
            c = ds_app_contrast(&app);
            if (c != last_contrast) {
                ds_display_contrast(c);
                last_contrast = c;
            }
        }

        /* 6: one echo line per iteration (public data only), so a slow
         * host never delays the audio for more than one line. */
        got = ds_app_take_echo(&app, line, sizeof line);
        if (got > 0) {
            printf("%s\n", line);
        }

        /* 7: a wipe asks for a reboot (everything is already zeroed). */
        if (ds_app_wants_reboot(&app)) {
            scrub_buffers();
            watchdog_reboot(0, 0, 0);
        }

        if ((int32_t)(now - next_report) >= 0) {
            next_report = now + STACK_REPORT_MS;
            printf("stack-free %" PRIu32 "\n", stack_free());
            gpio_put(DS_PIN_LED, !gpio_get(DS_PIN_LED));
        }
    }
}
