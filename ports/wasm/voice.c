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
#include "ds_bitcoin.h"
#include "ds_core.h"
#include "ds_pack.h"
#include "ds_speex.h"
#include "ds_voice.h"
#include "speex_vector.h"

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

/* A test hook, not part of the phone: decodes the shared Speex vector
 * (vectors/speex-v0.json) and returns 1 when the PCM's SHA-256 is the one
 * the host test checks, so the simulator plays what the firmware plays. */
EMSCRIPTEN_KEEPALIVE int dvw_speex_selftest(void)
{
    static ds_speex d;
    static const char hex[] = "0123456789abcdef";
    int16_t pcm[DS_SPEEX_FRAME];
    uint8_t le[2 * DS_SPEEX_FRAME];
    uint8_t digest[32];
    ds_sha256_ctx h;

    if (ds_speex_init(&d) != DS_OK) {
        return 0;
    }
    ds_sha256_init(&h);
    for (uint32_t f = 0; f < SPEEX_VECTOR_FRAMES; f++) {
        uint16_t at = speex_vector_offsets[f];
        uint8_t n = (uint8_t)(speex_vector_offsets[f + 1] - at);
        if (ds_speex_frame(&d, speex_vector_bytes + at, n, pcm) != DS_OK) {
            return 0;
        }
        for (uint32_t i = 0; i < DS_SPEEX_FRAME; i++) {
            le[2 * i] = (uint8_t)((uint16_t)pcm[i] & 0xffu);
            le[2 * i + 1] = (uint8_t)((uint16_t)pcm[i] >> 8);
        }
        ds_sha256_update(&h, le, sizeof le);
    }
    ds_sha256_final(&h, digest);
    for (uint32_t i = 0; i < 32; i++) {
        if (SPEEX_VECTOR_PCM_SHA256[2 * i] != hex[digest[i] >> 4] ||
            SPEEX_VECTOR_PCM_SHA256[2 * i + 1] != hex[digest[i] & 15]) {
            return 0;
        }
    }
    return 1;
}
