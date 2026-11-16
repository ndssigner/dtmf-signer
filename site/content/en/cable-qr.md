The reference build: it **listens by cable and speaks by QR**. The tones
reach it through a headphone cable from the phone or computer that holds
your wallet; the signed transaction comes back as QR codes on its own small
screen, which your wallet reads with its camera. There is no microphone and
no speaker, so it cannot hear the room or be heard in it.

It is the build that works today, on a breadboard or a perfboard, with a
Raspberry Pi Pico, a small OLED screen, a few push buttons and a handful of
resistors and capacitors.

## What it does

- Receives your seed as tones, protected by a PIN, and never shows it: only
  its fingerprint.
- Shows your wallet's account as a QR, for your wallet to watch.
- Receives a transaction to sign, shows you on its screen what it spends and
  where it sends it, and signs only when you confirm with its buttons.

## What it does not do

- It keeps nothing: when it is switched off, the seed is gone.
- No Taproot or multisig yet.
