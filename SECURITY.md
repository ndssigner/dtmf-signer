# Security

DTMF-Signer handles Bitcoin seeds. If you find a vulnerability, please
report it privately: use GitHub's **"Report a vulnerability"** button
(Security tab of this repository) rather than a public issue.

Useful to include: what an attacker can do, the steps to reproduce it, and
the commit you tested (`git rev-parse HEAD`).

## Auditing

Where to start, the critical files and the pinned dependencies:
[AUDIT.md](AUDIT.md). What the signer must never do:
[SPEC.md §3](SPEC.md#3-invariants). What is protected and from whom:
[docs/risk-analysis.md](docs/risk-analysis.md).

## Scope

- **DTMF-Signer's own code:** the core (`core/`), the device logic
  (`app/`), the ports (`ports/`), the tools that make its test vectors and
  layouts (`tools/`), and the hardware designs (`hardware/`, `docs/`).
- **ur-tones:** the tone protocol, the seed PIN and their C library. A
  problem there should be reported to
  [ur-tones](https://github.com/ndssigner/ur-tones/security); please tell
  us too, so we can update the pinned version.
- **libsecp256k1 and the QR code generator:** report upstream
  ([libsecp256k1](https://github.com/bitcoin-core/secp256k1/security),
  [QR Code generator](https://github.com/nayuki/QR-Code-generator)), and
  tell us.

## Known limits (not vulnerabilities)

- The signer trusts the computer or phone that sends it a seed or a
  passphrase: that device knows them. The cable protects the path, not the
  source ([SPEC.md §4](SPEC.md#4-channels)).
- The prototype's USB carries an output-only development echo with public
  data (the descriptor, signed PSBTs, state and timing lines). The firmware
  never reads from USB, and USB reset to the bootloader is disabled. Power
  it from a charger when the computer should not learn which wallet it is.
- No inactivity timeout yet: wipe (hold LEFT 3 s) or unplug when done.
- Physical attacks on the chip (fault injection, reading SRAM right after
  power-off) are out of scope; the signer stores nothing, so they only
  matter during a session.
- The project is a prototype and has not been audited.
