/* SH1106 over SPI0, the controller of the prototype's 1.3" OLED. */
#include "display.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "board.h"

#define OLED_SPI spi0

static void cmd(const uint8_t *c, size_t n)
{
    gpio_put(DS_PIN_OLED_DC, 0);
    gpio_put(DS_PIN_OLED_CS, 0);
    spi_write_blocking(OLED_SPI, c, n);
    gpio_put(DS_PIN_OLED_CS, 1);
}

static void data(const uint8_t *d, size_t n)
{
    gpio_put(DS_PIN_OLED_DC, 1);
    gpio_put(DS_PIN_OLED_CS, 0);
    spi_write_blocking(OLED_SPI, d, n);
    gpio_put(DS_PIN_OLED_CS, 1);
}

void ds_display_init(void)
{
    static const uint8_t init[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock divide / oscillator */
        0xA8, 0x3F, /* multiplex: 64 rows */
        0xD3, 0x00, /* display offset */
        0x40,       /* start line 0 */
        0xAD, 0x8B, /* DC-DC (charge pump) on */
        0x32,       /* pump voltage 8.0 V */
#ifdef DS_DISPLAY_FLIP
        0xA0,       /* segment remap: rotated 180 degrees */
        0xC0,       /* COM scan bottom to top */
#else
        0xA1,       /* segment remap: column 0 on the left */
        0xC8,       /* COM scan top to bottom */
#endif
        0xDA, 0x12, /* COM pins configuration */
        0x81, 0x80, /* contrast */
        0xD9, 0x22, /* pre-charge period */
        0xDB, 0x35, /* VCOM deselect level */
        0xA4,       /* show RAM contents */
        0xA6,       /* normal, not inverted */
    };
    static const uint8_t on[] = {0xAF};
    static const uint8_t blank[8][128];
    const uint pins[] = {DS_PIN_OLED_CS, DS_PIN_OLED_DC, DS_PIN_OLED_RES};
    size_t i;

    spi_init(OLED_SPI, 4 * 1000 * 1000); /* 4 MHz, well within the SH1106 */
    gpio_set_function(DS_PIN_OLED_SCK, GPIO_FUNC_SPI);
    gpio_set_function(DS_PIN_OLED_MOSI, GPIO_FUNC_SPI);
    for (i = 0; i < 3; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 1);
    }
    gpio_put(DS_PIN_OLED_RES, 0); /* hardware reset */
    sleep_ms(10);
    gpio_put(DS_PIN_OLED_RES, 1);
    sleep_ms(10);

    cmd(init, sizeof init);
    sleep_ms(100); /* let the pump settle */
    /* The RAM holds noise after power-up: blank it before turning on. */
    ds_display_flush(blank);
    cmd(on, 1);
}

void ds_display_flush(const uint8_t fb[8][128])
{
    uint8_t p;

    for (p = 0; p < 8; p++) {
        const uint8_t addr[] = {
            (uint8_t)(0xB0 | p),                         /* page */
            (uint8_t)(0x00 | (DS_OLED_COL_OFFSET & 0x0F)), /* column, low */
            (uint8_t)(0x10 | (DS_OLED_COL_OFFSET >> 4)),   /* column, high */
        };
        cmd(addr, sizeof addr);
        data(fb[p], 128);
    }
}

void ds_display_contrast(uint8_t v)
{
    const uint8_t c[2] = {0x81, v};

    cmd(c, 2);
}
