It **listens by cable and speaks by QR**. Your wallet's tones reach it
through a headphone cable; the signed transaction comes back as QR codes on
its small screen, for your wallet's camera. No microphone, no speaker: it
cannot hear the room or be heard in it.

The tones speak [ur-tones](https://github.com/ndssigner/ur-tones/blob/main/SPEC.md); you can hear and decode them with its
[web tool](https://ndssigner.github.io/ur-tones/).

## It does

- Receives your seed as tones, protected by a PIN, and never shows it: only
  its fingerprint.
- Shows your wallet's account as a QR, for your wallet to watch.
- Shows on its screen what a transaction spends and where it sends it, and
  signs only when you confirm with its buttons.

## It does not

- Keep anything: when it is switched off, the seed is gone.
- Taproot or multisig, yet.
