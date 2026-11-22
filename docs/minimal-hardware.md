English · [Español](es/minimal-hardware.md)

# Minimal DTMF-Signer — Hardware specification (draft)

**Status:** a design proposal, not tested on hardware.
**Platform:** Raspberry Pi Pico 2 (RP2350), bare metal, no operating system.
**Profile:** this document describes the `PROFILE-PICO-FULL` profile of the [specification](../SPEC.md) and the [catalog](catalog.md). The current prototype (`PROFILE-PICO-REF`) uses an RP2040 Pico.

---

## 1. Philosophy

- **The core idea: the seed arrives as tones and is never shown.** It is made elsewhere, and [ur-tones](https://github.com/ndssigner/ur-tones) turns it into DTMF tones, with or without a PIN, on an offline computer. The signer receives it by audio and never shows it, on any channel: only its fingerprint. From there, **the user chooses** how to interact: all by audio, for the most isolation, or with a vintage terminal over UART.
- **A basic signer that works on its own.** Screen, D-pad and audio built in. Anyone can load a seed, review a transaction and sign it with nothing else.
- **Better still with a borrowed interface.** The signer is at its best connected to a vintage device (Game Boy, HP-48, C64…) that brings a screen and a keyboard. That path is offered and documented, but never required.
- **Assemble and go.** The project offers a PCB and a breadboard layout. The user only flashes the Pico 2 and plugs it into its socket.
- **The optional parts planned, not fitted.** The ports for enthusiasts (RS-232, PS/2, video, audio output by cable, batteries) come as holes with their circuit already designed. Whoever wants them solders the parts the silkscreen names.
- **No black boxes.** The whole signal path is the project's own auditable code. No modules with closed firmware (QR readers, cameras); for signing by QR, other projects already exist.
- **Air-gapped communication by audio** with [ur-tones](https://github.com/ndssigner/ur-tones): by air for what is public, by cable for what is sensitive.
- **For enthusiasts.** We document the interfaces well; adapting other devices' power and levels is up to the community.

## 2. Base board (fitted as standard)

| Component | Function | Approx. cost |
|---|---|---|
| Raspberry Pi Pico 2 in a socket (2 × 20-pin female headers) | MCU | $5.50 |
| 1.3" 128×64 I2C OLED (SH1106 or SSD1306) | Screen | $3-4 |
| 5-button D-pad (4 directions + centre) on a resistor ladder | Navigate, type, confirm | $0.50 |
| Electret microphone + MCP6001 + wide band-pass filter | Receiving by air | $1 |
| Piezo driven directly by the Pico | Sending by air | $0.30 |
| 3.5 mm TRS jack, **input only**, with a detect contact, and its line stage | The cable channel for sensitive data | $0.50 |
| Power-only USB-C + 2 × 5.1 kΩ + Schottky | Power | $0.70 |
| Keep-alive load (2N7000 + 33 Ω 1 W) | Keeps power banks from switching off | $0.30 |
| PCB (small run) | | ~$2 |
| **Total** | | **~$13.50** |

## 3. Holes with a planned circuit (not fitted)

| Port | Parts to solder | Notes |
|---|---|---|
| TTL serial | 4-pin header | 3.3 V, on the same pins as RS-232: **one or the other** |
| RS-232 serial | MAX3232 + 4 capacitors + male DE-9 | Never connect RS-232 straight to the Pico |
| PS/2 keyboard | Female 6-pin mini-DIN + 2 BSS138 + resistors | The keyboard needs 5 V: there is 5 V with USB-C; on batteries, each makes their own arrangement |
| B/W composite video | Female RCA + 2 resistors | A TV or monitor as a passive screen |
| Audio output by cable | Jack, terminal block or wires + RC filter | For those who want to send by cable |
| Batteries | Battery holder of choice + Schottky | 2×AAA recommended; CR2032 experimental |
| Expansion | Header with the free GPIOs, 3V3, 5V and GND | GP12-GP19 match the HSTX block (DVI, VGA…) |

## 4. Power

- **As standard: power-only USB-C**, from a phone charger or a power bank.
  - It needs **two 5.1 kΩ resistors on CC1 and CC2**; without them, USB-C to USB-C chargers give no voltage. The supply does the regulation.
  - **The data lines connect to nothing**: isolation in hardware even if it is plugged into a computer.
  - From the 5 V rail come VSYS (through a Schottky) and the 5 V of the PS/2 and expansion holes.
  - **Unplugging = switching off = seed lost.** No switch is needed. RAM loses its content without power, though not instantly; that is why the firmware zeroes it first: when done, on cancel and on inactivity.
- **Keep-alive load:** the firmware makes a pulse of ~150 mA for ~100 ms every ~10 s, so power banks do not switch off for low draw. Duration, interval and activation configurable; off with a wall charger.
- **The Pico's micro-USB: for flashing only.** In a pinch it can power it from a charger, never from a computer: that port does carry data.
- **Batteries (optional, holes):** to VSYS through a Schottky. 2×AAA recommended (~200-250 five-minute sessions, estimated). The CR2032 is an experimental variant: at 25-45 mA its voltage drops and it may switch off mid-session. Not dangerous, as there is no state to lose, but annoying.
- **Estimated draw in use: ~25-45 mA.** *To be measured.*

## 5. Pin assignment

| GPIO | Use | Notes |
|---|---|---|
| GP0 / GP1 | UART0 TX / RX | To the TTL header or the MAX3232 (holes) |
| GP2 / GP3 | Piezo | PWM in anti-phase (same slice, outputs A and B): doubles the voltage |
| GP4 / GP5 | I2C0 SDA / SCL | OLED |
| GP6 | Keep-alive load | The 2N7000's gate |
| GP7 | Free (expansion) | |
| GP8 | Cable detect on the INPUT jack | The jack's switch contact |
| GP9 | Free (expansion) | |
| GP10 / GP11 | PS/2 data / clock (holes) | PIO, through BSS138 |
| GP12-GP19 | Free (expansion) | The RP2350's HSTX block |
| GP20 | Audio output by cable (holes) | PWM + RC filter |
| GP21 / GP22 | Composite: sync / video (holes) | PIO |
| GP25 | The board's LED | Activity |
| GP26 (ADC0) | Microphone | **Max 3.3 V** |
| GP27 (ADC1) | The INPUT jack's line | **Max 3.3 V** |
| GP28 (ADC2) | D-pad (resistor ladder) | **Max 3.3 V** |
| GP29 (ADC3, internal) | VSYS/3 | Already wired on the board |

## 6. Audio

As the microphone and the line come in on different ADC pins, and the jack has a detect contact, **the firmware always knows where each datum arrives from**.

**Analog filtering: a wide band-pass from ~300 Hz to ~4 kHz** on both inputs.
- The high-pass removes DC, mains hum, knocks and wind. It comes from the coupling capacitors and the bias.
- The low-pass is the **anti-aliasing filter**, indispensable. The ADC can sample far above what is needed and filter digitally afterwards, so a simple filter is enough.
- **No narrow filters per DTMF tone.** Goertzel is already a much narrower filter. Narrow analog filters ring and smear short tones, need precise parts, and would tie the board to DTMF for good.

**Microphone input:**
- Electret biased with 2.2 kΩ to 3V3.
- 1 µF coupling capacitor.
- Non-inverting amplifier with a gain of 20 to 50, centred at 1.65 V by a 2×10 kΩ divider.
- Low-pass filter before GP26.

**Line input (INPUT jack):**
- Connected with an ordinary male-to-male cable from the source device's headphone output. Phones without a jack need a USB-C to 3.5 mm adapter.
- 1 µF coupling capacitor, centred at 1.65 V by 2×10 kΩ, a 1 kΩ series resistor as protection, and a low-pass filter before GP27.
- If the source gives more level than the ADC takes, a divider at the input.

**Protecting the line input:** a slip with the volume or the cable must not damage anything.
- **No fixed attenuation.** Measured on the prototype (RP2040, the same stage with 1 kΩ and a 4.7 kΩ / 10 nF filter): from a laptop's headphone output it decodes without errors from a quarter of the volume, about 90 of 4095 counts peak to peak, with an idle noise of about 30. Attenuating would shrink a signal that already arrives small. If a source gives more than the ADC takes, the level indicator asks for less volume.
- **Two external Schottky diodes** (through-hole BAT85) from the input to 3V3 and to GND, in unfitted holes. They take the current before the pin's internal diodes, for serious mistakes such as an amplifier's speaker output.
- **The series resistors limit the current** into the diodes: with the stage's ~5.7 kΩ, an excess of several volts lets less than 1 mA through.
- **The coupling capacitor blocks DC**: a DC voltage put on the jack by mistake, from a 12 V charger for instance, does not reach the pin. It must withstand at least 25 V.
- **A level indicator in the firmware:** it should measure the tones' power, as the ur-tones detector does, not the peak, which includes the noise. The "low" threshold sits far below what it might seem: it already decodes at about 90 counts. A convenience, not a protection.
- *Measured:* at 40/20 ms and down to 30/10 ms, no errors in 900 tones.

**Sending by air (piezo):**
- Driven directly from GP2 and GP3 in anti-phase, with a series resistor. No amplifier needed.
- The piezo is much louder near 1.5 kHz than near 700 Hz. **The firmware compensates by giving the low group of tones more amplitude.**
- **A magnetic buzzer will not do:** tried on the prototype, a 2 kHz one is much louder in the high group, and the decoder rejects most keys even with 12 dB of compensation. **A wired earbud** next to the phone's microphone, by contrast, gave every key: a tested alternative, for instance from the audio output by cable.
- **Use: devices close by (a few centimetres) and a moderate volume.** At 1-2 cm it can saturate the phone's microphone. The documentation should say where the microphone usually is (the bottom edge on phones, next to the webcam on laptops).

**Robustness in firmware:** check the tones' energy against the total, the *twist* between the two tones, the second harmonics and the minimum duration, besides ur-tones' Reed-Solomon correction.

**For the ur-tones web tool:** open the microphone with `echoCancellation`, `noiseSuppression` and `autoGainControl` set to `false`, as that processing is meant for speech.

## 7. Built-in interface

**128×64 OLED screen:** 21 columns × 8 lines. The firmware must support SH1106, SSD1306 and SSD1315, which circulate mixed up. **The module's pin order varies by maker** (VCC-GND or GND-VCC): a solder jumper to choose, and a warning on the silkscreen.

**5-button D-pad on the ADC:** each button pulls a different resistor to ground against a 10 kΩ pull-up. Indicative values: 0 Ω, 2.2 kΩ, 4.7 kΩ, 10 kΩ and 22 kΩ, giving ~0 V, 0.6 V, 1.06 V, 1.65 V and 2.27 V; 3.3 V when none is pressed. It does not detect simultaneous presses, and need not.

**Reviewing and signing a transaction:**

```
[1/5] SUMMARY            [2/5] OUTPUT 1/2        [3/5] OUTPUT 2/2
2 outputs                bc1q 9h7g 2x4k ...      CHANGE (verified)
Send: 0.01500000         ... (4 lines)           0.00480000
Fee: 1200 sat            0.01500000 BTC
(3.1 sat/vB)

[4/5] FEE                [5/5] SIGN?
1200 sat                 Hold centre 2 s: sign
3.1 sat/vB               Hold left 2 s: cancel
```

- Addresses are shown whole, in groups of 4 characters. A 62-character Taproot one takes 4 lines.
- **The option to sign only appears after going through every screen.** There is no shortcut.
- **Signing takes a 2 s long press.**
- The change is marked "verified" only if the signer checks it belongs to the loaded seed.

**PIN (A-Z, 0-9):** a single page of 4 rows of 10: the 26 letters, the 10 digits and a delete cell, the 36 symbols ur-tones takes.

```
PIN                 4
TEST_
. . . . . . . . . . . . . . . . .
 A B C D E F G H I J    +-----+
 K L M N O P Q R S T    |  T  |   <- magnifier: the chosen character,
 U V W X Y Z 0 1 2 3    |     |      three times larger
 4 5 6 7 8 9 <-         +-----+
```

- The arrows move the cursor, which wraps around the edges. OK adds the character and the cursor stays where it is, so a repeated character is OK, OK. Holding OK finishes; with the PIN empty, it is "no PIN". The delete cell removes the last character, and with the input empty goes back. Holding left does nothing here: at 3 s it is the emergency wipe.
- **It shows in clear:** only whoever has the signer in front of them sees it, and so you see what you type. Against a camera that can see the screen, the PIN and the passphrase can come by cable.
- The magnifier on the right shows the chosen character three times larger: at 5×7, `l`, `1`, `I` and `|`, or `O` and `0`, get confused.

**Passphrase:** four pages of 3 rows of 10 with all of printable ASCII: `abc` (lower case), `ABC` (upper case), `123` (digits and the 19 most used symbols) and `<{~` (the other 11). Space, full stop and hyphen are on both letter pages, and the delete cell in the same corner of every page. Going down from the last row moves to the next page and up from the first to the previous one, so changing pages is one or two presses. Tabs at the bottom say which page you are on.

**ASCII only, on purpose:** BIP39 normalises the passphrase (UTF-8 in NFKD) and not every wallet does it the same way with ñ or accents, so the same phrase could give another wallet with no error in sight. The wallet must be recoverable from any other device. Whoever brings a wallet made on another system with a passphrase in other characters will not be able to type it with the standard interface; there will be other paths for that (by cable or by console).

**Fingerprints:** after loading the seed and the PIN, the **seed's fingerprint** is shown. After the passphrase, the **final wallet's fingerprint**. A PIN or a passphrase with one wrong letter gives another valid, empty wallet, so these checks are indispensable.

**The seed is never shown.** Not on the OLED, nor by video, nor by UART, nor by audio. There is no screen to type it in by hand or to review it: it only comes in by audio (or by UART with confirmation) and only its fingerprint is shown. So an implant reading the screen (the Ledger case of October 2026), a camera or someone looking has nothing to capture.

**A caveat on the PIN and the passphrase with the D-pad:** the grid and its cursor show on the screen. Even with the text masked, an implant reading the screen could rebuild them by seeing which cell is chosen. On their own they are useless without the seed, which never goes through the screen. For the highest level, the PIN and the passphrase also by cable.

## 8. Channel policy

The channel policy is common to every build and is in the [specification](../SPEC.md#4-channels): which datum is accepted over which class of channel, and why. On this board, each port belongs to a class:

| Port | Class |
|---|---|
| Microphone (GP26) | `AIR` |
| INPUT jack (GP27, with detection on GP8) | `CABLE` |
| TTL or RS-232 serial (GP0/GP1) | `CABLE` |
| D-pad, PS/2 keyboard | `LOCAL` |
| Piezo (output) | `AIR` |
| Audio output by cable | `CABLE` |

- As the microphone and the line come in on different pins (§6), the firmware always knows each datum's class.
- Sensitive data arriving by UART is only accepted after pressing the centre button.
- The physical confirmation of a signature is the long press of the centre button (§7).

**Recommended everyday use:**
1. The seed is made outside the signer (with dice, or with another trusted wallet).
2. On an **offline computer**, the ur-tones web tool turns it into tones, protected by a **PIN of 12 characters or more** (the tool can make one), and saves it as a **WAV**.
3. The PIN is written on paper or in a password manager, **always apart from the WAV**.
4. The WAV is kept on an **audio player with no internet connection** (an MP3 player, for instance).
5. To sign: the player is connected **by cable** to the input jack and plays the WAV; the PIN is typed on the signer; the fingerprint is checked.
6. From there, PSBTs go back and forth by air with the wallet, or by UART with a vintage terminal.

The WAV is a copy of the encrypted seed: whoever gets it can try PINs offline, which is why the PIN must be long. And **it must not be the only backup**: players break. The original seed is kept another way (paper, metal, [SeedCraft](https://github.com/ndssigner/seedcraft)…).

## 9. Interface borrowed from vintage devices

- **An ASCII text serial protocol**, in lines ending in CRLF, 8N1, 9600 baud by default and configurable up to 115200. **Any existing terminal program** works with no specific software; the community can write native applications later.
- PSBTs in and out as UR text (`ur:crypto-psbt/...`), the same format as ur-tones, or in base64.
- **Even with a terminal, the final confirmation is always made on the signer**, and the OLED also shows the summary of what is signed. The terminal is a convenience; the reference of trust stays the signer.
- Other devices (the Game Boy's link, their own buses) by PIO on the free GPIOs.
- An example dialogue:

```
> PSBT received: 2 outputs
  1) bc1q...xyz   0.01500000
  2) change       0.00480000
  fee: 1200 sat
Press the signer's centre button to sign
```

The full protocol specification is a separate document, still to be written.

## 10. Composite video (optional)

- **Black and white**, with no colour carrier. The same signal works for PAL and NTSC; the firmware picks 50 or 60 Hz.
- **Two resistors** from GP21 (sync) and GP22 (video) to the RCA, over the TV's 75 Ω load. Indicative values: ~680 Ω and ~220-330 Ω, for ~0 V sync, ~0.3 V black and ~1 V white. *To be tuned on the prototype.*
- **A 40-column text mode**, readable on a CRT TV.
- **The seed is never shown**, so it cannot leak by video. What does show is the PIN and passphrase grid.
- **Smart TVs** can analyse and send what they show (ACR), on any input. If a TV connected to the internet is used, the PIN and the passphrase are better by cable.
- **Analog video signals can be picked up at a distance** with specialised equipment (*van Eck phreaking*). It affects, at most, the PIN and passphrase grid.

## 11. Electrical levels

The board's ports bring their planned protection: the MAX3232 for RS-232 and the BSS138 for PS/2. For whatever is connected through the expansion header, each enthusiast adapts their device. What you need to know:

- **The RP2350's digital pins:** according to secondary sources, they tolerate up to 5.5 V **only with the chip powered**. During boot they can be damaged. A level shifter or series resistors are recommended. *To be confirmed in the datasheet.*
- **The ADC pins (GP26-GP28): never over 3.3 V.**
- **RS-232 (±12 V):** always with a MAX3232 or equivalent.

## 12. Security rules

1. **The seed is never shown** on any channel. Only its fingerprint.
2. **No state:** the seed lives only in RAM. Nothing is written to flash. The firmware zeroes it when done, on cancel and on inactivity; unplugging also loses it, though not instantly (see [§8 of the specification](../SPEC.md#8-seed-storage-and-emergency-wipe)).
3. **Automatic switch-off on inactivity**, which wipes the seed.
4. **Signing needs the signer's own D-pad.** No terminal can sign on its own.
5. **The channel policy** of section 8, enforced by the firmware.
6. **The Pico's USB disabled at boot.** It is only enabled by holding the left arrow at power-on (development mode: updating the firmware and reading it back to verify it).
7. **Deterministic nonces** (RFC 6979 for ECDSA; BIP340 with no mandatory auxiliary randomness). It depends on no random number generator.
8. **Power from a charger or a power bank, not a computer.** Even though the USB-C carries no data, a compromised machine could in theory measure the draw while signing (power analysis).

## 13. Distribution formats

The aim is **to make it cheaper and easier for the public**, not to do business. The risk analysis of each format (tampering, supply chain and **the buyer's privacy**) is in [risk-analysis.md](risk-analysis.md).

| Format | Content | For whom |
|---|---|---|
| **Breadboard + bill of materials** | Wiring diagram, photos and a list of common commercial modules (Pico 2, OLED, microphone module, piezo, buttons, jack and USB-C adapters) | Enthusiasts and prototyping |
| **Perfboard + assembly guide** | The same modules as on the breadboard, **soldered** onto a perfboard with a breadboard's layout. Pico and OLED in female-header sockets | Whoever wants something solid and portable without ordering a PCB |
| **"Shell" PCB** | A board assembled **without the Pico or the OLED**, both in sockets. The user buys them separately and flashes the Pico | Most people |
| **PCB with Pico and OLED** | The same board with the Pico **unflashed** and the OLED, **both in sockets** | Whoever puts convenience first |
| **Bare through-hole PCB** | A board the user orders (or has made) and solders entirely themselves | The most demanding |

**Recommended perfboard:** a prototyping board **with the same layout as a breadboard** (rows of 5 joined points and a central channel; sold as a *solderable breadboard* or *Perma-Proto*), **full size**, with holes plated on both sides. Advantages:
- **The same layout works for breadboard and perfboard:** each part goes in the same row and column. It is tried on the breadboard and, once it works, moved as is to the perfboard.
- The Pico 2 is designed to sit astride the central channel, as on a breadboard. On the perfboard it goes on **female headers**, so it can be taken out.
- **Solid and portable**, more so than a breadboard, and without ordering a PCB from anyone: it is a generic part, with the same privacy as the breadboard format.
- It only needs through-hole parts soldered, the simplest to solder.
- A cheaper alternative: **stripboard** (Veroboard-style), with continuous copper tracks to be cut at certain points. It needs its own placement plan; left to the community.

*To do:* check on the layout that everything fits on a full-size board, and choose specific models that are available now. The assembly and wiring plan will be published with the breadboard one.

**Two PCB designs, one circuit:**

| Design | Components | Basis of |
|---|---|---|
| **PCB-SMD** | Passives and small semiconductors in large SMD sizes (0805/1206), easy to inspect, **assembled by the maker**. Through-hole connectors and sockets | The "shell" PCB and the PCB with Pico and OLED |
| **PCB-THT** | **All through-hole**, meant for hand soldering | The bare through-hole PCB |

- Same schematic, same pin assignment and same firmware. Only the board's physical design changes.
- Factory assembly is much cheaper in SMD; hand soldering is much easier through-hole. Hence two designs.

Notes:
- **"Shell" PCB:** **high-resolution reference photos** of the assembled board will be published so it can be compared.
- **PCB with Pico and OLED:** the user must **always reflash** the Pico with the verified official firmware, even if it comes flashed, and check the OLED against the reference photos.
- **Regulations:** selling assembled electronics may require CE marking (EU), FCC (US), RoHS and electronic-waste handling. *To be looked into before offering the assembled formats.*

**Funding:** PCBWay pays a **10 % commission per order** on projects shared in its community. *To confirm the current terms, and whether it applies to assembled boards.* Other paths: Tindie (direct sale of kits) or Crowd Supply (campaigns in which they make and distribute). The files are also published in the repository, so as not to depend on a single maker.

**Licence and name:**
- Proposed hardware licence: **CERN-OHL-S v2**, which requires modified versions to be open too.
- Being open hardware, anyone can make and sell it without giving anything to the project; that is part of the deal. What sets the official version apart is trust: links from the repository, reference photos and signed releases. Consider registering the name as a trademark to tell it apart from clones.

**Design:** KiCad, published as open hardware. *To do.*

## 14. Firmware modules

| Module | Content |
|---|---|
| `audio` | Goertzel on the ADC, DTMF synthesis by PWM, the piezo's compensation, tone validation |
| `urtones` | Frames, Reed-Solomon, fountain codes, PIN (reuse `c/ur_tones.c`) |
| `crypto` | libsecp256k1, SHA-256/512, BIP39, BIP32 |
| `psbt` | BIP174 parser, segwit v0 (BIP143) and Taproot (BIP341) sighash |
| `policy` | Channel policy: which datum is accepted over which path |
| `ui_oled` | Review screens, PIN and passphrase grids, fingerprints |
| `input` | D-pad on the ADC; PS/2 by PIO (optional) |
| `serial` | Text protocol over UART |
| `video` | B/W composite by PIO (optional) |
| `system` | Boot, disabling USB, development mode, keep-alive load, switch-off on inactivity |

## 15. Validation on virtual hardware

Before publishing the repository with the designs, test in simulation everything that can be. Candidate tools, *to be verified*:

| What | Candidate tool |
|---|---|
| Analog stages (microphone, line, filters, the D-pad ladder, composite video) | SPICE simulation (ngspice, built into KiCad) |
| Firmware with Pico, OLED, buttons and piezo | Wokwi (an online simulator with Pico support). Check whether it supports the RP2350 and analog audio inputs |
| Goertzel decoder and ur-tones | Native tests on the computer fed with WAV files, recorded and synthetic with noise |
| Fuller emulation of the microcontroller | Renode or other emulators. Check RP2350 support |

## 16. To be verified

- Range and reliability of the piezo and the microphone with real phones and laptops.
- Noise of the Pico 2's ADC with the proposed microphone and line stages.
- Tone times shorter than ur-tones v0's with Goertzel.
- The D-pad's resistor-ladder values.
- Total draw and the keep-alive load's parameters with different power banks.
- The RP2350's 5 V tolerance in its datasheet.
- The composite resistors' values and the picture's stability on PAL and NTSC TVs.
- Performance of libsecp256k1, PBKDF2 (BIP39) and the ur-tones PIN on the RP2350.
- The RP2350 erratum on internal pull-downs: use pull-ups.

## 17. Ideas for the future (not a priority)

### SeedCraft seeds entered by console

For whoever keeps their seed with [SeedCraft](https://github.com/ndssigner/seedcraft) (a sequence of objects, for instance the beads of a necklace), being able to enter it on the signer **in console mode**: a PS/2 keyboard with a TV, or a terminal over UART. The seed would then go from the necklace to the signer without passing through any computer. Not by DTMF: for that there is already the seed encoded with ur-tones.

1. **The dictionary first.** The user "names" each symbol, in order, with a name and an abbreviation:

   ```
   0: Red ball (RB)
   1: Black tube (BKT)
   ...
   15: ...
   ```

   The signer checks there are N entries (16 at the recommended size) and that no abbreviation repeats.
2. **Then the sequence**, typing each object's abbreviation as the necklace is read. It is easier and less error-prone than mentally translating each object into its number.
3. **Keeping the rule of never showing the seed.** In the keyless mode, the sequence amounts to the seed in clear, so **what is typed is not shown**: only the progress ("Piece 23/54") and a warning if the abbreviation is not in the dictionary. Blind entry has a safety net: SeedCraft's QR repairs a misread piece, the signer can check row by row of 16 pieces, and at the end it shows the fingerprint.
4. **Keyed mode:** both sequences are entered and the signer combines them internally.

Entry details:
- **A separator between pieces** (space or Enter) and **case-insensitive**, so abbreviations such as "T", "TN" and "TNG" can coexist without ambiguity.
- **Abbreviations with ASCII letters and digits only.** PS/2 keyboards send the key's position, not the character, and the firmware has to assume a layout (English by default, others as an option). Long names may carry any character: they only serve for naming.
- **Each abbreviation is checked at once:** if it is not in the dictionary, the signer warns there and then, even though it does not show what was typed.
- **A command to undo the last piece.**
- **Entry in rows of 16 pieces**, confirmed at the end of each row. One piece too many or too few shifts everything after it; by rows, where the fault lies is found at once.

Notes:
- The SeedCraft sequence counts as an **unencrypted seed** in the channel policy: by PS/2 or by UART with confirmation, never by air.
- **Recommended path: a PS/2 keyboard with a TV**, both inside the trust boundary. By UART, the terminal sees the dictionary and the sequence, that is, the whole seed: better a device that has never been connected to a network.
- The dictionary is entered in each session: the signer keeps nothing.
- It needs SeedCraft's decoder ported to C (from symbols to QR modules, removing the mask, correcting errors and extracting the seed).

## References

- [ur-tones](https://github.com/ndssigner/ur-tones) and its [specification](https://github.com/ndssigner/ur-tones/blob/main/SPEC.md)
- [pico-signer](https://github.com/notemilo/pico-signer): a signer on a Pico 2 with a QR reader over UART (MIT)
- [Raspberry Pi Pico 2](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)
