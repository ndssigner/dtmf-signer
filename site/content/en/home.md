# Sign Bitcoin with telephone tones, on a device you build yourself

DTMF-Signer is a **specification**: it says what a Bitcoin signer must do,
and leaves how to build it open. It talks with telephone tones (DTMF), so it
needs no camera and no fancy screen. A cheap microcontroller, a few parts
from an electronics shop, or parts saved from the scrapyard, are enough.

## Why

- **Anyone, anywhere.** The parts are cheap and easy to find all over the
  world, new or salvaged from old telephones and gadgets.
- **More people.** Different builds suit different people: one of them, in
  the works, is used like a telephone exchange, with voice prompts and the
  keypad, and could serve people who cannot see a screen.
- **Nobody between you and your signer.** A commercial signer can be
  tampered with or intercepted on its way to you. A signer you build
  yourself, from generic parts bought anywhere, gives an attacker no
  shipment to target.

## How it started

Blame a pop song. In ["Catch Catch"](https://www.youtube.com/watch?v=NOiyDlWl534),
K-pop singer Yena uses the beeps of a telephone keypad as part of the
music. Somewhere between the second chorus and the third replay, a very
unmusical question came up: if a song can carry DTMF, why not a Bitcoin
transaction? Add the freshly finished [NDS-Signer](https://github.com/ndssigner/nds-signer),
which had just taught a Nintendo DSi to sign, and DTMF-Signer was the
obvious next step. So yes: this signer exists because someone could not
stop replaying a song. No regrets.

## The builds

Each build is one profile of the specification, with its parts list, its
layout and, soon, a simulator that runs the real firmware in your browser.
