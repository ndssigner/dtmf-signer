/* DTMF-Signer as WebAssembly: the board of the website's simulator.
 *
 * Like ports/pico, it reuses core/ and app/ unchanged and only does what a
 * board does: it feeds ds_app the line-in samples, the button class once a
 * millisecond (through the same ds_btn debouncer as the resistor ladder),
 * and the clock, and hands back the screen, the contrast and the echo.
 * JavaScript drives it through the plain C functions below; all state is
 * static, with no malloc. */
#include <emscripten/emscripten.h>
#include <stdint.h>

#include "ds_app.h"
#include "ds_btn.h"

#define DSW_RATE 16000u      /* the line-in rate, as DS_AUDIO_RATE on the Pico */
#define DSW_AUDIO_MAX 4096u  /* samples per dsw_audio call */

static ds_app app;
static ds_btn btn;
static uint32_t now_ms;
static int16_t audio[DSW_AUDIO_MAX];
static uint8_t fb[8][128];
static char echo[DS_APP_ECHO_MAX];

EMSCRIPTEN_KEEPALIVE void dsw_init(void)
{
    ds_app_init(&app, DSW_RATE);
    ds_btn_init(&btn);
    now_ms = 0;
    echo[0] = '\0';
}

EMSCRIPTEN_KEEPALIVE int16_t *dsw_audio_buf(void) { return audio; }

EMSCRIPTEN_KEEPALIVE void dsw_audio(int n)
{
    if (n > 0) {
        ds_app_audio(&app, audio, (size_t)n > DSW_AUDIO_MAX ? DSW_AUDIO_MAX : (size_t)n);
    }
}

/* ms milliseconds with the button class cls held (0 none, DS_BTN_LEFT..RIGHT):
 * the debouncer sees one sample a millisecond, as on the Pico. */
EMSCRIPTEN_KEEPALIVE void dsw_advance(uint32_t ms, int cls)
{
    uint32_t i;
    for (i = 0; i < ms; i++) {
        int kind = 0;
        int b;
        now_ms++;
        b = ds_btn_feed(&btn, cls, now_ms, &kind);
        if (b != DS_BTN_NONE) {
            ds_app_button(&app, b, kind);
        }
        ds_app_tick(&app, now_ms);
    }
}

EMSCRIPTEN_KEEPALIVE int dsw_take_dirty(void) { return ds_app_take_dirty(&app); }

EMSCRIPTEN_KEEPALIVE const uint8_t *dsw_draw(void)
{
    ds_app_draw(&app, fb);
    return &fb[0][0];
}

EMSCRIPTEN_KEEPALIVE int dsw_contrast(void) { return ds_app_contrast(&app); }

/* The text on the screen, row by row: what ds_app_draw draws. */
EMSCRIPTEN_KEEPALIVE int dsw_rows(void) { return DS_UI_ROWS; }

EMSCRIPTEN_KEEPALIVE const char *dsw_line(int r)
{
    return r >= 0 && r < DS_UI_ROWS ? app.screen.line[r] : "";
}

/* The character under the picker's cursor (on screen too); for the tests. */
EMSCRIPTEN_KEEPALIVE int dsw_picker_cell(void) { return (unsigned char)ds_picker_cell(&app.pick); }

/* The next echo line into dsw_echo(): its length, or 0 when there is none. */
EMSCRIPTEN_KEEPALIVE int dsw_take_echo(void)
{
    int32_t n = ds_app_take_echo(&app, echo, sizeof echo);
    if (n <= 0) {
        echo[0] = '\0';
        return 0;
    }
    return (int)n;
}

EMSCRIPTEN_KEEPALIVE const char *dsw_echo(void) { return echo; }

EMSCRIPTEN_KEEPALIVE uint32_t dsw_keys(void) { return ds_app_keys(&app); }

EMSCRIPTEN_KEEPALIVE int dsw_wants_reboot(void) { return ds_app_wants_reboot(&app); }

EMSCRIPTEN_KEEPALIVE const char *dsw_version(void) { return DS_VERSION; }
