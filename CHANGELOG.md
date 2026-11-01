# Changelog

The firmware is a prototype: do not use it with funds.

## Unreleased

- A new picker for the PIN and the passphrase: pages of 10 columns moved
  through with the arrows (the PIN on one page, the passphrase's printable
  ASCII on four: abc, ABC, 123 and the rarer symbols), OK to add, OK held to
  finish, a delete cell, and a magnifier that shows the chosen character
  three times larger. The entry is shown in the clear instead of as stars.
- A reproducible Pico build: `tools/build-pico.sh` fetches the pinned
  Pico SDK and Arm toolchain (checked by SHA-256) and, on Linux x86_64 as
  in the CI, gives the same UF2 byte for byte from the same tag (other
  hosts give the same program laid out slightly differently, as Arm's
  toolchain builds differ per host); CI keeps the UF2, so releases can
  carry it. Flashing is also explained the simple
  way: copy the UF2 onto the Pico's drive in BOOTSEL mode.
- At power-up the signer asks whether you flashed it yourself (a cheap
  device may be a present from someone with other intentions) and About
  shows the version, the profile, the catalog's capabilities with this
  build's ticked, and the repository as a QR code. The risk analysis has
  the gifted device as a risk of its own.
- A clearer screen: g, j, p, q and y reach below the line everywhere;
  icons replace the text hints; settings show their value between arrows
  and change with LEFT and RIGHT; fingerprints are drawn twice as large;
  dots show the steps of loading a seed; "Working..." says what it is
  doing; errors are inverted with a hint of what to check. The listening
  screens say what they expect, show the level with a mark where tones are
  read well and, for a PSBT, a bar of the parts.
- The review is now a row of cards: one per output (what it is first,
  `Send to` or, inverted, `Change, yours`/`To this wallet`; the amount
  twice as tall; the whole address in groups of 4, never split across
  pages as it could be before), the fee and totals, the warnings when there
  are any, and the confirmation with `Hold OK: sign`. In addresses g, j, p,
  q and y get a tail below the line, so g and q no longer look like 9 in
  the 5x7 font. The arming rules do not change.
- [docs/screens.md](docs/screens.md) (and in Spanish): every screen of the
  Pico build and how to move through them, with pictures drawn by the
  firmware's own code (`tests/c/screens.c`, `tools/render_screens.py`).
- ur-tones v0.5.0. Its receiver is unchanged; the new "fast cable" pace
  (30 ms tone + 10 ms silence) is now tested in the tone receiver's host
  tests, and `dtmf-signer-host tones` can record it (`--timing fast`).
- The listening screen asks for more volume only when reading fails:
  "Too quiet to read" and "Turn the volume up" when sound comes in with no
  key at all (above -60 dBFS in the DTMF bands; silence is about -67 on the
  Pico by cable), or when a frame was lost with its tones below -33 dBFS.
  While keys are being read it says nothing. The USB echo gives each
  group's tone level.

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
