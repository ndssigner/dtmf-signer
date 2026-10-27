# DTMF-Signer — Risk analysis

*[Leer en español](es/analisis-riesgos.md)*

**Status:** living document. Reviewed after the **Cable and QR** prototype
(the breadboard Pico that listens by cable and answers with QR codes). It
goes with the [specification](../SPEC.md),
[hardware-minimo.md](hardware-minimo.md) (Spanish) and the audit map
([AUDIT.md](../AUDIT.md)).
**Ratings:** qualitative (low / medium / high), with no external audit.

---

## 1. What we protect, and from whom

**Assets:**
- The **seed**, the **PIN** and the **passphrase**.
- The **integrity of what is signed**: that the user signs exactly the
  transaction they think they sign.
- The **user's identity as a bitcoin holder**: their name, their postal
  address, and the link between them and the signer.

**Adversaries considered:**
- Someone nearby who listens to or records the audio.
- A compromised computer, phone or terminal.
- A malicious or compromised manufacturer, distributor or carrier.
- Whoever gets hold of a leaked customer database.
- An attacker with physical access to the signer or to the user (coercion
  included).

**Out of scope:** attackers with a lab who attack the chip (fault
injection, invasive reading). Since the signer stores nothing, such attacks
only make sense during a session.

## 2. Distribution and privacy risks

### 2.1 By format

| Risk | Breadboard + bill of materials | Bare PCB | "Shell" PCB (no Pico, no OLED) | PCB with Pico and OLED |
|---|---|---|---|---|
| Tampered hardware | **Very low**: generic parts, the seller does not know what they are for | **Low**: a board with no parts, easy to inspect | **Medium-low**: the manufacturer fits parts; few and visible | **Medium**: the Pico or the OLED could be swapped or modified |
| Tampered firmware | Low: the user flashes it | Low | Low | **High if used as delivered**; low if the user reflashes it |
| Counterfeit Pico or chip | Low if bought from an official distributor | Low | Low (the user buys it) | Medium: depends on where the manufacturer got it |
| **Identity linking** | **Low**: millions of hobbyists buy these parts | Medium: the order carries the project's name | **High**: an identifiable order with a postal address | **High**: and it is plainly a finished product |
| Targeted interception of the parcel | Low: several generic parcels | Low | Medium | Medium-high: an identifiable parcel, ready to use |
| Skill needed | Medium: wiring | High: soldering | Low | Very low |

The **perfboard** laid out like a breadboard has the same profile as the
breadboard format (generic parts, very private). Only the skill changes:
medium, because through-hole parts must be soldered. It is the format of
today's reference build
([hardware/breadboard/](../hardware/breadboard/README.md)); the PCBs are
still planned.

### 2.2 Identity linking: the most forgotten risk

Any order identifiable as "a bitcoin signer" leaves a **name, postal
address, email and phone number** tied to being a likely bitcoin holder in
the databases of the manufacturer, the shop, the payment processor and the
carrier. **Assume those databases may leak some day.**

Precedent: in 2020 Ledger's customer database leaked, with hundreds of
thousands of postal addresses. Phishing campaigns, extortion and threats of
physical attacks on the buyers followed.

Mitigations:
- **Prefer the breadboard format, or buy the generic parts separately**,
  mixed with other electronics purchases. A Pico and an OLED give nobody
  away.
- **Neutral names in the manufacturer's listings** (for example, "Pico audio
  I/O board") and **silkscreen with no reference to bitcoin**. The
  repository says what it is; the order page need not.
- **Delivery to a pickup point or locker**, when possible.
- **Do not reuse** the email or phone of exchanges or bitcoin-related
  accounts.
- The signer **stores nothing**: whoever finds it only learns it is a board
  with a Pico and a screen.

### 2.3 Tampering and interception

Mitigations:
- **High-resolution reference photos** of each PCB version, to compare
  against.
- **The Pico and the OLED always in sockets**, never soldered: they can be
  taken out, inspected and replaced.
