/* DTMF-Signer "Centralita" on the Raspberry Pi Pico (RP2040): a tone
 * telephone as the whole interface (DS_FIRMWARE=CENTRALITA).
 *
 * The device logic is ds_voice (app/); this file is the hardware around it:
 *  - the ADC runs round-robin over the line-in (GP27) and the line's voltage
 *    (GP28), 32 kHz in all, into the DMA ring main.c uses: even positions of
 *    the stream are the line, odd ones its voltage;
 *  - the loop drains the ring: the line to ds_voice_line_in, the voltage to
 *    ds_hook (1 ms averages), whose changes go to ds_voice_hook;
 *  - the voice goes out by PWM on GP15: the loop keeps DS_OUT_LEAD_MS of
 *    ds_voice_line_out queued (ds_outq), and an interrupt 16000 times a
 *    second plays the next sample, silence when the queue is dry;
 *  - while ds_voice does heavy work (seconds: nothing else runs), the
 *    interrupt loops the wait motif on its own, and stops it at its next
 *    rest when the work is done;
 *  - the clock follows the samples, lost ones included, so the hook's
 *    timing and the help are counted on the line's own time;
 *  - the USB serial is output only: the echo is printed, nothing is read.
 *
 * The voice packs other than the built-in English one live in a 1 MB flash
 * region of their own (board.h), written apart from the firmware. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/regs/addressmap.h"
#include "pico/stdlib.h"

#include "adc_ring.h"
#include "board.h"
#include "ds_core.h"
#include "ds_pack.h"
#include "ds_prompts.h"
#include "ds_say.h"
#include "ds_voice.h"
#include "line_io.h"

/* Samples of the line handed to ds_voice at once. */
#define AUDIO_CHUNK 256
#define REPORT_MS 5000
#define WORK_REPORT_US 50000u /* a tick longer than this is reported */
#define STACK_PAINT 0xC5C5C5C5u
#define OUT_LEAD (DS_AUDIO_RATE / 1000u * DS_OUT_LEAD_MS)

/* The ring, aligned to its size so the DMA can wrap its write address. */
static volatile uint16_t ring[DS_RING_SAMPLES] __attribute__((aligned(1u << DS_RING_BYTES_LOG2)));
static int dma_chan, dma_reload_chan;
static uint32_t dma_count_reload = DS_DMA_RELOAD;
static ds_dma_total dma_total;
static ds_ring ring_state;

/* Scratch for pump_samples: the chunk as copied from the ring (line,
 * voltage pairs) and as handed to ds_voice. */
static uint16_t raw[2 * AUDIO_CHUNK];
static int16_t block[AUDIO_CHUNK];

/* Big and long-lived: never on the stack. */
static ds_voice voice;
static ds_hook hook;
static int16_t out_buf[DS_OUT_QUEUE];
static ds_outq out;
static int16_t out_chunk[OUT_LEAD];
static uint audio_slice, audio_chan;

/* The wait motif as PWM levels, looped by the interrupt while ds_voice
 * works: a rest, then the motif (ds_say renders it, as ds_voice plays it). */
#define WAIT_REST (DS_AUDIO_RATE * 270u / 1000u)
#define WAIT_LEN (DS_AUDIO_RATE * 600u / 1000u)
static uint16_t wait_loop[WAIT_LEN];
static volatile int wait_on;   /* the main loop: 1 while the work runs */
static int wait_playing;       /* the interrupt's: the loop is playing */
static uint32_t wait_pos;

#ifdef DS_LINE_DEBUG
/* A bench aid, off in releases (-DDS_LINE_DEBUG=1): once a second, the
 * line's peak-to-peak level and what the receiver has heard, so a
 * reception can be followed as it happens. Public data only: the keys of a
 * seed are never shown (rx.live holds a PSBT's only). */
static uint16_t dbg_min = 4095, dbg_max;
#endif

/* Set when the hook changes, for the echo. */
static int hook_changed;

/* The line's clock: samples of the line since the start, heard or lost. */
static uint32_t line_samples;

static uint32_t line_ms(void)
{
    return line_samples / (DS_AUDIO_RATE / 1000u);
}

/* The stack (as main.c: both scratch banks, core 1 never started). ------ */
extern char __StackBottom[];
extern char __StackTop[];

static void stack_paint(void)
{
    uint32_t *p = (uint32_t *)__StackBottom;
    uint32_t *sp;

    __asm volatile("mov %0, sp" : "=r"(sp));
    for (; p + 16 < sp; p++) {
        *p = STACK_PAINT;
    }
}

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

