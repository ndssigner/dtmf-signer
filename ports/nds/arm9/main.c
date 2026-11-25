/* DTMF-Signer on the Nintendo DS / DS Lite.
 *
 * The device logic is ds_app (app/); this file is the console around it:
 *  - the tones arrive by air: the microphone (mic.c) records while the app
 *    listens, and the loop hands its samples to ds_app;
 *  - the keys (A, B, the D-pad) are read once per frame and go through the
 *    same ds_btn debouncer as the Pico's buttons;
 *  - the bottom screen shows the app's 128x64 screen doubled, the top one
 *    the current QR code large (black when there is none);
 *  - no Wi-Fi, no storage: the ARM7 (arm7/main.c) runs neither.
 * The echo has nowhere to go on a DS and is dropped; the AUTOTEST build
 * (make AUTOTEST=1, never released) prints it to the emulator's debug
 * output instead. */
#include <nds.h>
#include <string.h>

#include "ds_app.h"
#include "ds_btn.h"
#include "ds_core.h"
#include "ds_session.h"
#include "mic.h"
#include "nds_view.h"
#ifdef DS_NDS_AUTOTEST
#include "autotest.h"
#endif

#define AUDIO_CHUNK 512

#define PX(r, g, b) (RGB15(r, g, b) | BIT(15)) /* bit 15: the pixel is shown */
#define BLACK PX(0, 0, 0)
#define WHITE PX(31, 31, 31)

/* Big and long-lived: never on the stack (the stack is in the small DTCM). */
static ds_app app;
static ds_btn btn;
static uint8_t fb[8][128];
static uint16_t bottom[NDS_W * NDS_H] __attribute__((aligned(32)));
static uint16_t top[NDS_W * NDS_H] __attribute__((aligned(32)));
static int16_t pcm[AUDIO_CHUNK];
static char echo[DS_APP_ECHO_MAX];
static u16 *vram_top, *vram_bottom;

/* The clock: hardware timers 0 and 1 cascaded, counting BUS_CLOCK / 1024
 * (about 32.7 kHz) in 32 bits; now_ms (called every frame, far more often
 * than the 36 hours they take to wrap) extends them to 64. */
static void clock_start(void)
{
    TIMER_CR(0) = 0;
    TIMER_CR(1) = 0;
    TIMER_DATA(0) = 0;
    TIMER_DATA(1) = 0;
    TIMER_CR(1) = TIMER_ENABLE | TIMER_CASCADE;
    TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1024;
}

/* Milliseconds since clock_start, wrapping modulo 2^32 as the app expects. */
static uint32_t now_ms(void)
{
    static uint32_t last;
    static uint64_t wraps;
    uint16_t hi, lo;
    uint32_t t;

    do { /* the low half may wrap between the two reads */
        hi = TIMER_DATA(1);
        lo = TIMER_DATA(0);
    } while (hi != TIMER_DATA(1));
    t = (uint32_t)hi << 16 | lo;
    if (t < last) {
        wraps += (uint64_t)1 << 32;
    }
    last = t;
    return (uint32_t)((wraps + t) * 1024u * 1000u / BUS_CLOCK);
}

static void video_init(void)
{
    int bg;

    /* Main engine on the top screen, sub engine on the bottom one, each a
     * 16-bit bitmap background (256x256, of which 192 rows are seen). */
    irqEnable(IRQ_VBLANK); /* swiWaitForVBlank waits for it */
    lcdMainOnTop();
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vram_top = bgGetGfxPtr(bg);
    bg = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vram_bottom = bgGetGfxPtr(bg);
}

static void show(const uint16_t *buf, u16 *vram)
{
    DC_FlushRange(buf, NDS_W * NDS_H * sizeof buf[0]);
    dmaCopy(buf, vram, NDS_W * NDS_H * sizeof buf[0]);
}

static void draw(void)
{
    size_t i;

    ds_app_draw(&app, fb);
    nds_view_scale((const uint8_t(*)[128])fb, bottom, WHITE, BLACK);
#ifdef DS_NDS_AUTOTEST
    nds_autotest_overlay(bottom, WHITE);
#endif
    show(bottom, vram_bottom);
    if (!app.screen.qr || nds_view_qr(&app.qr, top, BLACK, WHITE) == 0) {
        for (i = 0; i < NDS_W * NDS_H; i++) {
            top[i] = BLACK;
        }
    }
    show(top, vram_top);
}

