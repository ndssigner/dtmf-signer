/* SSD1306, over SPI0 (DS_DISPLAY_SSD1306_SPI) or I2C0 (DS_DISPLAY_SSD1306_I2C).
 *
 * NOT TESTED ON HARDWARE: the prototype has an SH1106. The table follows the
 * SSD1306 datasheet: charge pump on (8D 14), page addressing, no column
 * offset (the SSD1306's RAM is 128 columns wide). */
#include "display.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "board.h"

#if defined(DS_DISPLAY_SSD1306_I2C)
#define OLED_I2C i2c0

static void cmd(const uint8_t *c, size_t n)
{
    uint8_t buf[4];
    size_t i;

    /* One command per transfer: control byte 0x00, then the byte. */
    for (i = 0; i < n; i++) {
        buf[0] = 0x00;
        buf[1] = c[i];
        i2c_write_blocking(OLED_I2C, DS_OLED_I2C_ADDR, buf, 2, false);
    }
}

static void data(const uint8_t *d, size_t n)
{
    uint8_t buf[1 + 128];

    buf[0] = 0x40; /* control byte: display data follows */
    for (size_t i = 0; i < n && i < 128; i++) {
        buf[1 + i] = d[i];
    }
    i2c_write_blocking(OLED_I2C, DS_OLED_I2C_ADDR, buf, 1 + n, false);
}
#elif defined(DS_DISPLAY_SSD1306_SPI)
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
#else
#error "display_ssd1306.c needs DS_DISPLAY_SSD1306_SPI or DS_DISPLAY_SSD1306_I2C"
#endif

void ds_display_init(void)
{
    static const uint8_t init[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock divide / oscillator */
        0xA8, 0x3F, /* multiplex: 64 rows */
        0xD3, 0x00, /* display offset: none */
        0x40,       /* start line 0 */
        0x8D, 0x14, /* charge pump on */
        0x20, 0x02, /* page addressing mode */
#ifdef DS_DISPLAY_FLIP
        0xA0,       /* segment remap: rotated 180 degrees */
        0xC0,       /* COM scan bottom to top */
#else
        0xA1,       /* segment remap: column 0 on the left */
        0xC8,       /* COM scan top to bottom */
#endif
        0xDA, 0x12, /* COM pins configuration */
        0x81, 0x80, /* contrast */
        0xD9, 0xF1, /* pre-charge period */
        0xDB, 0x40, /* VCOM deselect level */
        0xA4,       /* show RAM contents */
        0xA6,       /* normal, not inverted */
    };
    static const uint8_t on[] = {0xAF};
    static const uint8_t blank[8][128];

#if defined(DS_DISPLAY_SSD1306_I2C)
    i2c_init(OLED_I2C, 400 * 1000);
    gpio_set_function(DS_PIN_OLED_SDA, GPIO_FUNC_I2C);
    gpio_set_function(DS_PIN_OLED_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(DS_PIN_OLED_SDA);
    gpio_pull_up(DS_PIN_OLED_SCL);
    sleep_ms(100);
#else
    const uint pins[] = {DS_PIN_OLED_CS, DS_PIN_OLED_DC, DS_PIN_OLED_RES};
    size_t i;

    spi_init(OLED_SPI, 4 * 1000 * 1000);
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
#endif
    cmd(init, sizeof init);
    /* The RAM holds noise after power-up: blank it before turning on. */
    ds_display_flush(blank);
    cmd(on, 1);
}

void ds_display_flush(const uint8_t fb[8][128])
{
    uint8_t p;

    for (p = 0; p < 8; p++) {
        const uint8_t addr[] = {(uint8_t)(0xB0 | p), 0x00, 0x10};

        cmd(addr, sizeof addr);
        data(fb[p], 128);
    }
}

void ds_display_contrast(uint8_t v)
{
    const uint8_t c[2] = {0x81, v};

    cmd(c, 2);
}