/* The ADC and its DMA ring, as in main.c (see there for the reload). ----- */
static void adc_dma_start(void)
{
    dma_channel_config c;

    adc_init();
    adc_gpio_init(DS_PIN_LINE);
    adc_gpio_init(DS_PIN_LINE_V);
    adc_select_input(DS_ADC_LINE);
    adc_set_round_robin((1u << DS_ADC_LINE) | (1u << DS_ADC_LINE_V));
    adc_fifo_setup(true, true, 1, false, false);
    adc_set_clkdiv(48000000.0f / DS_ADC_RATE_TOTAL - 1);

    dma_chan = dma_claim_unused_channel(true);
    c = dma_channel_get_default_config((uint)dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, DS_RING_BYTES_LOG2);
    channel_config_set_dreq(&c, DREQ_ADC);
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

/* The output: PWM on GP15, paced by a second slice. --------------------- */
static void __not_in_flash_func(on_pacer)(void)
{
    pwm_clear_irq(DS_PACER_SLICE);
    /* The wait loop starts once what was queued (the motif's own end) has
     * played, and stops only in its rest, so no note is cut. */
    if (!wait_playing && wait_on && ds_outq_level(&out) == 0) {
        wait_playing = 1;
        wait_pos = 0;
    }
    if (wait_playing && !wait_on && wait_pos < WAIT_REST) {
        wait_playing = 0;
    }
    if (wait_playing) {
        pwm_set_chan_level(audio_slice, audio_chan, wait_loop[wait_pos]);
        wait_pos = wait_pos + 1 == WAIT_LEN ? 0 : wait_pos + 1;
        return;
    }
    pwm_set_chan_level(audio_slice, audio_chan, ds_outq_pwm(ds_outq_pop(&out), DS_AUDIO_WRAP));
}

/* Renders the wait loop: the rest, then the motif. */
static void wait_loop_init(void)
{
    static ds_say s;
    int16_t *x = (int16_t *)wait_loop; /* rendered in place, then turned into levels */
    ds_pack en;
    size_t n = WAIT_REST;

    memset(wait_loop, 0, sizeof wait_loop);
    if (ds_pack_open(&en, ds_pack_en, ds_pack_en_len) == DS_OK) {
        ds_say_init(&s, &en);
        ds_say_earcon(&s, DS_EAR_WAIT);
        while (ds_say_busy(&s) && n < WAIT_LEN) {
            n += ds_say_read(&s, x + n, WAIT_LEN - n);
        }
    }
    for (n = 0; n < WAIT_LEN; n++) {
        wait_loop[n] = ds_outq_pwm(x[n], DS_AUDIO_WRAP); /* same slot, read then written */
    }
}

static void audio_out_start(void)
{
    pwm_config c;

    ds_outq_init(&out, out_buf, DS_OUT_QUEUE);
    wait_loop_init();
    gpio_set_function(DS_PIN_AUDIO_OUT, GPIO_FUNC_PWM);
    audio_slice = pwm_gpio_to_slice_num(DS_PIN_AUDIO_OUT);
    audio_chan = pwm_gpio_to_channel(DS_PIN_AUDIO_OUT);
    c = pwm_get_default_config();
    pwm_config_set_wrap(&c, DS_AUDIO_WRAP);
    pwm_init(audio_slice, &c, false);
    pwm_set_chan_level(audio_slice, audio_chan, ds_outq_pwm(0, DS_AUDIO_WRAP));
    pwm_set_enabled(audio_slice, true);

    /* The pacer: 125 MHz / 2.5 / 3125 = 16000 wraps a second exactly. */
    c = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac(&c, 2, 8);
    pwm_config_set_wrap(&c, 3124);
    pwm_init(DS_PACER_SLICE, &c, false);
    pwm_clear_irq(DS_PACER_SLICE);
    pwm_set_irq_enabled(DS_PACER_SLICE, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pacer);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    pwm_set_enabled(DS_PACER_SLICE, true);
}

/* Tops the output queue up to DS_OUT_LEAD_MS. */
static void feed_out(void)
{
    uint32_t k = ds_outq_room(&out, OUT_LEAD);

    if (k > 0) {
        ds_voice_line_out(&voice, out_chunk, k);
        ds_outq_push(&out, out_chunk, k);
    }
}

/* Hands the new samples to ds_voice: the line in chunks, the voltage to
 * the hook, whose changes go to ds_voice_hook at once (the line before
 * them first, so ds_voice sees both in the order they happened). As in
 * main.c, nothing read from the ring is trusted until ds_ring_intact says
 * the DMA has not begun to overwrite it; lost samples only move the clock
 * on (ds_voice has no use for them). */
static void pump_samples(void)
{
    uint32_t start, lost, first_total;
    size_t n, i;

    n = ds_ring_take(&ring_state, ds_ring_hold(&ring_state, ring_written_total()),
                     DS_RING_SAMPLES, &start, &lost);
    line_samples += lost / 2;
    first_total = ring_state.read_total - (uint32_t)n;

    for (i = 0; i < n;) {
        size_t pairs = 0, k = 0, j;

        while (i + 2 * pairs < n && pairs < AUDIO_CHUNK) {
            uint32_t pos = (start + (uint32_t)(i + 2 * pairs)) % DS_RING_SAMPLES;

            raw[2 * pairs] = ring[pos] & 0x0FFF;
            raw[2 * pairs + 1] = ring[(pos + 1) % DS_RING_SAMPLES] & 0x0FFF;
            pairs++;
        }
        if (!ds_ring_intact(ring_written_total(), first_total + (uint32_t)i,
                            DS_RING_SAMPLES)) {
            line_samples += (uint32_t)(n - i) / 2;
            break;
        }
        i += 2 * pairs;

        for (j = 0; j < pairs; j++) {
            int was = hook.off_hook;

            block[k++] = (int16_t)(((int32_t)raw[2 * j] - 2048) * 16);
#ifdef DS_LINE_DEBUG
            if (raw[2 * j] < dbg_min) dbg_min = raw[2 * j];
            if (raw[2 * j] > dbg_max) dbg_max = raw[2 * j];
#endif
            line_samples++;
            if (ds_hook_feed(&hook, raw[2 * j + 1], DS_LINE_V_RAW(DS_HOOK_OFF_MV),
                             DS_LINE_V_RAW(DS_HOOK_ON_MV)) &&
                hook.off_hook != was) {
                ds_voice_line_in(&voice, block, k);
                k = 0;
                ds_voice_hook(&voice, hook.off_hook, line_ms());
                hook_changed = 1;
            }
        }
        if (k > 0) {
            ds_voice_line_in(&voice, block, k);
        }
        /* The voice must not run dry while a long batch is heard. */
        feed_out();
    }
}

int main(void)
{
    static char line[DS_VOICE_ECHO_MAX];
    uint32_t next_report, loop_max = 0, last_us;

    stack_paint();
    stdio_init_all();
    gpio_init(DS_PIN_LED);
    gpio_set_dir(DS_PIN_LED, GPIO_OUT);
    gpio_put(DS_PIN_LED, 0);
    ds_hook_init(&hook);
    ds_voice_init(&voice, DS_AUDIO_RATE,
                  (const uint8_t *)(XIP_BASE + DS_VOICE_REGION_OFFSET), DS_VOICE_REGION_LEN);
    audio_out_start();
    adc_dma_start();
    next_report = REPORT_MS;
    last_us = time_us_32();

    for (;;) {
        uint32_t now, us;
        int32_t got;

        pump_samples();
        now = line_ms();
        us = time_us_32();
        wait_on = ds_voice_work_due(&voice); /* the interrupt loops the motif */
        ds_voice_tick(&voice, now);
        wait_on = 0;
        us = time_us_32() - us;
        if (us > WORK_REPORT_US) {
            /* heavy work ran in this tick (a PIN, a derivation, a signature) */
            printf("work-ms %" PRIu32 "\n", us / 1000u);
        }
        feed_out();

        /* The LED as ds_voice drives it: a reception's progress. */
        gpio_put(DS_PIN_LED, ds_voice_led(&voice));

        /* The hook as measured (the echo says what ds_voice made of it). */
        if (hook_changed) {
            hook_changed = 0;
            printf("hook %s\n", hook.off_hook ? "off" : "on");
        }

        /* One echo line per iteration (public data only). */
        got = ds_voice_take_echo(&voice, line, sizeof line);
        if (got > 0) {
            printf("%s\n", line);
        }

        /* The longest turn of the loop (DS_VOICE_OUT_DELAY_MS must cover
         * it), the voice's underruns, the stack's headroom. */
        us = time_us_32();
        if (us - last_us > loop_max) {
            loop_max = us - last_us;
        }
        last_us = us;
#ifdef DS_LINE_DEBUG
        {
            static uint32_t next_dbg;
            if ((int32_t)(now - next_dbg) >= 0) {
                next_dbg = now + 1000;
                printf("dbg pp %u keys %" PRIu32 " parts %" PRIu32 " live %s\n",
                       (unsigned)(dbg_max > dbg_min ? dbg_max - dbg_min : 0), voice.rx.keys,
                       voice.rx.parts, voice.rx.mode == DS_RX_PSBT ? voice.rx.live : "-");
                dbg_min = 4095;
                dbg_max = 0;
            }
        }
#endif
        if ((int32_t)(now - next_report) >= 0) {
            next_report = now + REPORT_MS;
            printf("stack-free %" PRIu32 " loop-max-us %" PRIu32 " underruns %" PRIu32 "\n",
                   stack_free(), loop_max, out.underruns);
            loop_max = 0;
            last_us = time_us_32();
        }
    }
}