- **Inspect the OLED module** in the formats that come with it fitted. The
  implant in the tampered Ledger devices of October 2026 read precisely the
  screen. An OLED should carry only the panel, its controller and a few
  passives; any extra chip is suspect. To send out what it reads, an
  implant would also need its own radio and power, because the signer has
  no wireless link at all. When in doubt, replace it with an OLED bought
  separately.
- **Always reflash** with the official firmware, however it arrives.
- **Buy only from the repository's links** for the fitted formats.
  Third-party clones are legitimate (open hardware), but there is no way to
  know how they were built.

## 3. Firmware risks

| Risk | Rating | Mitigation |
|---|---|---|
| Malicious firmware downloaded from a fake source | Medium | Build from source; **to do**: signed releases and a reproducible build with published hashes |
| Firmware altered in the Pico's flash | Medium | Read the flash back in BOOTSEL mode and compare its hash; reflash when in doubt |
| Cryptographic implementation errors | Medium | libsecp256k1 (constant-time code, widely reviewed; the signer does not randomise its context, so there is no blinding); vectors from an independent implementation (embit); an external audit before version 1.0 |
| Weak random number generator | **Not applicable** | Deterministic nonces (RFC 6979); the signer never generates seeds |
| A malformed or hostile PSBT (false lengths, repeated keys, too many inputs) | Medium | Streamed reading with limits; the rules fail with a clean error; the second pass hashes the PSBT again and does not sign if it is not the one reviewed. **To do**: fuzzing, in the repository, of the tone receiver and the frame decoding |
| A press that began before the summary was shown confirms the signature | Low | A long press of OK counts only on the last page, after every page was shown, and only after a short press of any button made at least 300 ms (tick time) after the summary was handed to the screen, so a hold or tap carried over from before never confirms. Accepted limit: tapping another button while OK is held splits the hold, so deliberate use of the buttons on the visible summary can still confirm |
| Secrets left in RAM after a wipe | Low | Wiped on every path; before the reboot, the audio, the screen and the dead part of the stack are zeroed, because SRAM survives the reset; ur-tones v0.4.0 clears its own copies |
| Audio corrupted silently if the loop falls behind | Low | Samples are checked after they are copied from the DMA ring; what the DMA overwrote is counted as lost and never delivered |

## 4. Risks in use

