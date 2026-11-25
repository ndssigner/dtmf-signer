/* Nintendo DS port: the emulator end-to-end test (development only).
 *
 * Built only with `make AUTOTEST=1` (DS_NDS_AUTOTEST; tools/build-nds.sh
 * --autotest), never in a released .nds. It stands in for the person and
 * the sender: the microphone's samples are tones synthesized at the mic
 * rate, and the keys are scripted from the echo's states and the screen:
 *
 *   Load seed -> OK on the air warning -> the test seed's tones (PIN 1234)
 *   -> PIN 1234 typed, hold OK -> OK on the seed fingerprint -> no
 *   passphrase -> OK on the wallet fingerprint -> Sign PSBT -> the PSBT
 *   vector's parts -> every review page, tap OK, hold OK -> signed.
 *
 * The signed PSBT from the echo is checked against the host port's (its
 * SHA-256 and length, from build/gen/autotest_vector.h), and so is the
 * message the QR on top carries. The verdict goes to the emulator's debug
 * output ("AUTOTEST PASS <sha256>" or "AUTOTEST FAIL <step>", what
 * tools/dev/nds_e2e.sh reads) and to the bottom screen's last rows (for a
 * person); the signed QR stays on top. */
#ifndef NDS_AUTOTEST_H
#define NDS_AUTOTEST_H

#include <stddef.h>
#include <stdint.h>

#include "ds_app.h"

/* Once, after ds_app_init. rate is the microphone's sample rate. */
void nds_autotest_init(ds_app *app, uint32_t rate);

/* The keys held now (NDS_KEY_* bits), instead of the real keys. */
uint32_t nds_autotest_keys(uint32_t now);

/* The app started listening: the "microphone" starts at now. */
void nds_autotest_mic_start(uint32_t now);

/* The samples "heard" since the last call, up to cap, at the mic rate (as
 * nds_mic_read gives them; nothing is ever lost). */
size_t nds_autotest_mic(int16_t *out, size_t cap, uint32_t now);

/* Each echo line the app gives. */
void nds_autotest_echo(const char *line);

/* 1 once when the verdict or the timings changed: draw again. */
int nds_autotest_take_redraw(void);

/* The timings on the bottom screen's top band and the verdict on its last
 * rows (both outside the app's doubled 128x64 screen). */
void nds_autotest_overlay(uint16_t *bottom, uint16_t on);

#endif
