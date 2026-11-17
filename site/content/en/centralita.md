A build with **no screen and no buttons: a tone telephone is the whole
interface**. The signer powers the telephone's line, as an exchange does.
You type on the keypad; it speaks to you in the earpiece, reads you what a
transaction spends and where it sends it, and signs only when you hang up
for a moment and pick up again. Your wallet's tones reach it by the
handset's microphone, and the signed transaction goes back into the
earpiece as tones.

It is designed and simulated, not yet built: a Raspberry Pi Pico, a small
boost converter and a current source for the line, an RJ11 socket and any
tone telephone with a handset.

You can try it below, in the simulator: the firmware itself, compiled to
WebAssembly, with a telephone and a test wallet held to its handset.

## What it does

- Receives your seed by the handset (with a PIN of 12 characters or more,
  typed on the keypad) or typed on the keypad, and never says it: only its
  fingerprint.
- Reads you every output of a transaction, the change and the fee; the
  whole address on request.
- Speaks English; other languages are separate packs (Spanish here).

## What it does not do

- It keeps nothing: when it is switched off, the seed is gone.
- No passphrase, Taproot or multisig.
- A telephone may remember the last keys typed: the signer says so.
