# DTMF-Signer on the Raspberry Pi Pico (RP2040)

The firmware of the breadboard prototype: it **listens by cable** (DTMF tones
on a line-in) and **speaks by QR** on a 128 × 64 OLED, with five buttons on a
resistor ladder. It is a CMake project of its own that reuses `core/` and
`app/` unchanged; this directory holds only the hardware around them (ADC and
DMA, display, buttons, the USB echo). The profile is
[`PROFILE-PICO-REF`](../../docs/catalog.md#profile-pico-ref--pico-reference)
and the times measured on this board are in the
[catalog](../../docs/catalog.md).

## Build

You need the Raspberry Pi Pico SDK 2.3.1, an Arm GCC toolchain, CMake 3.22 or
later and the repository cloned with `--recurse-submodules`.

```bash
cmake -S ports/pico -B build-pico -DPICO_SDK_PATH=<path to the Pico SDK 2.3.1>
cmake --build build-pico
```

This gives `build-pico/dtmf-signer-pico.uf2`. Options:

| Option | Values | |
| --- | --- | --- |
| `DS_DISPLAY` | `SH1106_SPI` (default), `SSD1306_SPI`, `SSD1306_I2C` | `SH1106_SPI` is the one tested on hardware. `SSD1306_SPI` and `SSD1306_I2C` build, but are **not tested** on hardware. `SSD1306_I2C` uses I2C0 with SDA on GP4, SCL on GP5 and address 0x3C. |
| `DS_DISPLAY_FLIP` | `OFF` (default), `ON` | Rotates the picture 180 degrees. `OFF` is right for the prototype. |

## Flashing

Hold BOOTSEL while plugging in the USB cable, then:

```bash
picotool load -x build-pico/dtmf-signer-pico.uf2
```

The firmware turns off the USB reset-to-bootloader on purpose (the host
must not be able to reboot the device into its bootloader), so
`picotool load -f` does not work: always use BOOTSEL.

## Pins

The only place the pins are named is [`board.h`](board.h).

| Pin | Use |
| --- | --- |
| GP17 | OLED CS (SPI0) |
| GP18 | OLED SCK (SPI0) |
| GP19 | OLED MOSI (SPI0) |
| GP20 | OLED DC |
| GP21 | OLED RES |
| GP4, GP5 | OLED SDA, SCL (only with `SSD1306_I2C`, I2C0, address 0x3C) |
| GP25 | The board's LED |
| GP27 (ADC1) | Line-in |
| GP28 (ADC2) | Button ladder |

The ADC runs round-robin over both inputs at 32 kHz in all (16 kHz for the
line, a 1 kHz reading of the ladder), and a DMA channel writes into a ring.
The ladder gives five buttons: LEFT, UP, OK, DOWN and RIGHT.

## Controls

- **Menus:** UP and DOWN move, OK chooses, LEFT goes back.
- **The picker** (the PIN, the passphrase): UP and DOWN change the letter,
  RIGHT adds it, LEFT deletes the last one, OK finishes. The letter being
  chosen is only added with RIGHT: finishing with OK does not add it.
- **Confirming a signature:** on the last page of the review, a short press
  of any button, at least 300 ms after the review appears, and then hold OK
  for 1.5 s. A hold begun before the review was on screen never confirms.
  Nothing is signed otherwise.
- **Emergency wipe:** hold LEFT for 3 s, on any screen. Everything is zeroed
  and the board reboots.

## The USB echo

The USB serial port is **output only**: the signer never reads from it. It
prints public data and never a secret: the state names, the timings
(`t pin|derive|review|sign|qr <ms>`), the fingerprints, the descriptor, the
signed PSBT in chunks of base64 between `signed-psbt-begin` and
`signed-psbt-end`, and, every 5 s, `stack-free`. Compare the signed PSBT with
the output of the computer build to check the firmware (see
[`app/ds_app.h`](../../app/ds_app.h) for the exact lines).

## Memory

The stack of core 0 is 8 KB, laid over both scratch banks (the SDK's default
is 4 KB). Core 1 is never started, and the linker fails if anything is placed
where it would need its own stack. The minimum free stack seen was 2692 of
8192 bytes, in the review and the signing; static RAM (`.data` and `.bss`) is
about 116 KB of 264 KB.

## What is tested

Tested on the breadboard prototype (SH1106 over SPI): the seed by tones with
its PIN, the fingerprints, the xpub as a QR read by Sparrow, two PSBTs signed
byte for byte as the computer build does, a fake change rejected, and the
emergency wipe.

**Not tested on hardware:** `DS_DISPLAY=SSD1306_SPI` and `SSD1306_I2C`,
`DS_DISPLAY_FLIP=ON`, other line-in stages, and the RP2350.
