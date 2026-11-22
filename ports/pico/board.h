/* The prototype's wiring and the analog front end. The only place the pins
 * are named. */
#ifndef DS_BOARD_H
#define DS_BOARD_H

#include <stdint.h>

/* OLED on SPI0 (SH1106; the SSD1306 SPI variant uses the same pins). */
#define DS_PIN_OLED_SCK 18
#define DS_PIN_OLED_MOSI 19
#define DS_PIN_OLED_CS 17
#define DS_PIN_OLED_DC 20
#define DS_PIN_OLED_RES 21
/* SH1106: its RAM has 132 columns and the 128 visible ones start at 2. */
#define DS_OLED_COL_OFFSET 2

/* SSD1306 over I2C0. */
#define DS_PIN_OLED_SDA 4
#define DS_PIN_OLED_SCL 5
#define DS_OLED_I2C_ADDR 0x3C

#define DS_PIN_LED 25

/* Inputs: the line-in on GP27 (ADC1) and the button ladder on GP28 (ADC2). */
#define DS_PIN_LINE 27
#define DS_PIN_LADDER 28
#define DS_ADC_LINE 1
#define DS_ADC_LADDER 2

/* The ADC runs round-robin over both inputs, starting with the line: a
 * 48 MHz clock divided by 32000 samples a second in all, 16000 for each. */
#define DS_ADC_RATE_TOTAL 32000u
#define DS_AUDIO_RATE 16000u
/* Odd samples (the ladder) are used one in this many: 1 kHz. */
#define DS_LADDER_DECIMATE 16u

/* The Phone Exchange (PEX, the voice firmware, DS_FIRMWARE=PEX) has no
 * buttons: GP28 reads the line's voltage instead, through a divider
 * (47 kOhm from the line, 10 kOhm to ground, 100 nF across the 10 k). The
 * current source holds an open line at about 11 V; a telephone off the
 * hook holds it at 6 to 8 V. */
#define DS_PIN_LINE_V 28
#define DS_ADC_LINE_V 2
#define DS_LINE_V_TOP 47u    /* kOhm */
#define DS_LINE_V_BOTTOM 10u /* kOhm */
/* A 12-bit reading of the line at mv millivolts (3.3 V reference). */
#define DS_LINE_V_RAW(mv) \
    ((uint16_t)((uint32_t)(mv) * DS_LINE_V_BOTTOM / (DS_LINE_V_TOP + DS_LINE_V_BOTTOM) * 4096u / 3300u))
#define DS_HOOK_OFF_MV 8000u /* off-hook under this */
#define DS_HOOK_ON_MV 9000u  /* on-hook over this */

/* Its voice goes into the line by PWM on GP15 (slice 7, channel B): a
 * 10-bit counter at the system clock (122 kHz), the duty following the
 * samples, smoothed by 1 kOhm and 10 nF and coupled into the line through
 * 10 uF. A second slice, with no pin, only paces the samples: it wraps
 * 16000 times a second and its interrupt sets the next duty. */
#define DS_PIN_AUDIO_OUT 15
#define DS_AUDIO_WRAP 1023u
#define DS_PACER_SLICE 0u
/* The main loop keeps this much of the voice queued ahead of the PWM. The
 * output delay ds_voice is built with (DS_VOICE_OUT_DELAY_MS, set in
 * CMakeLists.txt) is this plus one turn of the loop. */
#define DS_OUT_LEAD_MS 20u
#define DS_OUT_QUEUE 1024u /* samples, a power of two */

/* Its voice packs: a 1 MB region at flash offset 1 MB (written with
 * tools/voice/make_uf2.py); the firmware itself must fit in the first MB. */
#define DS_VOICE_REGION_OFFSET 0x100000u
#define DS_VOICE_REGION_LEN 0x100000u

/* The DMA ring: 4096 samples of 16 bits (8192 bytes, aligned to its size). */
#define DS_RING_SAMPLES 4096u
#define DS_RING_BYTES_LOG2 13

/* The ladder's classes: a reading under DS_LADDER_LIMIT[i] (12 bits) is
 * button i + 1 (LEFT, UP, OK, DOWN, RIGHT); at or over the last, none. */
#define DS_LADDER_LIMITS {555, 1023, 1678, 2432, 3455}

#endif
