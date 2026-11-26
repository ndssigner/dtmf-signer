English · [Español](README.es.md)

# Nintendo DS

A DTMF-Signer with nothing to build: the console is the hardware. Its
microphone hears your wallet's tones, its two screens and its buttons are
the interface, and the signed transaction goes out as an animated QR for
your wallet's camera. The profile is
[`PROFILE-NDS`](../../docs/catalog.md#profile-nds--nintendo-ds-air-only);
the firmware, how to build it, load it and use it:
[`ports/nds/`](../../ports/nds/README.md).

**Air only:** the DS has no input but its microphone, so a seed reaches it
through the room, after a warning (§4.2 and §7 of the
[specification](../../SPEC.md#42-what-is-accepted-where)). Use a long PIN.

## Which consoles

- **DS and DS Lite:** a slot-1 flashcart that loads `.nds` files.
- **DSi, DSi XL and 3DS:** their homebrew loader runs it, in DSi mode by
  default; no flashcart.

**Status:** tried end to end on a DSi XL (a seed and a PSBT by air, the
signed PSBT read by a wallet, the stylus, the wipe, the lid); the other
consoles are still to be tried:
[testers are wanted](../../ports/nds/README.md#testers-wanted).
Do not use it with funds.

There is no parts list, schematic or layout, and no soldering.
