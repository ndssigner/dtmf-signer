**The console is the whole signer.** Nothing to build: a Nintendo DS (or a
DSi, or a 3DS) loads the firmware like any homebrew. Its microphone hears
your wallet's tones, its screens and buttons are the interface, and the
signed transaction goes back as an animated QR for your wallet's camera.

The tones speak [ur-tones](https://github.com/ndssigner/ur-tones/blob/main/SPEC.md), which you can hear and decode with its
[web tool](https://ndssigner.github.io/ur-tones/).

## It does

- Receives your seed through the air, after one warning, with the PIN typed
  on the console; it never shows the seed: only its fingerprint.
- Shows what a transaction spends and where it sends it, and signs only when
  you hold A.
- Wipes everything when you close the lid or hold B for 3 seconds.

## It does not

- Keep anything: when it is switched off, the seed is gone.
- Take tones by cable: air only, so use a long PIN.
- Taproot or multisig, yet.
- Proven on every console, yet: tried on a DSi XL only, and testers are
  wanted.
