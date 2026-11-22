# DTMF-Signer on the Raspberry Pi Pico (RP2040)

The firmware of the breadboard prototype: it **listens by cable** (DTMF tones
on a line-in) and **speaks by QR** on a 128 × 64 OLED, with five buttons on a
resistor ladder. It is a CMake project of its own that reuses `core/` and
`app/` unchanged; this directory holds only the hardware around them (ADC and
DMA, display, buttons, the USB echo). The profile is
[`PROFILE-PICO-REF`](../../docs/catalog.md#profile-pico-ref--pico-reference)
and the times measured on this board are in the
[catalog](../../docs/catalog.md). With `DS_FIRMWARE=PEX` it builds
[the Phone Exchange (PEX)](#the-phone-exchange-pex) instead, the voice firmware.

## Get the firmware

Two ways, both giving the same program.

### The published UF2

The releases after v0.1.0-alpha carry `dtmf-signer-pico.uf2`, built by the
project's CI from the release's tag, and its SHA-256. Take it if you do not
want to build it: you are then trusting that build instead of checking it
yourself, a choice worth making knowingly. Anyone can check it for everyone
by building the same tag on Linux x86_64, as below, and comparing the
SHA-256.

### Build it yourself

From a clone of the repository (not a downloaded archive: the version
shown on the About page comes from git):

```bash
git clone --recurse-submodules https://github.com/ndssigner/dtmf-signer
cd dtmf-signer
git checkout v0.2.0-alpha    # the release to build; skip it for the latest code
tools/build-pico.sh
```

The script fetches the Pico SDK 2.3.1 and Arm's GNU toolchain 14.3.rel1
(checked against Arm's SHA-256) into `~/.cache/dtmf-signer`, builds from
scratch and prints the UF2's path and SHA-256. Built on Linux x86_64, as
the published one is, it matches it byte for byte for a release's tag. On
other hosts the firmware is the same program, but Arm's toolchain for each
host lays the code out slightly differently, so the SHA-256 differs; the
script says so. It needs git, CMake 3.22 or later, Ninja or
make, Python 3, curl and tar, and picotool 2.3.1 (`brew install picotool`
on macOS; elsewhere the SDK builds it, which needs a C++ compiler and
libusb, `build-essential` and `libusb-1.0-0-dev` on Debian or Ubuntu). It
runs on Linux (x86_64, aarch64), on macOS with Apple silicon, and on
Windows through WSL. Arm publishes no 14.3 toolchain for Intel Macs: there,
use the published UF2 or the container below.

To check a release from a Mac or any other host, build it in a Linux
x86_64 container with the same packages as the CI (on a Mac with Apple
silicon it gave the CI's UF2 byte for byte, in about ten minutes under
emulation):

```bash
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src ubuntu:24.04 sh -c \
  'apt-get update && apt-get install -y --no-install-recommends git ca-certificates cmake ninja-build python3 curl xz-utils build-essential libusb-1.0-0-dev pkg-config && git config --global --add safe.directory "*" && tools/build-pico.sh'
```

With your own SDK and toolchain the build is the usual one, though its UF2
will not match the published one byte for byte:

```bash
cmake -S ports/pico -B build-pico -DPICO_SDK_PATH=<path to the Pico SDK 2.3.1>
cmake --build build-pico
```

Options:

| Option | Values | |
| --- | --- | --- |
| `DS_DISPLAY` | `SH1106_SPI` (default), `SSD1306_SPI`, `SSD1306_I2C` | `SH1106_SPI` is the one tested on hardware. `SSD1306_SPI` and `SSD1306_I2C` build, but are **not tested** on hardware. `SSD1306_I2C` uses I2C0 with SDA on GP4, SCL on GP5 and address 0x3C. |
| `DS_DISPLAY_FLIP` | `ON` (default), `OFF` | Rotates the picture 180 degrees. `ON` is right for the [breadboard layout](../../hardware/breadboard/README.md); `OFF` for a screen mounted the other way round. Both are tested. |
| `DS_FIRMWARE` | `CABLE_QR` (default), `PEX` | `CABLE_QR` builds `dtmf-signer-pico.uf2`, this page's firmware. `PEX` builds `dtmf-signer-pex.uf2`, the voice firmware: see [The Phone Exchange (PEX)](#the-phone-exchange-pex). |
| `DS_LINE_DEBUG` | `OFF` (default), `ON` | PEX only, a bench aid: once a second the USB echo gives the line's level and what the receiver has heard. Never in a release. |

## Power

Many cheap RP2040 boards with a USB-C socket lack the two 5.1 kΩ resistors
that tell a USB-C charger to give power, so a USB-C to USB-C charger (a
laptop's, for instance) leaves them off. A USB-A charger with a USB-A to
USB-C cable works. To use it, always a charger or a power bank, never a
computer (only to flash it): that keeps [the USB echo](#the-usb-echo) away
from any computer.

## Flashing

Hold BOOTSEL while plugging in the USB cable: the Pico shows up as a drive
called `RPI-RP2`. Copy the UF2 onto it; the Pico restarts with the new
firmware. Or, with picotool:

```bash
picotool load -x build-pico-release/dtmf-signer-pico.uf2
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
| GP25 | The board's LED: a short flash per tone heard, off otherwise |
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
- **Screens a job puts up** (the fingerprints, the errors, the QR) ignore
  button presses for the first 300 ms.
- **On the seed-fingerprint screen,** LEFT discards the seed and wipes
  everything.
- **Emergency wipe:** hold LEFT for 3 s, on any screen. Everything is zeroed
  and the board reboots. While "Working..." is on screen the buttons are not
  read, so the 3 s only start counting once the job has finished.

## The USB echo

The USB serial port is **output only**: the signer never reads from it. It
prints public data and never a secret: the state names, the timings
(`t pin|derive|review|sign|qr <ms>`), the fingerprints, the descriptor, the
tone level of each group heard (`level -32.6 dBFS`), the
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

## The Phone Exchange (PEX)

The voice firmware (profile
[`PROFILE-PHONE`](../../docs/catalog.md#profile-phone--phone-exchange-pex)):
a tone telephone is the whole interface. It has no screen or buttons. The
signer powers the telephone line, speaks into it and hears the keys and the
wallet's tones on it. The logic is `app/ds_voice`, the same that runs in the
website's simulator; [`voice_main.c`](voice_main.c) is the hardware around
it.

```bash
cmake -S ports/pico -B build-pex -DPICO_SDK_PATH=<path to the Pico SDK 2.3.1> -DDS_FIRMWARE=PEX
cmake --build build-pex
```

It links libspeex without its encoder (`speex_nb_dec`) and the built-in
English prompts. It must fit in the first MB of flash, or the link fails:
the second MB is the voice packs' region.

### Voice packs

Every language but English is a pack, written apart from the firmware, at
flash offset 1 MB (see [`tools/voice`](../../tools/voice/README.md)):

```bash
python3 tools/voice/make_uf2.py app/voice/packs/es.bin --offset 0 es.uf2
picotool load es.uf2
picotool load -x build-pex/dtmf-signer-pex.uf2
```

Both loads need BOOTSEL, as above. With more than one language, the
PEX lists them when the handset is lifted.

### The line

**Never connect it to a socket of the real telephone network:** that line
carries up to 48 V, and about 90 V AC when it rings.

The telephone is a whole tone telephone, handset and all, plugged with its
own telephone cable into an RJ11 socket on the board: the line is the
socket's two centre contacts.

The signer feeds the line itself: the 5 V of the USB, raised to 12 V by a
boost module, then a constant-current source of about 24 mA (an LM317 with
51 ohms between OUT and ADJ). The current is what a telephone expects, and a
short circuit on the line does no harm. An open line sits at about 11 V; a
telephone off the hook holds it at 6 to 8 V.

| Pin | Use |
| --- | --- |
| GP27 (ADC1) | The line, heard: 100 nF from the line to a node biased at 1.65 V (10 k to 3V3, 10 k to ground), then 1 k + 4.7 k with 10 nF to ground, as the line-in above |
| GP28 (ADC2) | The line's voltage, for the hook: 47 k from the line, 10 k and 100 nF to ground |
| GP15 | The voice into the line: PWM (10 bits, 122 kHz), 1 k and 10 nF to ground, then 10 uF (+ towards the line) |
| GP25 | The board's LED: see below |

The hook is off below 8 V on the line and on above 9 V. ds_voice then
debounces it and tells a hook flash (200 ms to 1.5 s) from a hang-up.

The wallet reaches the signer by air, through the handset's microphone: the
person holds the handset to the wallet while its tones play.

### The LED

While the handset is held to a wallet, the person cannot hear the signer
(and it is silent: it only listens), so the LED tells how a reception goes:

| What happens | The LED |
| --- | --- |
| A tone or a key is heard | a short flash |
| A part of a PSBT is taken | a long flash (300 ms), in the pause between frames |
| A seed by the handset, or a PSBT, is complete | on, steady, until a key or the hook |

### The USB echo

Output only, public data only, as above: what is said (`say`, `ear`,
`num`, `chars`), the states, `hook on|off`, the fingerprints, the
descriptor, the signed PSBT between `signed-psbt-begin` and
`signed-psbt-end`, `work-ms N` for each piece of work over 50 ms, and,
every 5 s, `stack-free`, `loop-max-us` (the longest turn of the loop) and
`underruns` (voice samples the PWM found missing: it plays silence then).

### Timing and memory

Measured at 125 MHz with the test seed (all zeros):

| Work | Time |
| --- | --- |
| The seed, no PIN | 2.6 s |
| The seed, a 12-character PIN | 4.5 s |
| The wallet's derivation | 2.6 s |
| The account | under 50 ms |
| A PSBT's analysis | 170 ms |
| The signature | 130 ms |

A key taken for work sounds a short motif and then the wait motif, which
the PWM interrupt loops on its own while the work holds the core. Away from
the work, a turn of the loop takes about 3 ms; the voice is queued 20 ms
ahead (`DS_VOICE_OUT_DELAY_MS` is 40). The minimum free stack seen was 2.8 KB
of 8 KB, in the review; static RAM is about 129 KB of 264 KB, flash about
555 KB.

### What is tested (PEX)

On a breadboard, with a tone telephone that had lost its handset: its hook
switch was pressed by hand, a pair of wired earbuds stood in for the
earpiece, and an electret microphone module (a MAX9814, through a
22 k / 220 ohm divider and two 1 uF capacitors) for the microphone; the
wallet's tones were also fed by cable into that microphone input. So tested:
the menus and the language list, the hook flash that wipes and the one that
signs, the seed by the keypad, the seed by the handset encrypted with a
12-character PIN, a PSBT received the same way, reviewed and signed byte for
byte as the computer build does, the account sent and read by a phone
running ur-tones, the wait motif and the LED.

**Not yet right:** by air, from a laptop's speaker to the microphone module,
about one tone in ten was lost in long frames (laptops give little below
1 kHz, where the lower DTMF tones are). And on the board, a wallet still
looping a PSBT after it was complete once made two of its tones count as
keys, which cancelled the review; the computer's tests of that case pass, and
it is being looked into.

## What is tested

Tested on the breadboard prototype (SH1106 over SPI): the seed by tones with
its PIN, the fingerprints, the xpub as a QR read by Sparrow, two PSBTs signed
byte for byte as the computer build does, a fake change rejected, and the
emergency wipe.

**Not tested on hardware:** `DS_DISPLAY=SSD1306_SPI` and `SSD1306_I2C`,
other line-in stages, and the RP2350.
