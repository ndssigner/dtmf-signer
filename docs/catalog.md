English · [Español](es/catalogo.md)

# DTMF-Signer catalog (draft v0)

The [specification](../SPEC.md) says what every DTMF-Signer must do. This
catalog says **what each piece of a build adds, what it needs and what it
costs**, and which ready-made profiles combine them. It grows with every
build someone makes and measures.

## 1. How to read this catalog

- **Every capability has the same five lines:** what it does; what it
  needs (pins, peripherals, RAM, program memory); its time on each
  processor class; its risks; the vectors that check it.
- **Processor classes:** `CPU-8` (8-bit: AVR, 8051, Z80, 6502, under
  20 MHz), `CPU-32S` (modest 32-bit: Cortex-M0 and similar, under 50 MHz),
  `CPU-32` (32-bit: RP2040, RP2350 and equivalents). See
  [§9 of the specification](../SPEC.md#9-capabilities-and-profiles).
- **Times are measured, never guessed.** Until someone measures one, it
  says "not measured".
- **Prices** are given, if at all, as rough ranges, or entered by whoever
  plans a build. Never fixed prices for one country: they vary too much.
- **New, or salvaged.** Buying the recommended parts is best. Salvaged
  parts are welcome: the catalog says how far they get you and what is
  missing.

## 2. Capabilities

### `TONES-SW` — tones in software

- **What it does:** decodes DTMF from audio samples with the Goertzel
  algorithm, as the ur-tones C library does.
- **Needs:** an ADC sampling at 8 kHz or more, and the CPU to filter in
  real time. An analog anti-aliasing filter before the pin.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** an ADC fed beyond its range is damaged or clips; the input
  stage must protect it. A slow processor misses tones: then use
  `TONES-CHIP`.
- **Vectors:** ur-tones' own vectors and recordings.

### `TONES-CHIP` — tones from a decoder chip

- **What it does:** a DTMF decoder chip (MT8870, HT9170 and compatibles)
  gives each key as four data bits plus a "valid tone" signal. The ur-tones
  library already reads chips like these.
- **Needs:** five digital inputs, the chip's crystal (3.579545 MHz, the
  colour-TV crystal) and a few passive parts.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** hard to buy new in many places, but common in old telephones,
  answering machines and switchboards. A chip of unknown origin decodes
  every key, including those of an unencrypted seed, but keeps nothing: it
  has no memory and no firmware.
- **Vectors:** ur-tones' own vectors, fed as keys.

### `IN-MIC` — microphone

- **What it does:** receives tones through the air (`AIR`).
- **Needs:** an electret capsule with an amplifier, or a module with
  automatic gain; one ADC pin, or the input of `TONES-CHIP`.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** anything it receives is `AIR`: never sensitive data. It must
  not share a pin with `IN-LINE` unless the build treats both as `AIR`.
- **Vectors:** ur-tones' air recordings.

### `IN-LINE` — line input

- **What it does:** receives tones by cable (`CABLE`) from a headphone
  output: the recommended path for the seed.
- **Needs:** a 3.5 mm jack or plug, a DC-blocking capacitor, a bias at half
  the supply, a series resistor and a low-pass filter; one ADC pin, or the
  input of `TONES-CHIP`.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** a careless volume or a wrong cable can push the pin out of
  range: fixed attenuation and clamping diodes protect it.
- **Vectors:** ur-tones' cable recordings.

### `IN-UART` — serial input

- **What it does:** text or ur-tones frames over a serial port (`CABLE`),
  from a vintage terminal or another device.
- **Needs:** a UART, and a level shifter for RS-232 (never RS-232 straight
  to a 3.3 V pin).
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** the terminal sees what it sends: sensitive data needs
  physical confirmation, and is best sent from a device never online.
- **Vectors:** planned.

### `IN-PS2` — PS/2 keyboard

- **What it does:** a keyboard plugged into the signer (`LOCAL`), for the
  PIN, the passphrase and SeedCraft console entry.
- **Needs:** two pins with 5 V level shifting, and 5 V for the keyboard.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** a keyboard with its own firmware could, in theory, remember
  keys; old plain keyboards are the safest.
- **Vectors:** planned.

### `IN-PHONEPAD` — salvaged phone keypad

