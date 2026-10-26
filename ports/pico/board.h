/* The prototype's wiring and the analog front end. The only place the pins
 * are named. */
#ifndef DS_BOARD_H
#define DS_BOARD_H

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

/* The DMA ring: 4096 samples of 16 bits (8192 bytes, aligned to its size). */
#define DS_RING_SAMPLES 4096u
#define DS_RING_BYTES_LOG2 13

/* The ladder's classes: a reading under DS_LADDER_LIMIT[i] (12 bits) is
 * button i + 1 (LEFT, UP, OK, DOWN, RIGHT); at or over the last, none. */
#define DS_LADDER_LIMITS {555, 1023, 1678, 2432, 3455}

#endif
