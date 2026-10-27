# Auditing DTMF-Signer

A map for anyone who wants to review DTMF-Signer's security. What it must
never do is in the [specification's invariants](SPEC.md#3-invariants)
(`INV-1` to `INV-6`); what is protected and from whom, in the
[risk analysis](docs/risk-analysis.md). Report findings as in
[SECURITY.md](SECURITY.md): privately for anything that can lose funds or
leak a seed, a PIN or a passphrase.

All of DTMF-Signer's own code is new and nobody independent has reviewed
it. It is small: the portable core (`core/`) and the device logic
(`app/`) are about 5,600 lines of C99 (comments included, the generated word list and the font table left out), with no `malloc`, no floating point
and no global mutable state; the Pico port (`ports/pico/`) is the hardware
around them. Elliptic curve work is libsecp256k1; tones, frames, the seed
PIN and multi-part URs are ur-tones.

## Getting started

- **Host build and tests:**
  `cmake -S . -B build && cmake --build build && ctest --test-dir build`,
  then `python3 -m unittest discover -s tests`. The C tests cover the core
  and `app/` (the whole device flow runs on the host, fed with synthesised
  tones and simulated buttons: `tests/c/test_app.c`); the Python tests run
  the computer build (`ports/host`) against the vectors.
- **Vectors** (`vectors/`): keys from the BIP-39/BIP-32 test seeds, and PSBTs
  (`psbt-v0.json`: 13 to sign, 10 to reject) generated with embit, an
  independent implementation: `tools/gen_psbt_vectors.py --check`
  regenerates them and compares.
- **Pico firmware:** `cmake -S ports/pico -B build-pico -DPICO_SDK_PATH=…`
  (Pico SDK 2.3.1); see [ports/pico/README.md](ports/pico/README.md).
- **Hardware:** the reference build's breadboard layout,
  [hardware/breadboard/](hardware/breadboard/README.md);
  `tools/check_diy.py` prints what each of its parts connects.

## Where to look, by priority

### 1. Secrets and wiping (`INV-1`, `INV-2`)

The seed's entropy, the PIN, the passphrase, the derived keys and the tone
groups that carry a seed must never leave the signer, and must be cleared
(`ds_wipe`, which the compiler cannot remove) on every path, errors
included.

| Path | What it does |
| :--- | :--- |
| `core/ds_wipe.c` | The wipe every other file relies on |
| `core/ds_bip39.c`, `core/ds_bip32.c`, `core/ds_wallet.c` | Entropy → seed (PBKDF2-HMAC-SHA512) → master and account keys; fingerprints |
| `core/ds_session.c` | The session: holds the entropy (to derive again with a passphrase) and the account key; `ds_session_wipe` |
| `core/ds_sign.c` | Derives one private key per input, signs, clears the key, the sighash and the secp256k1 context |
| `app/ds_rx.c` | Tones in: a group that holds a seed is kept until the PIN is typed, then cleared |
| `app/ds_app.c` (`clear_all`, the PIN and passphrase jobs) | The device flow: PIN and passphrase typed on the buttons, temporary wallet for the seed fingerprint, wipe on discard, on errors and on the emergency wipe |
| `app/ds_ui.c` (`ds_picker_wipe`) | The PIN and passphrase picker's buffer |
| `ports/pico/main.c` (`scrub_buffers`) | Before the reboot that follows a wipe: the audio ring, the sample buffers, the framebuffer and the dead part of the stack, since SRAM survives a watchdog reset |
| `third_party/ur-tones/c/ur_tones.c` | The seed PIN (PBKDF2-HMAC-SHA256) and seed decoding; since v0.4.0 it clears its own copies |

Things to check: a secret that survives in a struct, a stack temporary or a
buffer after the screen that used it is left; a path (error, cancel,
emergency wipe during a heavy job) that skips a wipe; anything secret in
the USB echo (`ds_app_take_echo`), which must carry public data only.

### 2. Signing what the user saw (`INV-3` to `INV-6`)

| Path | What it does |
| :--- | :--- |
| `core/ds_psbt.c`, `core/ds_tx.c` | PSBT v0 analysis, streamed through a 64-byte window: which inputs are ours, which output is change (our path **and** our key's script), the fee, the input amounts (with two or more inputs, each previous transaction is hashed and checked against its outpoint), sighash, network, limits; the review summary text |
| `core/ds_sign.c` | The second pass: re-hashes the PSBT and refuses if it is not the one reviewed; signs only our inputs; BIP-143 sighash |
| `core/ds_session.c`, `core/ds_policy.c` | The state machine (no signature without the review of that very PSBT and a confirmation after it) and the channel policy (what may arrive by air, cable or locally) |
| `app/ds_app.c` (pager, `armed`, `DS_APP_ARM_MS`) | Confirmation: a long press of OK counts only on the last page, after every page was shown, and only after a short press of any button made at least 300 ms (tick time) after the review was handed to the port, so a hold or tap carried over from before the review never confirms it (under the port contract in `app/ds_app.h`: draw before the next tick, button events within 200 ms of their samples). A signed PSBT that would not fit a QR code is refused before confirmation |
| `app/ds_ui.c` (`ds_pager_*`) | Wrapping the summary on the 21 × 8 screen without losing a character |
| `app/ds_qr.c` | The signed PSBT and the xpub as animated UR QR codes |

The PSBT rules, one by one, are in `core/ds_psbt.h`'s header comment and in
[SPEC.md §2.1](SPEC.md#21-signing) and [§6](SPEC.md#6-the-segwit-v0-fee-attack).

### 3. Untrusted input

Everything that arrives as tones is attacker-controlled.

| Path | What it does |
| :--- | :--- |
| `third_party/ur-tones/c/ur_tones.c` | Tone detection, frames, Reed-Solomon, fountain codes (multi-part URs), CBOR |
| `app/ds_rx.c` | What is accepted while listening for a seed or for a PSBT, and what is refused |
| `core/ds_psbt.c`, `core/ds_tx.c` | Lengths, compact sizes, duplicate keys, counts and limits (`DS_MAX_INPUTS`, `DS_PSBT_MAX_KEYS`, `DS_PSBT_PREV_MAX`) |
| `ports/pico/adc_ring.c`, `ports/pico/main.c` (`pump_samples`) | The DMA audio ring: samples are checked after they are copied, and a lap of the DMA is reported as lost audio, never delivered |

### 4. The Pico port

| Path | What it does |
| :--- | :--- |
| `ports/pico/main.c` | The loop (draw before the next tick, button events within 200 ms of their samples: the confirmation rule depends on it, see `app/ds_app.h`), the USB echo (output only: the firmware never reads from USB; the USB stack still enumerates and accepts the host's bytes), the stack watermark |
| `ports/pico/CMakeLists.txt` | 8 KB stack over both scratch banks (core 1 is never started, and the link fails if anything lands there), USB reset to bootloader disabled, libsecp256k1 with small tables |
| `ports/pico/board.h`, `ports/pico/display_*.c` | Pins, the button ladder's thresholds, the screens |

### 5. The hardware

[hardware/breadboard/](hardware/breadboard/README.md): the reference
build's layout. The line input has a DC-blocking capacitor and a series
resistor but no clamp diodes; the full board's input protection is in
[docs/hardware-minimo.md](docs/hardware-minimo.md) (Spanish).

## Pinned dependencies

Git submodules in `third_party/` (exact commits: `git submodule status`):

| Dependency | Version |
| :--- | :--- |
| libsecp256k1 | v0.8.0 (`6e2c8bc`) |
| ur-tones | v0.4.0 (`02b58be`) |
| QR Code generator (Nayuki) | v1.8.0 (`720f62b`) |

Build only, not in the repository: Pico SDK 2.3.1 (with TinyUSB) and the
Arm GNU toolchain for the firmware; embit 0.8.0 for the PSBT vectors;
ReportLab 5.1.0 for the printable templates. Licences:
[THIRD_PARTY.md](THIRD_PARTY.md).

## Known gaps

- No independent audit.
- No fuzzing harness in the repository. The PSBT parser and signer were
  mutation-fuzzed informally while they were written (about 7,500 mutated
  PSBTs, under ASan and UBSan, with no finding), but no harness or results
  are kept in the repository; the tone receiver and ur-tones' frame
  and fountain decoding have not been fuzzed here.
- The Pico firmware's build has not been checked for reproducibility, and
  there are no signed releases yet.
- No inactivity timeout: a loaded seed stays in RAM until the user wipes it
  (hold LEFT 3 s, or the menu) or unplugs the board.
- The USB echo is for development: the computer that powers the board over
  USB sees the descriptor (xpub and fingerprint), the signed PSBTs, and
  state and timing lines. Public data, but
  it tells that computer which wallet this is. Power the board from a
  charger or a power bank when that matters.
- The audio output (OUT) is wired but not used by the firmware yet, and not
  tested. The SSD1306 screen drivers build but are untested on hardware.
- The confirmation rule has one accepted limit: tapping another button
  while OK is held splits the hold, so deliberate use of the buttons on the
  visible review can still confirm (`app/ds_app.h`).
- libsecp256k1's context is never randomised on the signer (no blinding;
  `core/ds_secp.h`): its code is constant time, but side channels on the
  Pico have not been studied.
- The 5×7 screen font is the classic, widely published character-generator
  table; its provenance is stated, not its licence.