- **What it does:** the keypad of a landline telephone, wired to the
  signer (`LOCAL`). The PIN is typed with several presses per key for A–Z,
  and goes through `TONES-SW` or `TONES-CHIP`.
- **Needs:** the keypad and its tone generator, wired to a tone input.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** many telephones keep the last number dialled, and some have a
  phone book: use the keypad alone, a telephone without memory, or
  overwrite it. See [§4.3 of the specification](../SPEC.md#43-a-salvaged-phone-keypad).
- **Vectors:** planned.

### `IN-BUTTONS` — buttons

- **What it does:** confirmation, and navigation and typing on a screen
  (`LOCAL`).
- **Needs:** at least one push button. Several can share one ADC pin
  through a resistor ladder, or use one digital pin each.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** a PIN typed on an on-screen grid can be rebuilt by someone who
  sees the screen.
- **Vectors:** planned.

### `OUT-AIR` — tones through the air

- **What it does:** returns data (PSBT, xpub, summaries, fingerprints) as
  tones through a buzzer, a piezo or a speaker (`AIR`).
- **Needs:** a PWM pin. A magnetic buzzer needs a transistor; a piezo can
  be driven straight from two pins.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** anyone nearby hears it: never sensitive data (§4 of the
  specification).
- **Vectors:** ur-tones' own vectors.

### `OUT-LINE` — line output

- **What it does:** returns tones by cable (`CABLE`).
- **Needs:** a PWM pin and an RC filter, to a jack.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** none beyond those of the receiving device.
- **Vectors:** ur-tones' own vectors.

### `OUT-UART` — serial output

- **What it does:** returns text or frames over a serial port (`CABLE`).
- **Needs:** the UART of `IN-UART`.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** never an echo of the PIN or the passphrase (`INV-2`).
- **Vectors:** planned.

### `VERIFY-SCREEN` — the signer's own screen

- **What it does:** shows the summary, the fingerprints and the prompts.
  The recommended verification.
- **Needs:** an OLED (SH1106, SSD1306, SSD1315), an HD44780 character LCD
  (common in old printers, faxes and alarm panels) or seven-segment
  displays; I2C, SPI or parallel pins.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** an implant that reads the screen sees what it shows; the seed
  is never shown, so it has nothing to capture but the grids of the PIN and
  the passphrase.
- **Vectors:** planned (the summary's text).

### `VERIFY-TERMINAL` — a terminal

- **What it does:** the summary on a vintage terminal over UART.
- **Needs:** `IN-UART` and `OUT-UART`.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** the terminal must be one the signer drives, not the device
  that made the PSBT. Final confirmation is still on the signer.
- **Vectors:** planned.

### `VERIFY-TV` — a television

- **What it does:** the summary on a television, as black-and-white
  composite video.
- **Needs:** two pins and two resistors to an RCA jack, and the CPU or a
  peripheral to generate the signal.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** smart TVs can analyse and send what they show; analog video
  can be picked up at a distance with special equipment.
- **Vectors:** planned.

### `VERIFY-LISTENERS` — several listeners

- **What it does:** the summary is broadcast through the air and shown by
  several independent ur-tones listeners. Correct if at least one of them
  is honest. See [§5.1 of the specification](../SPEC.md#51-several-listeners).
- **Needs:** `OUT-AIR`, and two or more independent listeners (for
  example the ur-tones web tool on a phone and on a computer, offline).
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** listeners that are not really independent; tones injected by
  a loudspeaker nearby (a check code is pending).
- **Vectors:** planned.

### `STORE-SEED` — keeping the seed

- **What it does:** keeps the seed, encrypted, across power-offs.
- **Needs:** flash or EEPROM, and an emergency wipe triggered physically
  (button combination, switch or jumper).
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** memory of doubtful origin, extraction from a stolen device,
  erasure that cannot be trusted (wear levelling). See
  [§8 of the specification](../SPEC.md#8-seed-storage-and-emergency-wipe).
- **Vectors:** planned.

### `BTC-TAPROOT` — Taproot

- **What it does:** single-signature P2TR: Schnorr signatures and the
  BIP341 signature hash.
- **Needs:** more code and more hashing than P2WPKH.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** more code to audit.
- **Vectors:** planned.

### `BTC-MULTISIG` — multisig

- **What it does:** signs as one of several signers.
- **Needs:** to know and check the other signers' public keys, so that no
  fake change is accepted; more memory.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** much more complex to verify correctly.
- **Vectors:** planned.

### `BTC-PATHS` — other accounts and paths

- **What it does:** accounts and derivation paths beyond the core's BIP84
  account.
- **Needs:** a way to choose and show them.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** a wrong path gives an empty wallet; it must be visible.
- **Vectors:** planned.

### `BTC-INPUT-AMOUNTS` — checked amounts with several inputs

- **What it does:** takes every input's amount from its full previous
  transaction (`non_witness_utxo`), against the SegWit v0 fee attack.
  Mandatory in `PROFILE-PICO-REF`.
- **Needs:** to hash each previous transaction while reading it in parts;
  more data to receive (a previous transaction is usually a few hundred
  bytes, but can be tens of kilobytes).
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** without it, the signer shows "fee NOT verified" and every
  input's declared amount, and the user checks them elsewhere before
  confirming. See
  [§6 of the specification](../SPEC.md#6-the-segwit-v0-fee-attack).
- **Vectors:** planned.

### `SPLIT-MCU` — several microcontrollers

- **What it does:** one microcontroller handles input and output, and
  another one holds the seed and signs. They talk over serial with
  ur-tones frames (`ut_frame`), without audio. The split separates code and
  memory, not secrets.
- **Needs:** two microcontrollers and a serial link between them.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** the internal protocol must be as strict as the channel policy.
  The input-and-output side handles everything entered (the PIN, the
  passphrase, the seed on its way), so it must be trusted like the signing
  side.
- **Vectors:** planned.

### `SEEDCRAFT-CONSOLE` — SeedCraft console entry

- **What it does:** a seed kept with [SeedCraft](https://github.com/ndssigner/seedcraft)
  is typed in on a PS/2 keyboard with a television, or on a terminal,
  blind: only the progress is shown, never what is typed.
- **Needs:** `IN-PS2` and `VERIFY-TV`, or `VERIFY-TERMINAL`.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** over UART the terminal sees the whole seed: best a device
  never online. It counts as an unencrypted seed in the channel policy.
- **Vectors:** planned.

## 3. Profiles

### `PROFILE-SCRAP` — Scrapyard minimum

- **Capabilities:** `TONES-CHIP`, `IN-LINE`, `IN-BUTTONS` (one push button)
  or `IN-PHONEPAD`, `OUT-AIR`, `VERIFY-LISTENERS`.
- **Parts:** an 8-bit microcontroller, a salvaged MT8870, an input jack, a
  push button, a buzzer.
- **What it lacks:** its own screen, `STORE-SEED`, `BTC-TAPROOT`,
  `BTC-MULTISIG`; possibly `BTC-INPUT-AMOUNTS`, and then the input amounts
  are checked elsewhere before confirming. Signing is slow.

### `PROFILE-PICO-REF` — Pico reference

- **Capabilities:** `TONES-SW`, `IN-LINE`, `IN-MIC`, `IN-BUTTONS`,
  `OUT-AIR`, `VERIFY-SCREEN`, `BTC-INPUT-AMOUNTS`.
- **Parts:** the current prototype, on a breadboard: a Raspberry Pi Pico
  (RP2040; the RP2350 later), a 1.3" SH1106 OLED over SPI, push buttons, a
  3.5 mm plug as line input, a MAX9814 microphone module, a buzzer driven
  through a transistor.
- **What it lacks:** `STORE-SEED`, `BTC-TAPROOT`, `BTC-MULTISIG`,
  `IN-UART` and the other ports, for now.

### `PROFILE-PICO-FULL` — Full Pico

- **Capabilities:** those of `PROFILE-PICO-REF`, plus `IN-UART`,
  `OUT-UART`, `VERIFY-TERMINAL`, `IN-PS2`, `VERIFY-TV`, `OUT-LINE`, and, optionally,
  `SEEDCRAFT-CONSOLE`. No `STORE-SEED`: this board is stateless. A variant
  that adds it needs the emergency wipe of §8 of the specification.
- **Parts:** the board in [`docs/hardware-minimo.md`](hardware-minimo.md)
  (in Spanish).
