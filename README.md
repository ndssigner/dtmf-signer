English · [Español](README.es.md)

# DTMF-Signer

A minimal, cheap, open-hardware Bitcoin signer that anyone can build. The
full board: a Raspberry Pi Pico, a small screen, five buttons, a microphone
and a piezo; the reference build that works today, **Cable and QR**, leaves
out the microphone and the piezo: it listens by cable and answers with QR
codes.
It talks to your wallet with telephone tones (DTMF) through
[ur-tones](https://github.com/ndssigner/ur-tones), through the air or by
cable. No camera, no USB data, no secure element and no storage: the seed
lives in memory only while you sign. From the
[NDS-Signer](https://github.com/ndssigner/nds-signer) project.

**The key idea:** the seed arrives as tones, made by ur-tones on an offline
computer, and **is never shown, on any channel**: only its fingerprint.
From there, you choose: keep everything on audio, for the strongest air
gap, or drive it from a vintage terminal over a serial port. DTMF-Signer
and ur-tones are designed to go together.

**Many ways to build it.** DTMF-Signer is a [specification](SPEC.md) that
can be built in many ways: from a Raspberry Pi Pico, the reference, down to
parts recovered from a scrapyard (an 8-bit microcontroller, a DTMF decoder
chip from an old telephone), knowing what each build gives up and how long
it takes. The [catalog](docs/catalog.md) lists the capabilities and the
profiles that combine them. The prototype uses a Pico (RP2040); the Pico 2
comes later.

> **Status: working prototype.** The reference build, Cable and QR, runs on
> a breadboard: it loads a seed by tones with its PIN, shows the
> fingerprints, exports the xpub and returns signed PSBTs as QR codes that
> Sparrow reads. Its layout and a printable 1:1 template are in
> [`hardware/breadboard/`](hardware/breadboard/README.md). Tones through the
> air (microphone and speaker), the PCBs and an independent review are still
> to come. Do not use it with funds.

## Motivation

- **A signer for everyone.** It works on its own, with any phone or computer
  that can play and hear sound. No camera, no special cable, no app store.
- **Low cost.** About US$13 in parts for the basic board (an estimate),
  roughly a third of a signer that reads QR codes with a scanner module.
- **Easy, foolproof DIY.** Plug a Pico into a ready-made board, with no
  soldering, or wire common modules on a breadboard. Generic parts you can
  buy anywhere: the reference decodes the tones in software, so it needs no
  hard-to-find DTMF decoder chips. A slower build can use one salvaged from
  an old telephone.
- **Against supply-chain attacks and fake entropy.**
  - In October 2026, Ledger devices sold through resellers turned out to
    carry a hidden implant that read the recovery phrase from the screen
    during setup and sent it out over the mobile network; losses are
    estimated in the tens of millions of dollars
    ([CoinDesk](https://www.coindesk.com/tech/2026/10/13/ledger-says-spy-chip-inside-cryptobilis-ledger-devices-transmitted-wallet-keys-over-mobile-network)).
    Here you build the signer yourself from generic parts, the
    microcontroller is one you bought, flashed and can pull out of its
    socket, and every part of the board is open and in plain sight. And
    the seed is never shown on its screen: an implant reading it would
    find nothing to steal.
  - Also in 2026, a build error in Coldcard's firmware made it generate
    seeds from a weak software fallback instead of its hardware random
    number generator, and wallets created that way were swept
    ([crypto.news](https://crypto.news/coldcard-firmware-bug-drains-38-million-bitcoin/)).
    DTMF-Signer never generates seeds, and its signatures use deterministic
    nonces: it has no random number generator that could fail.

It also has **no black boxes** (no camera or QR module with closed
firmware: the whole path of the signal, from the microphone to the decoder,
is our own code), and it **likes old machines**: a vintage computer or
calculator can lend it a screen and a keyboard over a serial port.

## How it works

```
  wallet + ur-tones tool               DTMF-Signer
 ┌──────────────────────┐   tones   ┌───────────────────────┐
 │ unsigned PSBT  ──────┼─────────► │ microphone → decoder  │
 │                      │           │ screen + 5 buttons    │
 │ signed PSBT  ◄───────┼───────────┤ piezo ← tone encoder  │
 └──────────────────────┘   tones   └───────────────────────┘
```

1. The seed arrives as tones, encrypted with a PIN. The PIN is typed on the
   signer and never played. A passphrase, if you use one, comes by cable or
   is typed on the signer.
2. The signer shows the seed's fingerprint, to check it is the right one.
   The seed itself is never shown.
3. The unsigned PSBT arrives as tones.
4. The signer shows every output, amount and fee on its screen. You page
   through all of them, and hold a button to sign.
5. The signed PSBT goes back as tones.

Unplug it, and the seed is gone. From step 3 on, the PSBTs can travel as
tones, or through a vintage terminal over the serial port: your choice.

## Everyday use

1. Generate the seed outside the signer: with dice, or with another wallet
   you trust.
2. On an **offline computer**, the ur-tones web tool turns it into tones,
   protected by a **PIN of 12 characters or more** (it can make one up for
   you), and saves them as a **WAV** file.
3. Write the PIN down on paper or keep it in a password manager, **always
   apart from the WAV**.
4. Keep the WAV on an **audio player with no internet connection**, such as
   an MP3 player.
5. To sign: plug the player **by cable** into the signer's input jack and
   play the WAV. Type the PIN on the signer and check the fingerprint.
6. Then sign: PSBTs come and go through the air with your wallet, or
   through a vintage terminal.

The WAV is an encrypted copy of your seed: whoever gets it can try PINs
offline, so make the PIN long. And it **must not be your only backup**:
players break. Keep the seed itself some other way too (paper, metal,
[SeedCraft](https://github.com/ndssigner/seedcraft)…).

## The reference build: Cable and QR

![The breadboard layout: the Pico on the left, the screen at the top, the five buttons below it and the audio terminal block on the right](docs/images/breadboard-layout.png)

What works today, on one full-size breadboard
([layout and template](hardware/breadboard/README.md)):

| Part | What for |
|---|---|
| Raspberry Pi Pico (RP2040) | The microcontroller. You buy it and flash it |
| 1.3" SH1106 OLED, 128 × 64, over SPI | Reviewing what you sign, entering PIN and passphrase, showing QR codes |
| Five buttons on one ADC pin | Moving around, typing, confirming |
| Audio terminal block (IN, COM, OUT) | Tones by cable from a phone or a computer, through a headset cable |

It listens by cable and speaks by QR: the seed and the PSBTs come in as
tones, and the xpub and the signed PSBTs go out as animated QR codes on its
screen. A dozen resistors and capacitors complete it.

## The full board (planned)

| Part | What for |
|---|---|
| Raspberry Pi Pico 2 in a socket | The microcontroller. You buy it and flash it |
| 1.3" OLED, 128 × 64 | Reviewing what you sign, entering PIN and passphrase |
| 5-button pad | Moving around, typing, confirming |
| Microphone and piezo | Tones through the air |
| Input-only audio jack | A cable channel for sensitive data |
| USB-C, power only | No data lines at all |

Optional ports come as **unpopulated footprints**, for whoever wants them:
RS-232 or TTL serial, PS/2 keyboard, black-and-white composite video, line
out, batteries, and a header with the free GPIO pins.

## Security in brief

- **The seed is never shown.** Not on the screen, the video output, the
  serial port or the audio. Only its fingerprint.
- **Stateless.** The seed lives in memory only. Nothing is written to flash.
  Unplugging, switching off or leaving it idle wipes it.
- **PIN and passphrase never go through the air.** Sensitive data comes by
  cable or is typed on the signer, and never leaves it, on any channel.
- **You review it on the signer.** Every output is shown on its own screen,
  and nothing is signed without its physical button.
- **No random number generator to get wrong.** Signatures use deterministic
  nonces (RFC 6979, BIP 340).
- **No secure element, by design.** There is nothing to extract when it is
  off.
- **Buying ties you to Bitcoin.** Any order for a ready-made board leaves
  your name and address in someone's database. Generic parts bought
  separately for the breadboard version are the most private path.

The whole analysis, by distribution format and by use, is in
[`docs/analisis-riesgos.md`](docs/analisis-riesgos.md) (Spanish).

## Ways to build it

- **Breadboard** (available: the [layout](hardware/breadboard/README.md)),
  with common off-the-shelf modules.
- **Perfboard** (available: the same layout and its printable 1:1
  template): the same modules soldered onto a prototyping board laid out
  like a breadboard, with the Pico and the screen on socket headers. The
  breadboard layout carries over as is. Sturdier and portable, with no PCB
  to order.
- **"Shell" PCB** (planned): factory-assembled, without the Pico or the screen. You buy
  those yourself, flash the Pico and plug them in.
- **The same PCB with the Pico and the screen in their sockets**, the Pico
  unflashed, for convenience. Reflash it before use.
- **A through-hole PCB** to order bare and solder by hand.

## Roadmap

- [x] Hardware-independent specification and capability catalog
- [x] Core: seed to fingerprints and xpub, and a computer build (`ports/host`)
- [x] Core firmware, tested with ur-tones recordings
- [x] Software tone decoder on the Pico reference, tested with ur-tones
- [x] Breadboard layout (DIY Layout Creator), checked by its connections, and a printable 1:1 template for the perfboard
- [ ] Schematic, step-by-step assembly guide and bill of materials
- [ ] Two PCB layouts: SMD (factory-assembled) and through-hole
- [ ] Simulation of the analog stages and the firmware
- [x] Core and computer build: PSBT analysis, signing and the signing session (`ports/host`)
- [x] Firmware for the Pico (`ports/pico`): audio, ur-tones, PSBT, signing, screens. Tested on the breadboard prototype: seed by tones with its PIN, fingerprints, the xpub as a QR read by Sparrow, two PSBTs signed byte for byte as the computer build does, a fake change rejected, and the emergency wipe
- [ ] Serial protocol for vintage terminals
- [ ] Porting guide: an invitation to other platforms
- [ ] Independent review

## Notice

DTMF-Signer is an experimental design and is offered as is, without any
warranty. The firmware is tested only on a breadboard prototype, it is not
for real funds, and no one independent has reviewed it. We are not liable for any loss of funds or any other harm, nor
for the use anyone makes of this design.

## In this repository

- [`SPEC.md`](SPEC.md) ([Spanish](docs/es/especificacion.md)): the
  specification: what every build must do, whatever it is made of.
- [`docs/catalog.md`](docs/catalog.md) ([Spanish](docs/es/catalogo.md)): the
  capabilities and the profiles that combine them.
- [`docs/hardware-minimo.md`](docs/hardware-minimo.md): the hardware
  specification of the full Pico board: pins, audio, screens, distribution
  formats (Spanish).
- [`docs/analisis-riesgos.md`](docs/analisis-riesgos.md): the risk analysis:
  supply chain, buyer privacy, firmware, use and physical threats (Spanish).
- [`core/`](core/): the portable C99 core: SHA-512, HMAC, PBKDF2, BIP39,
  BIP32 and the BIP84 wallet, from a seed to its fingerprints and xpub; PSBT
  analysis and signing, the channel policy and the session.
- [`ports/host/`](ports/host/): the computer build, `dtmf-signer-host keys`:
  from a UR, tones or a WAV to the fingerprints, the xpub and the descriptor;
  and `dtmf-signer-host sign`: a PSBT (base64, binary, a UR or a WAV of its
  tones) in, the summary on standard error, "yes" on standard input, the
  signed PSBT out. `dtmf-signer-host tones seed|psbt … --out FILE.wav` is a
  test tool that records a seed or a PSBT as tones (cable or air timing;
  keypad seeds, `--keypad`, always go at air timing, 80/80/600 ms, since
  ur-tones asks for at least 80 ms + 80 ms there, and `--timing cable` is
  refused with them).
  Warning: its arguments (seed, PIN, passphrase) are visible to other
  processes and are kept in the shell history; use only test seeds with it.
- [`ports/pico/`](ports/pico/): the Raspberry Pi Pico (RP2040) firmware, a
  CMake project of its own:
  `cmake -S ports/pico -B build-pico -DPICO_SDK_PATH=<pico-sdk>`
  gives `dtmf-signer-pico.uf2`. Line-in on GP27,
  buttons on a resistor ladder on GP28, an SH1106 OLED over SPI (pins in
  `board.h`). `-DDS_DISPLAY=SSD1306_SPI` and `SSD1306_I2C` build for the
  SSD1306 but are not tested on hardware; `-DDS_DISPLAY_FLIP=ON` rotates the
  screen. The USB serial is output only (the public echo and `stack-free`).
  See the [Pico README](ports/pico/README.md).
- [`hardware/breadboard/`](hardware/breadboard/README.md): the breadboard
  layout of the reference build (DIY Layout Creator, CERN-OHL-S v2) and its
  printable 1:1 templates. `tools/check_diy.py` reads the layout and prints
  what each part connects; `tools/breadboard_template.py` makes the
  templates.
- [`vectors/`](vectors/): test vectors (JSON), public test seeds only: keys and PSBTs.
- [`CONTRIBUTING.md`](CONTRIBUTING.md): how to propose changes, add to the
  catalog or port DTMF-Signer.

## Sister projects

- [NDS-Signer](https://github.com/ndssigner/nds-signer): an air-gapped
  signer for the Nintendo DSi, signing PSBTs from QR codes or tones.
- [ur-tones](https://github.com/ndssigner/ur-tones): PSBTs and seeds as
  telephone tones, by cable or through the air.
- [SeedCraft](https://github.com/ndssigner/seedcraft): a seed kept as the
  order of some objects, read back by hand or with a web tool.

## Donate

DTMF-Signer is free and has no funding. Donations help keep it going:

<table>
<tr><td align="center"><img src="docs/images/donate-bitcoin.png" width="180" alt="Bitcoin"><br><b>Bitcoin</b><br><code>bc1qx5snc0wlc8cg9gwxhyx27y6pkru8rnngyq7uja</code></td>
<td align="center"><img src="docs/images/donate-lightning.png" width="180" alt="Lightning"><br><b>Lightning</b><br><code>ndssigner@coinos.io</code><br><sub>LNURL (for wallets without Lightning addresses):<br><code>LNURL1DP68GURN8GHJ7CM0D9HX7UEWD9HJ7TNHV4KXCTTTDEHHWM30D3H82UNVWQHKUERNWD5KWMN9WGQ8XE42</code></sub></td></tr>
</table>

The same addresses as [NDS-Signer](https://github.com/ndssigner/nds-signer#donate).

## License

- **Software** (the core, `app/`, the ports, the tools and the tests) and
  its documentation: the [MIT licence](LICENSE).
- **Hardware** (circuits, board layouts, printable templates and, later,
  PCB designs, such as [the breadboard layout](hardware/breadboard/README.md)
  and [docs/hardware-minimo.md](docs/hardware-minimo.md)):
  the [CERN-OHL-S v2](LICENSE-HARDWARE), a strongly reciprocal open hardware
  licence. Whoever makes and distributes a modified design must publish its
  sources too.

Third-party parts keep their own terms: libsecp256k1 and qrcodegen (in
`third_party/`) are under the MIT licence, and
`ports/pico/pico_sdk_import.cmake` comes from the Raspberry Pi Pico SDK
(BSD 3-Clause). The 5×7 screen font (`app/ds_font5x7.h`) is not an original
drawing: its glyphs (0x20–0x7E) are the classic 5×7 character-generator
design, widely published.
