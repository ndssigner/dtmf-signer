English · [Español](README.es.md)

# DTMF-Signer on the Nintendo DS

The firmware for the console: it **listens through the air** (the DS's own
microphone) and **speaks by QR**. The bottom screen shows the same 128 × 64
screens as the Pico, doubled; the top one shows the signed PSBT or the xpub
as a large animated QR (4 parts a second), and the bottom one a small copy.
It reuses `core/` and `app/` unchanged; this directory holds only what
surrounds them (screens, buttons, microphone, the lid). The profile is
[`PROFILE-NDS`](../../docs/catalog.md#profile-nds--nintendo-ds-air-only).
At start it switches the DS radio's power off and, in DSi mode, holds the
DSi's wireless chip in reset and turns its light off; no Wi-Fi code is
linked. In DS mode a DSi or 3DS keeps its wireless light on: that mode
cannot switch it off.

**Tried on one console so far:** a DSi XL, in DSi and in DS mode, end to
end: a seed with its PIN and a PSBT by air from a laptop next to it, the
signed PSBT read by a wallet from the top screen's QR, the stylus, the
emergency wipe and the lid. By air it took ur-tones' fastest pace ("Fast
cable", 30 ms tone + 10 ms gap). With a television on in the room the seed
still loaded but the PSBT failed; with it off, the PSBT loaded too. A DS, DS
Lite or 3DS are still to be tried. Do not use it with funds yet, and never
put a real seed in an emulator.

## Consoles

DS and DS Lite need a slot-1 flashcart that loads `.nds` files; DSi, DSi XL
and 3DS run it from their homebrew loader, in DSi mode by default (DS mode
works too, with the wireless light left on).

## Get it

The releases from v0.4.0-alpha carry `dtmf-signer-ds.nds`, built by the
project's CI from the release's tag, with its SHA-256 in `SHA256SUMS`. Taking
it means trusting that build; to check it, build the same tag yourself and
compare:

```bash
git clone --recurse-submodules https://github.com/ndssigner/dtmf-signer
cd dtmf-signer
git checkout v0.4.0-alpha    # the release to build; skip it for the latest code
tools/build-nds.sh           # gives build-nds/dtmf-signer-ds.nds and its SHA-256
```

It needs git and Docker. The build runs in a pinned devkitARM image and is
reproducible: the same commit gives the same `.nds`, byte for byte, on any
machine (CI's build of a commit matched a local one). Copy the `.nds` to
your flashcart or loader as any homebrew.

An emulator end-to-end test (melonDS):
`tools/build-nds.sh --autotest`, then
`tools/dev/nds_e2e.sh` with the path of melonDS.app (or of its binary); it prints `AUTOTEST PASS`.
The autotest build is for development and is never released.

## Controls

- **D-pad** moves; **A** is OK; **B** goes back (the LEFT of the Pico). To
  confirm a signature or finish a picker, hold A.
- **Stylus:** tap the PIN or passphrase grid (a cell adds it, a tab turns
  the page) or an arrow hint of the bottom row ("◀ back", "▲▼ move"). OK
  is never tapped: finishing and signing stay a held A. The review's "Tap
  OK, then hold OK" means the A button; stylus taps never confirm.
- **Emergency wipe:** hold B for 3 s, on any screen. Everything is zeroed
  and the console powers off (it does not restart).
- **Closing the lid** wipes everything and powers the console off. During a
  long computation (a few seconds) it takes effect when that finishes.

The PIN and the passphrase are typed on the console.

## Air only

The DS has no cable input, so a seed reaches it by air. Before listening
the signer says: "Seed by air … Anyone who records these tones gets the
seed, or can try PINs if it is short. Use a long PIN." This is the
air-only build that [§4.2 and §7](../../SPEC.md#42-what-is-accepted-where) of the
specification allow. The PSBT and the xpub are not secret, and the PIN and
the passphrase never travel by air.

## Timings (emulator)

Measured in melonDS, DS mode: **an emulator, not a console.** The seed PIN
5.5 s, the wallet's derivation 2.9 s, a PSBT's review 0.3 s, signing 0.1 s,
starting a QR 0.4 s. Nothing is measured on hardware.

## Testers wanted

**We are looking for people with a real DS, DS Lite, DSi or 3DS** to try it:
does it load, does the microphone hear a wallet's tones, does a wallet read
its QR, does the lid wipe. Tell us what you saw in an issue of the
repository, with the console model and how you loaded it. Use only a test
seed.
