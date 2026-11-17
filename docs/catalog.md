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
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` live, up to 30 ms tone + 10 ms silence without errors (RP2040 at 16 kHz, 900 tones). On the Pico prototype (RP2040 at 125 MHz), receiving by cable at 40 ms tone + 20 ms silence: a PSBT of 372 base64 bytes in about 1 min 7 s, one of 920 bytes in 2 min 31 s, a seed UR frame (120 tones) in about 8 s. The headphone output of a laptop needed about 30 % volume there (at 19 % only 107 of 120 tones arrived).
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
  automatic gain; one ADC pin, or the input of `TONES-CHIP`. Tested: a
  MAX9814 module (automatic gain, three pins) straight to the ADC pin; it
  idles at half the 3.3 V supply.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` live
  (RP2040 at 16 kHz): from a handheld console's speaker 2–3 cm away, at air
  timing (80 ms tone + 80 ms silence), 8 frames of 272 tones without a
  wrong tone; from a salvaged telephone ringer about 3 cm away (see
  `OUT-AIR`), 128 keys without an error at 40 ms tone + 20 ms silence,
  5 errors at 30 + 10 ms.
- **Risks:** anything it receives is `AIR`: never sensitive data. It must
  not share a pin with `IN-LINE` unless the build treats both as `AIR`.
  With the gain at its maximum, the module's own noise sets the floor, not
  the room. Not explained yet: in the console test the receiver did not
  see the pauses between frames, so the frames had to be found by their
  sync afterwards.
- **Vectors:** ur-tones' air recordings.

### `IN-LINE` — line input

- **What it does:** receives tones by cable (`CABLE`) from a headphone
  output: the recommended path for the seed.
- **Needs:** a 3.5 mm jack or plug, a DC-blocking capacitor, a bias at half
  the supply, a series resistor and a low-pass filter; one ADC pin, or the
  input of `TONES-CHIP`. Measured: it decodes from about 90 of 4095 counts
  peak to peak (idle noise about 30), so a laptop's headphone output at a
  quarter of its volume is enough.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` live, up to 30 ms tone + 10 ms silence without errors (RP2040 at 16 kHz, 900 tones). On the Pico prototype (RP2040 at 125 MHz), receiving by cable at 40 ms tone + 20 ms silence: a PSBT of 372 base64 bytes in about 1 min 7 s, one of 920 bytes in 2 min 31 s, a seed UR frame (120 tones) in about 8 s. The headphone output of a laptop needed about 30 % volume there (at 19 % only 107 of 120 tones arrived).
- **Risks:** a wrong cable, or a loudspeaker output, can push the pin out of
  range: series resistors, a DC-blocking capacitor and clamping diodes
  protect it. Fixed attenuation is not needed with headphone outputs: it
  would shrink a signal that already arrives small at low volume.
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
  and goes through `TONES-SW` or `TONES-CHIP`. A whole tone telephone on
  `IN-LINE` works too, unmodified; it is then `CABLE`
  ([§4.1 of the specification](../SPEC.md#41-classes)), which still
  takes the seed, the PIN and the passphrase.
- **Needs:** the keypad and its tone generator, wired to a tone input. Or
  a whole telephone and a **simulated line**: the telephone takes its power
  from the line, so it needs a supply through a resistor, and a capacitor
  that passes its tones to the line input. Tested with a Siemens euroset
  805 P without its handset (the hook switch then stays up, off the hook):
  - **5 V is not enough:** the speech circuit holds the line at about
    4.3 V, only 6–7 mA flow, and the dialler stays silent.
  - **9 V through 220 Ω works:** the line sits at 5.8 V, about 15 mA, and
    every key decodes.
  - **The supply must be firm:** a loose contact cuts a tone in two, and
    keypad mode keeps repeated keys, so a digit came out twice.
  - **For a board** (a proposal, not built): a boost from USB's 5 V to
    12–15 V and a constant current of about 25 mA, within the loop current
    telephones are made for (check the figures against the standards: TBR
    21 / ETSI ES 201 970 in Europe, TIA-968 in the United States). A short
    on the line is then harmless.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` a
  12-word seed in keypad mode (`*`, 48 digits, `#`), typed by hand on the
  telephone, reached the signer's firmware without a wrong key (RP2040 at
  16 kHz).
