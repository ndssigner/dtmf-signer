# Changelog

No release yet. The firmware is a prototype: do not use it with funds.

## Unreleased

### Hardware

- The screen is turned 180 degrees by default (`DS_DISPLAY_FLIP=ON`), as
  the breadboard layout mounts it; checked on the board built from the
  layout.
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
- Tested on the breadboard: the seed and its PIN (also sent as tones from
  a Nintendo DSi running [NDS-Signer](https://github.com/ndssigner/nds-signer)),
  the xpub read by Sparrow, two PSBTs signed byte for byte as the computer build signs them,
  a fake change rejected, the emergency wipe. Times on the RP2040: PIN and
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
