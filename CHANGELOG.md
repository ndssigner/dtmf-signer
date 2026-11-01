# Changelog

The firmware is a prototype: do not use it with funds.

## Unreleased

- ur-tones v0.5.0. Its receiver is unchanged; the new "fast cable" pace
  (30 ms tone + 10 ms silence) is now tested in the tone receiver's host
  tests, and `dtmf-signer-host tones` can record it (`--timing fast`).
- The listening screen asks for more volume only when reading fails:
  "too quiet to read" when sound comes in with no key at all (above
  -60 dBFS in the DTMF bands; silence is about -67 on the Pico by cable),
  and "Too quiet: volume up" beside "Not a seed" or "Not a PSBT" when a
  frame was lost with its tones below -33 dBFS. While keys are being read
  it says nothing. The USB echo gives each group's tone level.

## v0.1.0-alpha (2026-10-31)

First tagged version: a working prototype, not a product. Nobody
independent has reviewed it.

### Hardware

- The screen is turned 180 degrees by default (`DS_DISPLAY_FLIP=ON`), as
  the breadboard layout mounts it; checked on the board built from the
  layout.
- The breadboard was built from the layout file and its printed template.
  The template carries no QR code on the board itself: Sparrow, reading the
  signer's screen, read the printed one first. The web tool's QR code is on
  the sheet, outside the board. The free end of the board shows the DTMF
  Audio block in its real order and short electrical warnings; the sheet
  carries the parts list and a warning to print at actual size (it also
  prints whole on Letter paper).
  The parts list with rough costs (about US$13 from multipacks, around US$45
  at retail) is in hardware/breadboard.
- The reference build, **Cable and QR**, on one full-size breadboard: a
  Raspberry Pi Pico, a 1.3" SH1106 OLED over SPI, five buttons on one ADC
  pin and a three-way audio terminal block
  ([hardware/breadboard/](hardware/breadboard/README.md)). The layout is a
  DIY Layout Creator project, checked by its connections
  (`tools/check_diy.py`, `tests/test_check_diy.py`), with printable 1:1
  templates for a solderable breadboard (`tools/breadboard_template.py`).
- Licences: software MIT, hardware CERN-OHL-S v2.

### Pico firmware (`ports/pico/`)

- Listens by cable and speaks by QR: a seed by tones with its PIN typed on
  the buttons, its fingerprint, an optional passphrase, the wallet
  fingerprint; the xpub as an animated UR QR code; a PSBT by tones, its
  summary page by page, confirmation by holding OK, the signed PSBT as an
  animated UR QR code. Emergency wipe: hold LEFT for 3 s.
- While listening, the screen says whether tones are coming in
  ("receiving" or "waiting") and counts the keys heard; for a PSBT it also
  shows the last keys and the parts received. A seed's keys are counted,
  never shown. The board's LED gives a short flash per key and is off
  otherwise.
- The QR screens give the number of parts and which one is shown
  ("part 3/7", starting over at 1), and "looping" after the first pass,
  instead of a part number that kept growing.
- Tested on the breadboard: the seed and its PIN (also sent as tones from
  a Nintendo DSi running [NDS-Signer](https://github.com/ndssigner/nds-signer)),
  the xpub read by Sparrow, two PSBTs signed byte for byte as the computer build signs them,
  a fake change rejected, the emergency wipe, the audio output's wiring
  (DTMF keys from a test program, decoded by ur-tones on a phone), and a
  signet transaction from
  Sparrow through ur-tones, signed on the breadboard. Times on the RP2040: PIN and
  seed 4.4 s, derivation 2.6 s, review 0.2–0.3 s, signing 0.1–0.25 s.

### Device logic (`app/`) and core (`core/`)

- The portable core: BIP-39, BIP-32 and the BIP-84 wallet; PSBT v0 analysis
  and signing, streamed through a 64-byte window; the channel policy and
  the signing session (`INV-1` to `INV-6`).
- The device logic, tested on the host with synthesised tones: the tone
  receiver, the screens, the PIN and passphrase picker, the buttons,
  animated QR codes.
- `ports/host`: the computer build (`keys`, `sign`, `tones`), the test bench
  for the core.
- ur-tones v0.4.0, which clears its own copies of seeds and PINs.

### Specification

- The hardware-independent specification and its catalog of capabilities
  and profiles, in English and Spanish; the risk analysis; the audit map
  ([AUDIT.md](AUDIT.md)).
