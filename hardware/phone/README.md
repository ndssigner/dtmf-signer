English · [Español](README.es.md)

# Phone Exchange (PEX) on a breadboard

The cheapest DTMF-Signer: a Raspberry Pi Pico, a handful of parts and an old
tone telephone. The telephone is the whole interface: the signer powers its
line, speaks to you in the earpiece, hears its keys, and hears your wallet's
tones through the handset's microphone. No screen and no buttons. The
firmware is the Pico port's `DS_FIRMWARE=PEX`
([how to build, flash and use it](../../ports/pico/README.md#the-phone-exchange-pex));
the profile is
[`PROFILE-PHONE`](../../docs/catalog.md#profile-phone--phone-exchange-pex).

**Never connect it to a socket of the real telephone network:** that line
carries up to 48 V, and about 90 V AC when it rings.

**Status:** designed and running on a breadboard. The schematic is drawn
in KiCad ([`kicad/pex.kicad_sch`](kicad/pex.kicad_sch)); the layout below is
a table, and the DIY Layout Creator file and the perfboard version are still
to come. Do not use it with funds.

## Parts

The parts list is [`bom.csv`](bom.csv): a Pico, a full-size breadboard, a
5 to 12 V boost module (MT3608), an LM317, an RJ11 socket, a dozen resistors
and capacitors, and any tone telephone with its handset and its telephone
cable. Without a screen and buttons it costs less than
[Cable and QR](../breadboard/README.md), and second-hand tone telephones
are easy to come by.

## The circuit

![Schematic of the PEX: the Pico, the 12 V boost, the LM317 line current source, the RJ11 socket, and the line in, audio out and hook circuits](kicad/pex.svg)

- **The line's power:** the LM317 holds 1.25 V across the 51 Ω, so about
  24.5 mA flow through the line whatever is on it, which is what a telephone
  expects. A short circuit does no harm. An open line sits at about 11 V; a
  telephone off the hook holds it at 6 to 8 V.
- **Hearing the line (GP27):** the 100 nF passes the tones and blocks the
  line's DC; then the usual line input.
- **Speaking into the line (GP15):** PWM, smoothed by the 1 kΩ and the
  10 nF, coupled into the line by the 10 µF, its + towards the line.
- **The hook (GP28):** the 47k/10k divider brings the line's voltage within
  the ADC's range (about 1 V off the hook, about 2 V on it); the 100 nF
  removes the tones from it.
- **The telephone** plugs into the RJ11 socket with its own telephone
  cable; the line is the socket's two centre contacts.

## Layout

The breadboard lies horizontally with the **Pico on the left**, its USB
towards the left edge. Rows run from 1 (right end) to 63 (left end);
columns a–e are the top half and f–j the bottom half.

**The Pico** straddles the channel on **rows 44 to 63**, USB at row 63:
pins 21–40 in column **d** (pin 21 at d44, pin 40 at d63) and pins 1–20 in
column **i** (pin 1 at i63, pin 20 at i44).

| Pin | Signal | Hole |
| --- | --- | --- |
| 40 | VBUS (5 V from the USB) | d63 |
| 38 | GND | d61 |
| 36 | 3V3 (OUT) | d59 |
| 34 | GP28 (hook) | d57 |
| 33 | AGND | d56 |
| 32 | GP27 (line in) | d55 |
| 20 | GP15 (audio out) | i44 |
| 18 | GND | i46 |

**Rails:**

| Jumper | Result |
| --- | --- |
| a59 → top outer rail | **3V3** |
| a61 → top inner rail | **GND** |
| a56 → top inner rail | AGND to ground |
| j46 → bottom outer rail | **GND** |

**Line in** (top half, rows 36–42):

| Part | Holes |
| --- | --- |
| 100 nF capacitor | c36 – c40 |
| 10 kΩ resistor | a40 – top outer rail |
| 10 kΩ resistor | b40 – top inner rail |
| 1 kΩ resistor | d40 – d42 |
| 4.7 kΩ resistor | b42 – b38 |
| Jumper | e38 → e55 (to GP27) |
| 10 nF capacitor | b55 – top inner rail |

**Audio out** (bottom half; row 36 is **line A**):

| Part | Holes | Notes |
| --- | --- | --- |
| Jumper | f36 → e36 | the line to the 100 nF above |
| 10 µF electrolytic | + at i36, − at i38 | the stripe (−) towards row 38 |
| 1 kΩ resistor | h38 – h44 | row 44 bottom is GP15 |
| 10 nF capacitor | j38 – bottom outer rail | |

**The line's power** (bottom half, rows 20–24):

| Part | Holes | Notes |
| --- | --- | --- |
| Jumper | j20 → bottom outer rail | row 20 is GND |
| **LM317** | j22, j23, j24 | printed face towards you (the bottom edge): from left to right ADJ (j24), OUT (j23), IN (j22) |
| **51 Ω** resistor | f23 – f24 | from OUT to ADJ |
| **100 µF** electrolytic | + at g22, − at g20 | |
| 100 nF capacitor | h22 – h20 | |
| MT3608 **IN+** | a63 | VBUS |
| MT3608 **IN−** | top inner rail | |
| MT3608 **OUT+** | f22 | row 22 is the 12 V |
| MT3608 **OUT−** | bottom outer rail | |
| Jumper | g24 → g36 | line A reaches row 36 |

**The hook** (bottom half, rows 24–28):

| Part | Holes |
| --- | --- |
| **47 kΩ** resistor | i24 – i28 |
| 10 kΩ resistor | j28 – bottom outer rail |
| 100 nF capacitor | h28 – bottom outer rail |
| Jumper | f28 → a57 (to GP28) |

**The telephone:** the RJ11 socket's two centre contacts: one to **h36**
(line A), the other to the **bottom outer rail** (line B). If the telephone
stays silent and the line at about 11 V with the handset lifted, swap them.

## Building it, step by step

Check after each step; the numbers are those of a working build.

1. **The Pico and the rails.** Without USB, in ohms: each rail continuous
   end to end, no short between the top rails.
2. **Line in and audio out.** With USB: about **1.65 V** at c40.
3. **Set the boost module before wiring its output.** A new MT3608 may come
   set to 16 V or more. Wire only its input (IN+ to a63, IN− to the top
   inner rail), leave OUT+ and OUT− loose, plug the USB and turn its
   potentiometer until **12.0 V** between OUT+ and OUT−. Unplug.
4. **The line's power.** Without USB, no short between f22 and the bottom
   outer rail. With USB and no telephone: about 12 V at f22, about 11 V at
   h36; the meter in mA from h36 to the bottom outer rail reads **22–25 mA**.
5. **The hook.** With USB and no telephone, about 1.9 V at f28 (never over
   3.3 V); then the jumper to a57.
6. **The telephone.** With the handset lifted, the line at **6–8 V**; on the
   hook, about 11 V. Press a key: you hear its tone in the earpiece.

Then flash the firmware and a voice pack
([how](../../ports/pico/README.md#voice-packs)): on lifting the handset,
the signer greets you.

## Use

- **Power it from a USB charger or a power bank,** never from a computer.
- **Lift the handset** and follow what it says. To hang up for a moment (the
  hook flash that signs and wipes), press the hook switch for less than a
  second.
- **While the handset is held to your wallet** you cannot hear the signer;
  the Pico's LED tells how the reception goes: a short flash per tone, a long
  one per part taken, and on, steady, once it is all in.

## Notes

- Tested on a breadboard with stand-ins for a telephone that had lost its
  handset (see [what is tested](../../ports/pico/README.md#what-is-tested-pex)):
  the design above, with a whole telephone, is the one to build.
- The boost module can whine in the earpiece; a 10 Ω resistor and a second
  100 µF between it and the LM317 filter it.
