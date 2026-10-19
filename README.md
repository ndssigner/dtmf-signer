English · [Español](README.es.md)

# DTMF-Signer

A minimal, cheap, open-hardware Bitcoin signer that anyone can build: a
Raspberry Pi Pico 2, a small screen, five buttons, a microphone and a piezo.
It talks to your wallet with telephone tones (DTMF) through
[ur-tones](https://github.com/ndssigner/ur-tones), through the air or by
cable. No camera, no USB data, no secure element and no storage: the seed
lives in memory only while you sign. From the
[NDS-Signer](https://github.com/ndssigner/nds-signer) project.

> **Status: design stage.** There is no firmware or hardware yet. This
> repository holds the hardware specification and the risk analysis, in
> Spanish for now. Do not use it with funds.

## Why

- **Cheap.** About US$13 in parts for the basic board (an estimate), roughly
  a third of a signer that reads QR codes with a scanner module.
- **No black boxes.** No camera or QR module with closed firmware: the whole
  path of the signal, from the microphone to the decoder, is our own code.
- **Parts from anywhere.** Generic, common parts. The tones are decoded in
  software, so there are no hard-to-find DTMF decoder chips.
- **Build it yourself.** On a breadboard, or on a PCB where you plug in a
  Pico 2 you bought and flashed yourself. No hardware-wallet vendor in the
  middle.
- **Friends with old machines.** A vintage computer or calculator can lend it
  a screen and a keyboard over a serial port. It also works fine on its own.

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
3. The unsigned PSBT arrives as tones.
4. The signer shows every output, amount and fee on its screen. You page
   through all of them, and hold a button to sign.
5. The signed PSBT goes back as tones.

Unplug it, and the seed is gone.

## The basic board

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

## Ways to build it (planned)

- **Breadboard + bill of materials**, with common off-the-shelf modules.
- **"Shell" PCB:** factory-assembled, without the Pico or the screen. You buy
  those yourself, flash the Pico and plug them in.
- **The same PCB with the Pico in its socket**, unflashed, for convenience.
  Reflash it before use.
- **A through-hole PCB** to order bare and solder by hand.

## Roadmap

- [ ] Software tone decoder on a Pico 2, tested with ur-tones
- [ ] Schematic, breadboard guide and bill of materials
- [ ] Two PCB layouts: SMD (factory-assembled) and through-hole
- [ ] Simulation of the analog stages and the firmware
- [ ] Firmware: audio, ur-tones, PSBT, signing, screens
- [ ] Serial protocol for vintage terminals
- [ ] Independent review

## Notice

DTMF-Signer is an experimental design and is offered as is, without any
warranty. There is no working device yet, and no one independent has
reviewed it. We are not liable for any loss of funds or any other harm, nor
for the use anyone makes of this design.

## In this repository

- [`docs/hardware-minimo.md`](docs/hardware-minimo.md): the hardware
  specification: board, pins, audio, screens, channel policy, distribution
  formats (Spanish).
- [`docs/analisis-riesgos.md`](docs/analisis-riesgos.md): the risk analysis:
  supply chain, buyer privacy, firmware, use and physical threats (Spanish).

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

Not decided yet. The hardware is planned under
[CERN-OHL-S v2](https://ohwr.org/cern_ohl_s_v2.txt).