/* Hands what the microphone heard to the app while it listens, and turns
 * the microphone on and off with that. */
static void pump_audio(int *mic_on)
{
    int listening = ds_app_listening(&app);

    if (listening && !*mic_on) {
        *mic_on = nds_mic_start();
#ifdef DS_NDS_AUTOTEST
        if (*mic_on) {
            nds_autotest_mic_start(now_ms());
        }
#endif
    } else if (!listening && *mic_on) {
        nds_mic_stop_wipe();
        ds_wipe(pcm, sizeof pcm);
        *mic_on = 0;
    }
    if (!*mic_on) {
        return;
    }
    for (;;) {
        uint32_t lost;
#ifdef DS_NDS_AUTOTEST
        /* the test's tones instead of what the microphone hears (it runs,
         * unread: its ring just counts the overflow) */
        size_t n = nds_autotest_mic(pcm, AUDIO_CHUNK, now_ms());
        lost = 0;
#else
        size_t n = nds_mic_read(pcm, AUDIO_CHUNK, &lost);
#endif

        if (n > 0) {
            ds_app_audio(&app, pcm, n);
        }
        if (lost > 0) {
            ds_app_audio_lost(&app, lost);
        }
        if (n < AUDIO_CHUNK) {
            break;
        }
    }
}

/* Everything that may have held a secret or shown one, to zero: the
 * microphone's buffers, the app, the screens' buffers and their VRAM. */
static void scrub(void)
{
    nds_mic_stop_wipe();
    ds_wipe(&app, sizeof app);
    ds_wipe(pcm, sizeof pcm);
    ds_wipe(fb, sizeof fb);
    ds_wipe(bottom, sizeof bottom);
    ds_wipe(top, sizeof top);
    show(bottom, vram_bottom);
    show(top, vram_top);
}

/* Zero everything and turn the console off; never returns. */
static void wipe_and_off(void)
{
    scrub();
    systemShutDown();
    for (;;) {
        swiWaitForVBlank();
    }
}

int main(void)
{
    int mic_on = 0, first = 1;

    video_init();
    clock_start();
    ds_btn_init(&btn);
    ds_app_init(&app, NDS_MIC_RATE);
    ds_app_set_channel(&app, DS_AIR);
#ifdef DS_NDS_AUTOTEST
    nds_autotest_init(&app, NDS_MIC_RATE);
#endif

    while (pmMainLoop()) {
        uint32_t now;
        int kind = 0, b;

        swiWaitForVBlank();

        /* 1: the samples heard since the last frame. */
        pump_audio(&mic_on);

        /* 2: the keys, once per frame (16.7 ms: within ds_btn's 30 ms
         * debounce, and far inside the 200 ms that ds_app.h allows). */
        now = now_ms();
        scanKeys();
        /* A sleeping DS keeps its RAM, seed included, for days on battery:
         * the ARM7 never lets it sleep, and closing the lid is an
         * emergency wipe instead (as NDS-Signer does). */
        if (keysHeld() & KEY_LID) {
            wipe_and_off();
        }
#ifdef DS_NDS_AUTOTEST
        b = ds_btn_feed(&btn, nds_view_button(nds_autotest_keys(now)), now, &kind);
#else
        b = ds_btn_feed(&btn, nds_view_button(keysHeld()), now, &kind);
#endif
        if (b != DS_BTN_NONE) {
            ds_app_button(&app, b, kind);
        }

        /* 3: the clock; the heavy work the app was asked for runs here. */
        ds_app_tick(&app, now_ms());

        /* 4: draw what changed now, before the next tick (ds_app.h). */
#ifdef DS_NDS_AUTOTEST
        if (nds_autotest_take_redraw()) {
            first = 1;
        }
#endif
        if (ds_app_take_dirty(&app) || first) {
            first = 0;
            draw();
        }

        /* 5: the echo (public data only). */
        while (ds_app_take_echo(&app, echo, sizeof echo) > 0) {
#ifdef DS_NDS_AUTOTEST
            nocashWrite(echo, strlen(echo));
            nocashWrite("\n", 1);
            nds_autotest_echo(echo);
#endif
        }

        /* 6: a wipe: zero what is left and turn the console off. */
        if (ds_app_wants_reboot(&app)) {
            wipe_and_off();
        }
    }
    /* The console is being turned off or reset (power button). */
    scrub();
    return 0;
}
