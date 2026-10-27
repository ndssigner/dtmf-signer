# Breadboard layout

*[Leer en español](README.es.md)*

The reference build of the **Cable and QR** profile on one full-size
breadboard (830 points): a Raspberry Pi Pico, the 1.3" SH1106 OLED over
SPI, five buttons on one ADC pin, and a three-way screw terminal block for
the audio cable. The same layout fits a solderable breadboard (a perfboard
with the breadboard's strips and rails), and the printed template for it
will come from this file.

**Status:** designed and checked by its nets; not yet built from this file.
The circuits themselves (screen, buttons, line input) were tested on an
earlier breadboard.

## Files

- [`dtmf-signer-breadboard.diy`](dtmf-signer-breadboard.diy): the layout,
  for [DIY Layout Creator](https://github.com/bancika/diy-layout-creator)
  6.8 or later (free).
- [`tools/check_diy.py`](../../tools/check_diy.py) reads it and prints what
  each part connects; `tests/test_check_diy.py` checks those connections
  against what the firmware expects.

## How it is laid out

Hold the board landscape, with the Pico on the right:

- **Screen:** its pins next to the centre channel; its body covers that half
  of the board and overhangs the edge. Wires run under it (there are 2–3 mm
  between the board and the screen).
- **Buttons**, beside the screen, from left to right: LEFT, UP, OK, DOWN,
  RIGHT, 10 mm apart. Each one goes through its resistor (1 k, 2.2 k, 4.7 k,
  10 k, 22 k) to one rail, which is the ladder's node and goes to GP28 with a
  10 k pull-up. **That rail is not 3V3**: never power anything from it.
- **Audio terminal block:** 1 OUT, 2 COM (ground), 3 IN.
  - The input (IN) goes through 1 µF (its − to the terminal), is centred at
    1.65 V by two 10 k resistors, and reaches GP27 through 1 k and 4.7 k with
    10 nF to ground.
  - The output (OUT) comes from GP15 through 1 k and 10 µF (its − to the
    terminal). The firmware does not drive it yet.
- **Pico:** at the right end, with the USB on the board's edge.
- **Rails:** the layout takes each rail as continuous from end to end. Check
  yours with a multimeter; if a rail is split in the middle, bridge it.

## The audio cable

Cut the cable of a cheap wired headset with a microphone (a four-contact
3.5 mm plug), or use a ready-wired 3.5 mm jack or plug, and screw its wires
into the terminal block:

| Terminal | Wire (CTIA plug, the usual one) |
|---|---|
| 3 IN | tip: left channel of the phone's or computer's headphone output |
| 2 COM | ring 2: ground |
| 1 OUT | sleeve: microphone |

Wire colours vary between headsets: find each wire with a multimeter in
continuity mode, from the plug's contacts to the cut wires. Older OMTP plugs
swap ground and microphone. OUT is meant for a microphone input but has not
been tested, and a microphone input may need the level lowered with a
resistor divider.

## On a perfboard

- Put female headers under the Pico and the screen, as sockets: they can be
  taken out, and wires can still run under them.
- Use solid, insulated wire. Wires may cross; what matters is that every
  lead goes into its own hole.
