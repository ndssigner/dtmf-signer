English · [Español](docs/es/especificacion.md)

# DTMF-Signer specification (draft v0)

**Status: draft for discussion.** Nothing here is final, there is no
working device yet, and no one independent has reviewed it. Do not use it
with funds.

DTMF-Signer is a Bitcoin signer that talks with telephone tones (DTMF)
through [ur-tones](https://github.com/ndssigner/ur-tones). This document
says **what any DTMF-Signer must do, whatever it is built from**: a
Raspberry Pi Pico, an 8-bit microcontroller, a vintage processor, a DTMF
decoder chip salvaged from an old telephone. How a given build does it is
described elsewhere: the [catalog](docs/catalog.md) lists the capabilities
and the profiles that combine them, and
[`docs/hardware-minimo.md`](docs/hardware-minimo.md) describes the Pico
board (in Spanish).

- **The goal:** anyone can build a signer they trust, with new parts or, in
  the worst case, with parts recovered from a scrapyard, knowing exactly
  what each build gives up and how long it takes.
- **The specification and its test vectors are the product.** The
  reference firmware is one implementation. A port may be a rewrite, even in
  assembly, guided by this document and checked against the vectors (§10).
- **ur-tones:** tones, frames, keypad mode and the seed PIN follow the
  ur-tones format version `0` (its `SPEC.md`, release `v0.3.0` or later).
  What DTMF-Signer needs from ur-tones is proposed there, in public, through
  issues and pull requests, under its own rules.

The words MUST, MUST NOT, SHOULD and MAY are used as in RFC 2119.

## 1. Principles

1. **The specification and the vectors come first.** A build is conforming
   when it does what this document says and reproduces the vectors.
2. **The signer never hides what it did not check.** A build checks on the
   signer everything it can afford to. Whatever it cannot check, it MUST
   say so on its own output (for example, "fee NOT verified"), and the
   catalog says which outside check replaces it. The user then carries that
   check, knowingly.
3. **Times are published as measured.** No one should be surprised by a
   slow processor. A time that has not been measured is written as "not
   measured".
4. **Stateless by default.** The seed lives in RAM only. Keeping it is an
   optional capability, with its risks spelled out (§8).

## 2. Core operations

Every build MUST provide all of these.

| ID | Operation | Detail |
|---|---|---|
| `OP-SEED` | Load the seed | In ur-tones tones, encrypted with a PIN or not. Held in RAM only. |
| `OP-PASSPHRASE` | Take a passphrase | The signer MUST accept one; using it is up to the user. ASCII only. |
| `OP-FINGERPRINT` | Present fingerprints | On the verification output (§5). The seed's, after loading it (as ur-tones defines it); the final wallet's, after the passphrase. |
| `OP-XPUB` | Return the xpub | Of the BIP84 account `m/84'/0'/0'` (and its test-network equivalent `m/84'/1'/0'`), with the master key fingerprint. |
| `OP-SIGN` | Sign a P2WPKH PSBT | Processed in parts, never held whole in RAM (§2.1). |
| `OP-WIPE` | Wipe | RAM set to zero when done, on cancel, and on the emergency wipe (§8). |
| `OP-TESTNET` | Test networks | testnet and signet, to practise without real money. |

**Address types.** Only **single-signature P2WPKH** (BIP84, ECDSA) is
mandatory. Taproot and multisig are optional capabilities
(`BTC-TAPROOT`, `BTC-MULTISIG`). The catalog says plainly which builds
cannot reach them, and a build without them can still take part in Bitcoin.

### 2.1 Signing

1. The PSBT is read **in parts**, so that a build with little RAM can sign
   a transaction with several inputs.
2. The signer works out **the change by itself**: an output is change only
   if its derivation path and its key are the signer's own (`INV-5`).
3. It computes the fee (§6), and presents a **summary** — amounts,
   destinations, fee, which output is change — on a verification output it
   controls (§5).
4. It waits for **physical confirmation** on the signer.
5. It signs with `SIGHASH_ALL` only, with deterministic signatures
   (RFC 6979: no random number generator is needed), and returns the PSBT.

## 3. Invariants

A conforming build MUST NOT break any of these, on any channel, in any
state.

1. `INV-1` — The seed never leaves the signer and is never shown. Only its
   fingerprint.
2. `INV-2` — The PIN and the passphrase never leave the signer, not even as
   an echo.
3. `INV-3` — Nothing is signed without physical confirmation on the signer.
4. `INV-4` — Nothing is signed before its summary has been presented on a
   verification output the signer controls (§5).
5. `INV-5` — No output is treated as change unless the signer has checked
   that it is its own.
6. `INV-6` — Nothing is presented as verified that the signer has not
   verified (principle 2).

## 4. Channels

### 4.1 Classes

Channels are grouped by **what they expose**, not by port.

| Class | What it is | Examples |
|---|---|---|
| `AIR` | Broadcast: anyone nearby can hear | Microphone, buzzer, speaker |
| `CABLE` | Point to point with another device, which knows what it sends | Line-in jack, UART, line-out |
| `LOCAL` | Part of the signer, with no other device behind it that knows the data | Buttons, PS/2 keyboard, a salvaged phone keypad (§4.3), the signer's own screen |

Every piece of data MUST reach the core **tagged with its class**. If a
build cannot tell where something came from (for example, a microphone and
a line input on the same pin), it MUST treat it as `AIR`, the most
restrictive class.

### 4.2 What is accepted where

| Data | `AIR` | `CABLE` | `LOCAL` |
|---|---|---|---|
| PSBT, xpub, summaries, fingerprints | ✅ | ✅ | — |
| Seed encrypted with a PIN | ⚠️ with a warning, and a PIN of at least `12` characters | ✅ recommended | ❌ (would mean showing it) |
| Unencrypted seed | ❌ | ✅ | ❌ except SeedCraft console entry, blind |
| PIN | ❌ | ✅ | ✅ |
| Passphrase | ❌ | ✅ | ✅ |

- **Sensitive data (the unencrypted seed, the PIN, the passphrase) travels
  only by `CABLE` or `LOCAL`, even when it travels as DTMF.**
- **Sensitive data over UART is accepted only after physical
  confirmation**: the signer asks, for example, "Accept passphrase received
  over UART?". A connected terminal can never slip sensitive data in
  without the user knowing.
- **A cable protects the path, not the source.** The device that sends the
  data knows it. Sensitive data is best sent from a device that has never
  been online: an old phone in airplane mode, an audio player, a vintage
  computer.
- **Recordings are copies.** An audio file with the seed or the passphrase
  deserves the care of a paper backup; if it is kept, encrypted with a long
  PIN.
- **Live over a cable, the passphrase may go in the clear.** Encrypting it
  only makes sense in stored recordings, with a salt of its own (for example
  `ur-tones/passphrase-pin/v0`) and padded to a fixed length. Even so, a
  wrong PIN gives text that does not look like text, which lets an attacker
  check a guess quickly: an encrypted passphrase stored next to the
  encrypted seed weakens both. *Pending in the ur-tones specification
  (§12).*

### 4.3 A salvaged phone keypad

A DTMF keypad from a landline telephone, wired to the signer, is `LOCAL`:
there is no other device behind it. The PIN is typed as on old mobile
phones, several presses per key for A–Z, and it goes through the same tone
decoding as the seed (in software, or with a decoder chip). With such a
keypad, the minimum is a single confirmation button, or a key of the keypad
itself.

**Warning:** many telephones keep the last number dialled, and some have a
phone book: the PIN would stay in the telephone. Use the keypad alone, a
telephone without memory, or overwrite it by dialling something else.

## 5. Verifying what is signed

If the signer does not show the amounts and destinations itself, the
connected computer could lie about the transaction, and the user would sign
blind. So the requirement is: **the summary leaves the signer by a path
that the connected device does not control.** Accepted forms:

- **The signer's own screen** (RECOMMENDED): OLED, an HD44780 character LCD,
  seven-segment displays.
- **A terminal over UART, or a television** driven by the signer.
- **Several listeners over the air** (§5.1).
- **A readout in tones or beeps:** a last resort, slow and error-prone.

What never counts is the connected device's own presentation of the
transaction.

### 5.1 Several listeners

The air is a broadcast: every listener hears the same transmission at the
same time. The signer transmits the summary (amounts, destinations, fee,
which output is change), and several listeners compatible with ur-tones
decode and show it.

- **The guarantee:** if N **independent** listeners show the same thing,
  it is correct as long as **at least one of them is honest**. An attacker
  who controls one causes a mismatch, which gives it away; to deceive the
  user, it would have to control them all.
- **Independent means really independent:** different software or
  hardware, for example a phone and a computer, offline. The same app on two
  phones with the same system can fail, or be compromised, together. The
  device that made the PSBT may be one of the listeners, but it does not
  count as the honest one.
- **The ur-tones web tool counts as a listener**, checked against its
  SHA-256. The hash MUST come by another path than the download: a signed
  tag of the repository, the release notes, or several sources at once.
  The hash checks the tool, not the browser or the system: the same tool in
  two browsers of the same laptop is not two independent listeners.
- **Injected tones:** a loudspeaker nearby could play false tones while the
  signer transmits. The summary will carry a short check code that the
  signer also presents by its own means (LEDs, beeps) and that the
  listeners show. *Detail pending (§12).*

With this, a build with no screen at all can still verify what it signs.
Its own screen remains the recommendation.

## 6. The SegWit v0 fee attack

In SegWit v0 (BIP143) each signature commits to the amount of **its own**
input, not to the others'. With two inputs or more, a malicious computer can
lie about the amounts across two signing sessions: the user sees a normal
fee and ends up signing a huge one.

- **One input:** the declared amount (`witness_utxo`) is enough. A lie only
  produces an invalid signature.
- **`2` or more inputs:** the full previous transaction
  (`non_witness_utxo`) of every input is REQUIRED. The signer hashes it
  while it reads it in parts, checks that it matches the outpoint being
  spent, and takes the amount from it.
- A build that cannot afford this MUST declare the capability
  `BTC-INPUT-AMOUNTS` absent. It then shows "fee NOT verified" (`INV-6`),
  and before the transaction is broadcast the user checks the real fee with
  another device or program than the one that made the PSBT: the ur-tones
  web tool, a wallet on another device, their own node. The attack only does
  harm once the signed transaction is broadcast, so that check catches it.

## 7. Minimum hardware

From §2–§5, every build needs at least:

- **A `CABLE` input** (line-in jack or UART): the recommended path for the
  encrypted seed, the only one for the unencrypted seed, and valid for the
  PIN and the passphrase when there is no `LOCAL` input.
- **A confirmation button**, or a key of a `LOCAL` keypad.
- **An output to return data:** tones through the air, line-out or UART.
- **A verification output** of those in §5.

**An example of the floor:** a microcontroller, an input jack (with an
MT8870 decoder chip if the processor is slow), one push button and a
buzzer; verification with two independent listeners; the PIN through a
salvaged phone keypad or by cable. This is the profile `PROFILE-SCRAP`.

## 8. Seed storage and emergency wipe

- **Stateless is the default.** The encrypted recording of the seed is at
  once the backup and the way to load it.
- **Keeping the seed is optional** (`STORE-SEED`), encrypted, with its
  risks stated: memory of doubtful origin, extraction from a stolen device,
  erasure that cannot be trusted.
- **A build that keeps the seed MUST have an emergency wipe:**
  - triggered physically, and working without a screen: a button
    combination held for a few seconds, or a dedicated switch or jumper;
  - it overwrites the seed and then checks that it reads back as zero. It
    does not just mark it free. Beware of memories that spread writes on
    their own (wear levelling).
- **Stateless:** wiping means setting the RAM to zero. Cutting the power
  does not guarantee an instant erasure.

## 9. Capabilities and profiles

A build is described as **the core (§2–§8) plus a set of capabilities**.
Profiles are named, ready-made sets of capabilities, to make choosing
easier. The [catalog](docs/catalog.md) describes each one in detail.

**Every capability is described the same way:** what it does; what it
needs (pins, peripherals, RAM, program memory); its time on each processor
class; its risks; the vectors that check it.

**Processor classes**, to give times without tying them to one chip:

| ID | Class |
|---|---|
| `CPU-8` | 8-bit: AVR, 8051, Z80, 6502, under 20 MHz |
| `CPU-32S` | Modest 32-bit: Cortex-M0 and similar, under 50 MHz |
| `CPU-32` | 32-bit: RP2040, RP2350 and equivalents |

**Capabilities:**

| Group | IDs |
|---|---|
| Tone input | `TONES-SW` (in software), `TONES-CHIP` (decoder chip) |
| Input channels | `IN-MIC`, `IN-LINE`, `IN-UART`, `IN-PS2`, `IN-PHONEPAD`, `IN-BUTTONS` |
| Data outputs | `OUT-AIR`, `OUT-LINE`, `OUT-UART` |
| Verification | `VERIFY-SCREEN`, `VERIFY-TERMINAL`, `VERIFY-TV`, `VERIFY-LISTENERS` |
| Storage | `STORE-SEED` |
| Optional Bitcoin | `BTC-TAPROOT`, `BTC-MULTISIG`, `BTC-PATHS`, `BTC-INPUT-AMOUNTS` |
| Several microcontrollers | `SPLIT-MCU` |
| Other | `SEEDCRAFT-CONSOLE` |

**Profiles:**

- `PROFILE-SCRAP` — **Scrapyard minimum:** an 8-bit microcontroller, a
  salvaged MT8870, an input jack, a push button and a buzzer; several
  listeners; no storage.
- `PROFILE-PICO-REF` — **Pico reference:** the current prototype, on a
  Raspberry Pi Pico.
- `PROFILE-PICO-FULL` — **Full Pico:** the board in
  [`docs/hardware-minimo.md`](docs/hardware-minimo.md).

## 10. Test vectors

*Planned.* Test vectors will live in `vectors/`, as JSON, like ur-tones'.
**Public test seeds and testnet PSBTs only.**

- **Every core operation:** decrypting the seed with a PIN; BIP39 to seed,
  with and without a passphrase; fingerprints; the xpub; the summary and the
  signature of a PSBT.
- **Malicious PSBTs**, which a build MUST reject or flag: fake change (our
  path, someone else's key), change to another address type, the wrong
  network, a sighash other than `SIGHASH_ALL`, an out-of-proportion fee,
  duplicate or malformed fields, false amounts with `2` or more inputs and
  no `non_witness_utxo`.

## 11. Security considerations

- The core rules are the invariants (§3) and the channel policy (§4); the
  rest of this document follows from them.
- No secure element is assumed, and a build may be made of parts of unknown
  origin. Stateless operation (§8) is what keeps a stolen or doubtful
  device from holding anything worth extracting.
- What a build cannot check, it says (principle 2), and the catalog gives
  the outside check.
- The full risk analysis, including the supply chain and the buyer's
  privacy, is in [`docs/analisis-riesgos.md`](docs/analisis-riesgos.md)
  (in Spanish).

## 12. Open questions

- **A check code against injected tones** when verifying with several
  listeners (§5.1).
- **A "verification summary" frame type in ur-tones**, to be proposed there
  through an issue and a pull request.
- **The salt and padding of an encrypted passphrase** in ur-tones (§4.2).
- **The ur-tones C library seen from 8 bits:** whether it builds with
  compilers such as SDCC or cc65, which lacks 64-bit integers, and whether
  it needs floating point anywhere.
- **The data format of the catalog**, once a tool needs to read it.
