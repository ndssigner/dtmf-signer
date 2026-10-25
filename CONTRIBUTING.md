# Contributing

DTMF-Signer wants anyone to be able to build a Bitcoin signer they trust:
with new parts, or with whatever can be recovered from a scrapyard. That only
works if the specification is clear, if every build can be checked, and if
changes are proposed in the open. So every change comes through a public
issue or pull request here, under the same rules.

## What is here

- **The specification**, [`SPEC.md`](SPEC.md), with its Spanish translation
  in [`docs/es/especificacion.md`](docs/es/especificacion.md): what every
  build must do. This is the standard.
- **The catalog**, [`docs/catalog.md`](docs/catalog.md) and
  [`docs/es/catalogo.md`](docs/es/catalogo.md): the capabilities, what each
  one needs and costs, and the profiles that combine them.
- **The test vectors**, `vectors/`: the contract. A build that reproduces
  them does what the specification says. So far `keys-v0.json` (PSBT
  vectors are still planned).
- **The reference firmware**: a portable core (`core/`, the keys so far) and
  a build for a computer (`ports/host`) exist; the port for the Raspberry
  Pi Pico is still planned.
- **The Pico hardware**, [`docs/hardware-minimo.md`](docs/hardware-minimo.md),
  and the risk analysis, [`docs/analisis-riesgos.md`](docs/analisis-riesgos.md)
  (both in Spanish for now).

## Kinds of change

### 1. A change to the specification

Anything that changes what a build must do: the core operations, the
invariants, the channel policy, verification, the rules for storage.

1. **Open an issue first** ("Change to the specification"). Say what problem
   it solves, and what it costs the smallest builds: the specification must
   never stop a scrapyard build from being possible.
2. **Then one pull request** with `SPEC.md`, its Spanish translation, and the
   vectors that show the change. The two languages
   must stay in step: `python3 tools/check_docs.py` checks it.

### 2. A capability or a build for the catalog

A new capability, or a build someone made: another microcontroller, a
decoder chip, a salvaged part. A pull request is enough. Describe it with
the five lines every capability has (what it does, what it needs, its time
on each processor class, its risks, its vectors), in both languages. **Times
are measured, never guessed**: say how you measured them, or write "not
measured".

### 3. A port

DTMF-Signer on another microcontroller, a vintage processor, several
microcontrollers together. Ports are welcome, as pull requests or as
projects of their own that report here. Open an issue ("A port") to say you
are on it. A porting guide, with what a port must provide and which vectors
it must pass, will follow in `docs/porting.md`.

### 4. Something ur-tones needs

Tones, frames, the seed PIN and the formats on the wire belong to
[ur-tones](https://github.com/ndssigner/ur-tones). What DTMF-Signer needs
from it is proposed there, like anyone else would: an issue for the format,
a pull request for the implementations, following its CONTRIBUTING.

## Before a pull request

```bash
cmake -S . -B build -G Ninja            # the core (clone with --recurse-submodules)
cmake --build build
ctest --test-dir build --output-on-failure   # the core's C tests
python3 -m unittest discover -s tests   # the tests (they run the host tool built above)
python3 tools/check_docs.py             # links, anchors, English/Spanish parity
```

All of them must pass; CI runs them on every pull request.

## Rules

- **Test data only:** public test seeds and testnet PSBTs, in documents,
  code, tests, issues and recordings. A recording of a real seed is a copy
  of it.
- **Nothing reaches a network.** The signer and its tools work offline.
- **The style of the file you touch:** its wording, its tables, its tests.
- **Small pull requests**, one subject each, with a commit message that says
  why.

**License.** DTMF-Signer has not chosen its license yet (see the
[README](README.md#license)). Contributions will be under the license the
project publishes.
