/* The voice firmware (the "Centralita" telephone exchange) as WebAssembly:
 * the phone of the website's simulator.
 *
 * As in main.c, it is only what a board does: JavaScript hands it the hook
 * (off-hook or on-hook), the samples that arrive on the line, the clock and
 * the pack region (the page copies the language packs in before dvw_init),
 * and takes back the samples the signer puts on the line and the public
 * echo. Everything else is app/ds_voice, unchanged. All state is static,
 * with no malloc. */
#include <emscripten/emscripten.h>
#include <stdint.h>

#include "ds_about.h"
#include "ds_pack.h"
#include "ds_voice.h"

#define DVW_RATE DS_VOICE_RATE      /* 16 kHz, as on the line */
#define DVW_BUF_SAMPLES 8192u       /* 512 ms of line, in either direction */
#define DVW_REGION_LEN (1024u * 1024u)

static ds_voice voice;
static uint8_t region[DVW_REGION_LEN];
static int16_t in_buf[DVW_BUF_SAMPLES];
static int16_t out_buf[DVW_BUF_SAMPLES];
static uint32_t now_ms;
static char echo[DS_VOICE_ECHO_MAX];

/* The pack region: copy packs in (any number, as a flash image) and then
 * call dvw_init, which scans it. Empty (zeros) means English only. */
EMSCRIPTEN_KEEPALIVE uint8_t *dvw_region_buf(void) { return region; }
EMSCRIPTEN_KEEPALIVE uint32_t dvw_region_len(void) { return DVW_REGION_LEN; }

/* Power-up: zeroes everything (the region is kept), on-hook. */
EMSCRIPTEN_KEEPALIVE void dvw_init(void)
{
    ds_voice_init(&voice, DVW_RATE, region, DVW_REGION_LEN);
    now_ms = 0;
    echo[0] = '\0';
}

/* The hook as measured now (1: off-hook), at the firmware's clock. */
EMSCRIPTEN_KEEPALIVE void dvw_hook(int off_hook) { ds_voice_hook(&voice, off_hook, now_ms); }

/* ms milliseconds pass (the counter wraps like the board's). */
EMSCRIPTEN_KEEPALIVE void dvw_advance(uint32_t ms)
{
    now_ms += ms;
    ds_voice_tick(&voice, now_ms);
}

EMSCRIPTEN_KEEPALIVE int16_t *dvw_in_buf(void) { return in_buf; }

/* n samples of in_buf arrive on the line. */
EMSCRIPTEN_KEEPALIVE void dvw_in(int n)
{
    if (n > 0) {
        ds_voice_line_in(&voice, in_buf, (size_t)n > DVW_BUF_SAMPLES ? DVW_BUF_SAMPLES : (size_t)n);
    }
}

EMSCRIPTEN_KEEPALIVE int16_t *dvw_out_buf(void) { return out_buf; }

/* Fills out_buf with n samples of what the signer puts on the line
 * (silence when it has nothing to say); returns how many. */
EMSCRIPTEN_KEEPALIVE int dvw_out(int n)
{
    if (n <= 0) {
        return 0;
    }
    return (int)ds_voice_line_out(&voice, out_buf, (size_t)n > DVW_BUF_SAMPLES ? DVW_BUF_SAMPLES : (size_t)n);
}

/* The next echo line into dvw_echo(): its length, or 0 when there is none. */
EMSCRIPTEN_KEEPALIVE int dvw_take_echo(void)
{
    int32_t n = ds_voice_take_echo(&voice, echo, sizeof echo);
    if (n <= 0) {
        echo[0] = '\0';
        return 0;
    }
    return (int)n;
}

EMSCRIPTEN_KEEPALIVE const char *dvw_echo(void) { return echo; }

EMSCRIPTEN_KEEPALIVE uint32_t dvw_keys(void) { return ds_voice_keys(&voice); }

EMSCRIPTEN_KEEPALIVE const char *dvw_version(void) { return DS_VERSION; }
