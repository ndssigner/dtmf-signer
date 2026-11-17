**A tone telephone is the whole interface.** The signer powers the
telephone's line, as an exchange does. You type on the keypad; it speaks to
you in the earpiece, reads you what a transaction spends and where it sends
it, and signs only when you hang up for a moment and pick up again.

Your wallet's tones reach it by the handset's microphone, and the signed
transaction goes back into the earpiece: the tones speak
[ur-tones](https://github.com/ndssigner/ur-tones/blob/main/SPEC.md), which you can hear and decode with its
[web tool](https://ndssigner.github.io/ur-tones/).

## It does

- Receives your seed by the handset (with a PIN of 12 characters or more)
  or typed on the keypad, and never says it: only its fingerprint.
- Reads you every output of a transaction, the change and the fee; the
  whole address on request.
- Speaks English; other languages are separate packs (Spanish here).

## It does not

- Keep anything: when it is switched off, the seed is gone.
- Passphrases, Taproot or multisig.
- Stop a telephone remembering the last keys typed: the signer warns you.
