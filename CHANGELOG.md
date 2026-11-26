# Changelog

The firmware is a prototype: do not use it with funds.

## v0.4.0-alpha (2026-11-26)

- A third build: the **Nintendo DS**, air only, with nothing to build (the
  console is the hardware). The firmware is in `ports/nds/`: the microphone
  hears the wallet, the top screen shows the signed PSBT or xpub as a large
  animated QR and the bottom one the same screens as the Pico, doubled; A is
  OK, B goes back, holding B for 3 s or closing the lid wipes everything and
  powers the console off; the stylus types the PIN and the passphrase on
  their grid and taps the footer's arrows, but never confirms a signature.
  No Wi-Fi code is linked; the radio's power is cut at start. The release
  carries `dtmf-signer-ds.nds` beside the UF2s, in the same `SHA256SUMS`. `tools/build-nds.sh`
  builds it in Docker, reproducibly (CI job `nds`); `tools/build-nds.sh
  --autotest` with `tools/dev/nds_e2e.sh` checks it in melonDS. Signs end to end
  on a DSi XL, by air at ur-tones' "Fast cable" pace; testers wanted for
  the other consoles. It is in the catalog as `PROFILE-NDS`
  and on the website; a build with no hardware stages shows "No parts to
  build: the console is the hardware".

- A build with no cable input is allowed (SPEC §4.2 and §7, "air only"): a
  seed by air is accepted after one warning shown before listening, and the
  PIN and the passphrase are typed on the device. `ds_app_set_channel`
  tells the app whether its tones come by cable (the default) or by air.

- Wanted: a voice pack in your language. `docs/voice-packs.md` (and its
  Spanish translation) says how to make one, step by step: the voice, the
  phrases written as they must sound, the number grammar, listening, the
  checks; CONTRIBUTING, the README and the website's downloads ask for it.
  `tools/voice/make_pack.py` takes any language with a voice in
  `voices.json`, refuses a pack with untranslated phrases (listing them),
  and `--wav-dir` writes every phrase as the signer will play it. Where no
  free voice exists, a contributor can record the phrases and share them
  (`"recorded"` in `voices.json`): they get the same treatment as a
  synthesised voice. The website names a pack's language even when the
  site is not translated into it.

- `tools/install_pex.py` puts the PEX on a Pico with the language packs you
  choose: with the firmware and the packs in one folder, `python3
  install_pex.py` checks every file (a pack for another firmware version, a
  damaged one, more than three), then copies the firmware and the languages
  onto the Pico, asking for BOOTSEL before each, and wipes earlier packs.
  Two copies, because the Pico's boot ROM does not write one UF2 holding
  both (tried on a Pico). Python 3.8, nothing else. The website's
  downloads offer it with the three steps.

## v0.3.0-alpha (2026-11-23)

A second build, the cheapest: the **Phone Exchange (PEX)**, a tone
telephone as the whole interface. Still a prototype, still not reviewed by
anyone independent.

- The voice firmware (`app/ds_voice`): the signer powers a telephone's line,
  speaks in the earpiece and hears the keys. A seed through the handset
  (with a PIN of 12 characters or more) or typed on the keypad, its
  fingerprint said, every output, the change and the fee read aloud (the
  whole address on request), and the signature only by a short hang-up,
  never a key; the signed PSBT and the account go back as ur-tones through
  the earpiece. Amounts in bitcoin or sats, a settings menu, the help said
  once if you hesitate. A wallet's data tones never count as keys, and
  neither does the signer's own voice.
- One sound language: every screen that offers keys opens with a notice
  motif and says each key as an option after the option motif; a key taken
  for work sounds a short motif, then a wait motif that keeps playing while
  the work runs.
- Voice packs in Speex (fixed point, about a fifth of the ADPCM size):
  English built in, Spanish as a pack; no prompt makes a key at any
  alignment.
- The PEX in the website's simulator (`/pex/`), with a test wallet by the
  handset, and on the Raspberry Pi Pico: `DS_FIRMWARE=PEX` in `ports/pico`
  gives `dtmf-signer-pex.uf2`, with libspeex decoder only. Measured at
  125 MHz: signing 130 ms, byte for byte as the computer build signs; the
  seed 2.6 s (4.5 s with a 12-character PIN). While the handset is held to
  the wallet, the Pico's LED shows the reception: a flash per tone, a long
  one per part, steady once it is all in.
- `hardware/phone/`: the PEX on a breadboard, with a whole telephone on an
  RJ11 socket, its line fed from the USB through a current source, its parts
  list and a step-by-step build with checks. Its layout in DIY Layout
  Creator, checked by its connections, with a printable 1:1 template; the
  layout spreads out the parts of the tested breadboard and has not been
  built as it stands. The perfboard is still to come.
- `tools/build-pico.sh pex` builds the PEX's firmware the way the published
  one is built, and the CI builds it with Cable and QR's: releases from now
  on carry `dtmf-signer-pex.uf2` too, in the same SHA256SUMS. Until one does,
  the website's downloads say how to build it and how to install a
  language pack.
- Schematics in KiCad for both builds (`hardware/breadboard/kicad/`,
  `hardware/phone/kicad/`), shown in their guides and on the website, where
  the `.kicad_sch` can be downloaded.
- Documentation in English first everywhere: the minimal hardware
  specification is now in English (`docs/minimal-hardware.md`), and the
  Spanish translations take the English file names.

## v0.2.0-alpha (2026-11-01)

Still a prototype, still not reviewed by anyone independent. The first
version with the firmware attached (`dtmf-signer-pico.uf2`, built by the CI
on Linux x86_64, and its SHA-256), for those who would rather flash it than
build it; anyone can build the same tag and compare.

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