| Risk | Rating | Mitigation |
|---|---|---|
| Someone records the encrypted seed sent through the air and tries PINs offline | Medium | A PIN of **12 characters or more**; the seed **by cable** when possible; the PIN never sounds. The Cable and QR build only listens by cable |
| The device that sends the seed or the passphrase is compromised | Medium | Send sensitive data from a device **disconnected from the network**; the cable protects the path, not the source |
| The wallet or the computer makes a PSBT with a swapped address | Medium | A **full review on the screen** before signing; the change output is checked |
| The computer that powers the prototype over USB sees its echo: the descriptor (xpub and fingerprint), the signed PSBTs, and state and timing lines | Low (privacy, not funds) | The echo is output only and never carries secrets, but it tells that computer which wallet this is. Power the board from a charger or a power bank; **to do**: a build option with no echo |
| Any camera that sees the screen reads its QR codes | Low | Only public data goes out (the xpub, signed PSBTs); never the seed, the PIN or the passphrase |
| A compromised UART terminal shows false data or injects sensitive data | Medium | The summary also on the screen; **physical confirmation** required to sign and, in builds with a terminal, to accept sensitive data |
| Stored audio recordings are copies of the seed | Medium | Treat them as paper backups; encrypted with a long PIN |
| Loss or theft of the player holding the seed's WAV | Medium | A PIN of **12 characters or more** (ur-tones can make up a 60-bit one), kept **apart** from the WAV; the WAV is not the only copy of the seed |
| An implant that reads the screen (the Ledger case) | Low | **The seed is never shown**: there is nothing to capture. The PIN and passphrase picker is visible; for the highest level, PIN and passphrase by cable |
| A camera or someone watching the screen | Low | The seed is never shown; the PIN and the passphrase are masked (stars), except the character being chosen |
| An encrypted passphrase stored with the encrypted seed lets an attacker check a PIN guess quickly | Medium | The passphrase live by cable or typed on the signer; do not store it encrypted next to the seed |
| A typo in the PIN or the passphrase gives another valid, empty wallet | High (human error) | Always compare the **fingerprints**: the signer shows the seed's right after the PIN, before asking for the passphrase, and it can be discarded |
| The signer is left on with the seed loaded | Medium | The emergency wipe (hold LEFT 3 s) or unplugging; **to do**: an inactivity timeout that wipes the seed |
| Power analysis when powered from a compromised computer | Low | Power it from a charger or a power bank |
| Emissions of the analog video signal picked up at a distance | Low | The seed is never shown; at most the PIN and passphrase picker is visible |
| A smart TV that analyses what it shows (ACR) | Low, if the video output is used | The seed is never shown; with a TV attached, PIN and passphrase by cable |
| Noise or the wrong volume | Medium (a failure, not a leak) | Reed-Solomon correction; by cable, an output volume around 30 % (measured on the prototype) |
| Verification with several listeners that are not really independent (the same app, the same system, or the device that made the PSBT) | Medium | Listeners on different software or hardware, offline; the one that made the PSBT does not count as the honest one ([specification, §5.1](../SPEC.md#51-several-listeners)) |
| A salvaged phone used as a keypad keeps the PIN in its redial memory or its phone book | Medium | Use only the keypad, a phone with no memory, or clear it by dialling something else ([specification, §4.3](../SPEC.md#43-a-salvaged-phone-keypad)) |
| A build that stores the seed leaves it in a memory that cannot be wiped with certainty | Medium | Stateless by default; if it stores it, encrypted and with an emergency wipe that overwrites and checks ([specification, §8](../SPEC.md#8-seed-storage-and-emergency-wipe)) |
| A build that does not check the amounts of several inputs signs a huge fee (the SegWit v0 fee attack) | Medium | The signer shows "fee NOT verified" and each input's declared amount; check them on another device **before confirming**, because afterwards it is too late: the signed PSBT goes back to the device that made it, which can broadcast it itself; be wary of being asked to sign the same inputs again ([specification, §6](../SPEC.md#6-the-segwit-v0-fee-attack)). The Pico reference checks the amounts |

## 5. Physical risks

| Risk | Rating | Mitigation |
|---|---|---|
| Physical coercion to hand over the funds (the "wrench attack") | Low, but of maximum impact | A **decoy wallet**: the seed with no passphrase holds a small amount, the main wallet sits behind the passphrase. Reduce identity linking (section 2.2) |
| Theft or seizure of the signer | Low | It stores nothing; without the seed, the PIN and the passphrase it is useless |
| Physical access to the signer during a session | Low | Short sessions; the emergency wipe |
| A speaker output or a high volume damages the line input | Low (damage, not a leak) | A low volume; a DC-blocking capacitor and a series resistor. The reference breadboard has no protection diodes; the full board plans them ([hardware-minimo.md](hardware-minimo.md)) |

## 6. Accepted risks

Documented so that users decide knowingly:

- **The device that sends the seed or the passphrase knows it.** It is the
  same risk other wallets accept when the passphrase is typed on the
  computer.
- **No secure element.** Offset by a stateless design: there is nothing to
  extract while the signer is off.
- **The format with the Pico and the OLED fitted moves trust to the
  manufacturer.** Offered for convenience, with the risk spelled out.
- **Any format bought by mail links identity and address.** Only generic
  parts bought separately avoid it entirely.

## 7. To do

- An external review of this analysis and of the code ([AUDIT.md](../AUDIT.md)).
- Fuzzing, in the repository, of the tone receiver and the frame decoding.
- Signed releases and a reproducible build of the firmware.
- An inactivity timeout that wipes the seed, and a build option with no
  USB echo.
- Ratings for tones through the air (microphone and speaker), once the
  firmware uses them.
- Regulatory advice for the fitted formats.
- The risks of builds with salvaged parts (chips and memories of unknown
  origin), as the [catalog](catalog.md) gathers real builds.