- **Risks:** many telephones keep the last number dialled, and some have a
  phone book: use the keypad alone, a telephone without memory, or
  overwrite it. Measured on that telephone: redial kept the **first 26
  digits**, about half of a 12-word seed, and **kept them without power**;
  it only works as the first thing after picking up. Overwrite it by
  dialling something else, then pick up and press redial to check that it
  sends only that. See [§4.3 of the specification](../SPEC.md#43-a-salvaged-phone-keypad).
- **Vectors:** planned.

### `IN-PHONELINE` — a telephone on the signer's line

- **What it does:** a tone telephone on a line the signer powers. It
  detects the handset off the hook, receives the keypad's DTMF (`CABLE`,
  by the exception in
  [§4.1 of the specification](../SPEC.md#41-classes): only keys typed while
  the signer asks for them) and what the handset's microphone picks up
  (`AIR`). A short hang-up (on-hook 200 to 1500 ms) is the confirmation,
  and no sound can fake it. A longer hang-up keeps the session: on the next
  pick-up the signer offers to resume. Cutting the power wipes everything.
  The PIN is typed by multi-tap, and each typed character is spoken back in
  the earpiece (through the handset: on a speakerphone it is heard aloud,
  and the signer warns once); the seed is never spoken.
- **Needs:** a boost from 5 V to 12 V and a constant current of about
  20 to 25 mA for the line, a divider to measure the line voltage (off
  hook, on hook), the line input of `IN-LINE`, an RJ11 socket and a tone
  telephone with a handset. The hook is read from the line voltage.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** the keypad and the microphone cannot be told apart, so the
  signer trusts the keys only while it is asking for them. A device playing
  tones near the handset (a wallet looping its data, at a pace slow enough
  to look like typing) can press keys: cancel, repeat, start an action. It
  cannot skip the spoken summary, sign or wipe: those need the hook. Keep
  the wallet away from the handset outside data transfers. A speakerphone
  says the keys aloud, and the redial memory may keep what was dialled
  (see `IN-PHONEPAD`). A line source that is wrong can hurt the
  telephone: limit the current, and check the figures against the
  standards (TBR 21 / ETSI ES 201 970 in Europe, TIA-968 in the United
  States).
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
  tones through a speaker, an earbud or a piezo (`AIR`).
- **Needs:** a PWM pin. **A wired earbud or a small speaker works**, salvaged
  ones too: tested, an earbud next to a phone's microphone gave every key
  (pin, 1 kΩ, 10 µF, earbud). **A bare piezo disc on 3.3 V is too quiet:**
  a phone decoded it only touching it. **A salvaged telephone ringer works
  at close range:** an enclosed piezo transducer (a BUJEON B9912 from a desk
  telephone) between two pins driven in opposite phase, with 220 Ω on each
  pin, gets ±3.3 V. It resonates at about 2.3 kHz and falls off below
  1.1 kHz, so the low group must go **12 dB louder** (pre-emphasis); without
  it, the keys of the 697 Hz row are lost. With it, a dedicated microphone
  about 3 cm away heard 128 keys without an error at 40 ms tone + 20 ms
  silence (see `IN-MIC`), and a handheld console running NDS-Signer, its
  microphone about 3 cm from the ringer, received a five-frame PSBT and
  signed it. A phone with the ur-tones web tool lost tones. **A magnetic
  buzzer (2 kHz) does not work** for DTMF: it is far louder in the high
  group, and a decoder rejects most keys even with the low group 12 dB
  louder. It is fine for beeps.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** anyone nearby hears it: never sensitive data (§4 of the
  specification), even with an earbud pressed against the microphone.
  Close range is a feature, not a flaw: the receiving microphone goes
  against the speaker, and the frames do not carry across the room. The
  cable stays the recommended path.
- **Vectors:** ur-tones' own vectors.

### `OUT-LINE` — line output

- **What it does:** returns tones by cable (`CABLE`).
- **Needs:** a PWM pin and an RC filter, to a jack.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** none beyond those of the receiving device.
- **Vectors:** ur-tones' own vectors.

### `OUT-PHONELINE` — audio onto the line

- **What it does:** sends audio onto the telephone line, to the earpiece:
  voice prompts, short sounds (success, failure, warning, listening) and
  the tones of the data (signature, xpub). The data is `AIR`: the earpiece
  held to the wallet's microphone is a broadcast at close range.
- **Needs:** a PWM pin, a filter and a capacitor to the line; the line of
  `IN-PHONELINE`. While it speaks it does not listen, except while asking
  for keys, where a key cuts the prompt.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** a voice can sound like a DTMF tone to the detector; every
  prompt is passed through the detector in a test, and none may give a key.
  Anyone near the earpiece hears the prompts and the data.
- **Vectors:** ur-tones' own vectors.

### `OUT-UART` — serial output

- **What it does:** returns text or frames over a serial port (`CABLE`).
- **Needs:** the UART of `IN-UART`.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** never an echo of the PIN or the passphrase (`INV-2`).
- **Vectors:** planned.

### `OUT-QR` — QR codes on the screen

- **What it does:** returns data (a signed PSBT, an xpub, a descriptor) as
  QR codes on the signer's screen (`AIR`): static, or animated URs for long
  data, as wallets such as Sparrow read them. With `IN-LINE`, a build can
  listen by cable and speak by QR, with no speaker and no microphone.
- **Needs:** a graphic screen (`VERIFY-SCREEN`) or a television
  (`VERIFY-TV`), and a QR encoder (small ones exist in C, without malloc).
  A character LCD (HD44780) cannot draw one; the graphic screens of old
  mobile phones (PCD8544, 84 × 48) fit small codes, up to version 5. An old
  television is the most salvageable option: an 8-bit microcontroller drives
  128 × 96 black-and-white composite video with two resistors (as the
  Arduino TVout library does), and 96 lines fit a version 17 QR (85 modules)
  at 1 pixel per module; but that image takes 1.5 KB, most of an ATmega328's
  RAM, so lower the resolution (64 × 64 still fits version 11) or draw it in
  parts.
  - **A light box around the code only,** dark modules inside it, not the
    whole screen inverted: at equal contrast it dazzles a camera much less
    (in the measurement, 15 % of saturated pixels against 42 %).
  - **Low contrast while a code shows, adjustable by the user** from the
    signer's menu: at high contrast the light of the lit pixels swallows the
    dark modules, and every camera is different.
  - **Module size:** measured on a 128 × 64 OLED with a laptop webcam, codes
    at 2 pixels per module read every time at low contrast (and never at
    full contrast); at 1 pixel per module (up to a version 7 QR, 45 modules,
    enough for a BIP-84 descriptor in one code) a phone camera reads them,
    but the webcam does not reliably. So 2 pixels per module is the default,
    with long data as animated URs, and 1 pixel per module a fast mode for
    phones. A 128 × 128 screen fits a descriptor at 2 pixels per module.
  - **Animated URs on a 128 × 64 screen:** the parts of an xpub or a PSBT do
    not fit at 2 pixels per module (64 pixels high allow a version 3 code at
    most, with fragments of about 10 bytes), so they go at 1 pixel per
    module. Measured on the Pico prototype: Sparrow on a laptop webcam read
    the xpub and a signed PSBT at 1 pixel per module only with the QR
    contrast at its minimum (`0x01`). A 128 × 128 screen would allow 2 pixels per
    module.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` starting a QR (choosing the fountain fragment and building the first part) takes 119–125 ms for a signed PSBT of about 387 bytes or an xpub account, and 297 ms for one of 906 bytes (RP2040 at 125 MHz).
- **Risks:** any camera that sees the screen can read it: never sensitive
  data (§4 of the specification). It is not a verification output: the
  device that reads it is the one that is not trusted. A wallet needs a
  descriptor, with the fingerprint and the path, to import an account: a bare
  xpub is not enough.
- **Vectors:** planned (the texts it shows come from the core's vectors).

### `VERIFY-SCREEN` — the signer's own screen

- **What it does:** shows the summary, the fingerprints and the prompts.
  The recommended verification.
- **Needs:** an OLED (SH1106, SSD1306, SSD1315), an HD44780 character LCD
  (common in old printers, faxes and alarm panels) or seven-segment
  displays; I2C, SPI or parallel pins.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** an implant that reads the screen sees what it shows; the seed
  is never shown, so it has nothing to capture but the grids of the PIN and
  the passphrase. It does see the summaries, the xpub and the fingerprints:
  a privacy leak, though not a theft.
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
  composite video. It can also show QR codes (`OUT-QR`).
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

### `VERIFY-VOICE` — the summary, spoken

- **What it does:** says the summary and the fingerprints in the earpiece:
  amounts, fee, which output is change, and each destination by default as
  the first 5 and the last 5 characters of the address. On request (key 5)
  it says the whole address in groups of 4. It is the verification of
  `PROFILE-PHONE`.
- **Needs:** `OUT-PHONELINE`, and the voice prompts in program memory.
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` not measured.
- **Risks:** an address made to match in prefix and suffix defeats the
  partial readout: the full check of the address belongs in the wallet.
  Whoever hears the earpiece learns the amounts and destinations (privacy,
  not theft). It needs a quiet moment: a misheard character is a wrong
  address that looks right, so the whole address is there to be asked for.
  See [§5 of the specification](../SPEC.md#5-verifying-what-is-signed).
- **Vectors:** planned (the spoken text of a summary).

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
- **Time:** `CPU-8` not measured · `CPU-32S` not measured · `CPU-32` reviewing a PSBT of 2 inputs with their full previous transactions takes 315 ms (RP2040 at 125 MHz).
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
  or `IN-PHONEPAD`, `OUT-AIR`, `VERIFY-LISTENERS`. Variant: `VERIFY-TV` and
  `OUT-QR` on an old television instead of `OUT-AIR` and
  `VERIFY-LISTENERS`: the television shows the summary, and the signed PSBT
  as a QR.
- **Parts:** an 8-bit microcontroller, a salvaged MT8870, an input jack, a
  push button, a salvaged earbud or small speaker (not a magnetic buzzer);
  or, in the variant, two resistors and an RCA lead to the television.
- **What it lacks:** its own screen, `STORE-SEED`, `BTC-TAPROOT`,
  `BTC-MULTISIG`; possibly `BTC-INPUT-AMOUNTS`, and then the input amounts
  are checked elsewhere before confirming. Signing is slow.

### `PROFILE-CABLE-QR` — Cable and QR

- **Capabilities:** `TONES-SW`, `IN-LINE`, `IN-BUTTONS`, `VERIFY-SCREEN`,
  `OUT-QR`, `BTC-INPUT-AMOUNTS`.
- **Parts:** a 32-bit microcontroller with an ADC (a Raspberry Pi Pico), one
  graphic screen (a 128 × 64 OLED works; 128 × 128 is faster for long
  data), a 3.5 mm jack or plug with its input stage, a few push buttons.
- **What it lacks:** any air path: no microphone and no speaker, so it
  cannot listen to the room. A phone without a headphone jack needs an
  adapter to send the tones. Also `STORE-SEED`, `BTC-TAPROOT`,
  `BTC-MULTISIG`.

### `PROFILE-PICO-REF` — Pico reference

- **Capabilities:** `TONES-SW`, `IN-LINE`, `IN-MIC`, `IN-BUTTONS`,
  `OUT-AIR`, `OUT-QR`, `VERIFY-SCREEN`, `BTC-INPUT-AMOUNTS`.
- **Parts:** the current prototype, on a breadboard: an RP2040 board with
  the Pico's layout and USB-C (the RP2350 later), a 1.3" SH1106 OLED over
  SPI (GP17 to GP21), push buttons, a 3.5 mm plug as line input (GP27), a
  wired earbud as air output (GP15), and a MAX9814 microphone module (GP26,
  not yet tested). Smaller RP2040 boards, such as the RP2040-Zero, run the
  same chip but have their pins round the edge: they suit a perfboard or a
  PCB, not a breadboard.
- **What it lacks:** `STORE-SEED`, `BTC-TAPROOT`, `BTC-MULTISIG`,
  `IN-UART` and the other ports, for now.
- **Measured** on the breadboard prototype with the firmware of
  [`ports/pico/`](../ports/pico/README.md) (RP2040 at 125 MHz, `CPU-32`):
  - Seed PIN (PBKDF2, 10 000 iterations) and seed decoding: 4412 ms.
  - BIP-39 and BIP-32 derivation (seed and wallet fingerprints, account):
    2595 ms.
  - Reviewing a PSBT of 1 input and 2 outputs (372 base64 bytes): 226 ms;
    of 2 inputs with their full previous transactions: 315 ms; rejecting a
    fake change: 164 ms.
  - Signing: 123 ms with 1 input, 240 ms with 2.
  - Starting a QR: 119–125 ms (a signed PSBT of about 387 bytes, or an xpub
    account), 297 ms (a signed PSBT of 906 bytes).
  - Minimum free stack seen: 2692 of 8192 bytes (the deepest flows are the
    review and the signing). Static RAM (`.data` and `.bss`): about 116 KB of
    264 KB.

### `PROFILE-PICO-FULL` — Full Pico

- **Capabilities:** those of `PROFILE-PICO-REF`, plus `IN-UART`,
  `OUT-UART`, `VERIFY-TERMINAL`, `IN-PS2`, `VERIFY-TV`, `OUT-LINE`, and, optionally,
  `SEEDCRAFT-CONSOLE` and `BTC-TAPROOT` (planned in its firmware modules).
  No `STORE-SEED`: this board is stateless. A variant
  that adds it needs the emergency wipe of §8 of the specification.
- **Parts:** the board in [`docs/hardware-minimo.md`](hardware-minimo.md)
  (in Spanish).

### `PROFILE-PHONE` — Phone exchange (Centralita)

- **Capabilities:** `TONES-SW`, `IN-PHONELINE`, `OUT-PHONELINE`, `VERIFY-VOICE`, `BTC-INPUT-AMOUNTS`.
- **Parts:** a Pico, a 5 to 12 V boost and a current source of about
  20 to 25 mA for the line, a divider to measure the line, the usual line
  input, the audio output into the line, an RJ11 socket and a tone
  telephone with a handset.
- **What it lacks:** its own screen, buttons, `OUT-QR`, `STORE-SEED`,
  `BTC-TAPROOT`, `BTC-MULTISIG` and the passphrase.
