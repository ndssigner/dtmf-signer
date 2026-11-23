"""Checks a DIYLC 6.8 breadboard layout of the DTMF-Signer prototype.

DIYLC draws the parts but does not say what they connect. This reads the
project file with a model of DIYLC's Full breadboard (5-hole strips on each
side of the channel, rails taken as continuous from end to end), and prints,
for every resistor, capacitor and button, the nets its two leads reach, plus
the nets that join the Pico, the screen, the terminal block, the pins of a
TO-220 part (as U1.1, U1.2, U1.3) and the leads that leave the board for a
part off it (a wire whose free end has a label, as lead:MT3608 IN+). It
reports points that miss a hole and holes used twice, and exits 1 if there
are any.

Usage: python3 tools/check_diy.py hardware/breadboard/dtmf-signer-breadboard.diy
       python3 tools/check_diy.py hardware/phone/dtmf-signer-pex.diy
"""
import sys
import xml.etree.ElementTree as ET

# Hole geometry relative to the board's point: u across the board, row n along it.
U_COL = {0.6 + 0.1 * i: c for i, c in enumerate("abcde")}
U_COL.update({1.3 + 0.1 * i: c for i, c in enumerate("fghij")})
U_RAIL = {0.2: "rail+a", 0.3: "rail-a", 2.0: "rail+j", 2.1: "rail-j"}
BOARD = {"px": 5.6, "py": 0.1, "orient": "DEFAULT"}
PICO = {36: "3V3", 38: "GND", 33: "AGND", 32: "GP27", 34: "GP28", 22: "CS", 24: "CLK",
        25: "MOSI", 26: "DC", 27: "RES", 23: "GND", 28: "GND", 3: "GND", 8: "GND",
        13: "GND", 18: "GND", 20: "GP15", 39: "VSYS", 40: "VBUS"}


def locate(x, y):
    """(column or rail name, row) of a point, or None."""
    if BOARD["orient"] == "_90":
        n, u = (BOARD["px"] - x) / 0.1, y - BOARD["py"]
    else:
        n, u = (y - BOARD["py"]) / 0.1, x - BOARD["px"]
    if abs(n - round(n)) > 1e-3:
        return None
    for table in (U_COL, U_RAIL):
        for k, v in table.items():
            if abs(k - u) < 1e-3:
                return v, round(n)
    return None


def node(x, y):
    loc = locate(x, y)
    if loc is None:
        return None
    col, row = loc
    if col.startswith("rail"):
        return col
    return ("L" if col in "abcde" else "R") + str(row)


def where(x, y):
    col, row = locate(x, y)
    return col + str(row)


def pts(el, tag):
    holder = el.find(tag)
    return [] if holder is None else [(float(p.get("x")), float(p.get("y"))) for p in holder.findall("point")]


parent = {}


def find(n):
    while parent.setdefault(n, n) != n:
        parent[n] = parent[parent[n]]
        n = parent[n]
    return n


def union(a, b):
    parent[find(a)] = find(b)


root = ET.parse(sys.argv[1]).getroot()
comps = list(root.find("components"))
for c in comps:
    if c.tag.endswith("Breadboard"):
        pt = c.find("point")
        BOARD.update(px=float(pt.get("x")), py=float(pt.get("y")), orient=c.findtext("orientation"))
problems, terminals, holes = [], [], {}
# Labels, to name the free end of a lead that leaves the board.
labels = [((float(c.find("point").get("x")), float(c.find("point").get("y"))), c.findtext("text"))
          for c in comps if c.tag.endswith(".Label")]


# Pads of modules drawn off the board, which wires reach: DIYLC's MT3608 board.
pads = {}
for c in comps:
    if c.tag.endswith(".MT3608BoostConverter"):
        for p, pin in zip(pts(c, "controlPoints"), ("VOUT+", "VOUT-", "VIN+", "VIN-")):
            pads[(round(p[0], 3), round(p[1], 3))] = f"{c.findtext('name')}:{pin}"


def off_board(p, reach=0.4):
    """What the free end of a wire off the board reaches: a module's pad, or
    the nearest label within reach."""
    pad = pads.get((round(p[0], 3), round(p[1], 3)))
    if pad:
        return pad
    near = sorted((abs(q[0] - p[0]) + abs(q[1] - p[1]), t) for q, t in labels
                  if abs(q[0] - p[0]) <= reach and abs(q[1] - p[1]) <= reach)
    return "lead:" + near[0][1].replace(" ", "_") if near else None


named = {}  # node -> set of names (Pico pins, OLED pins, terminal block, pads, leads)


def use(p, who):
    n = node(*p)
    if n is None:
        problems.append(f"{who}: point {p} is not on a hole")
        return None
    if not n.startswith("rail"):
        holes.setdefault(where(*p), []).append(who)
    return n


for c in comps:
    kind = c.tag.split(".")[-1]
    name = c.findtext("name")
    if kind == "OrthogonalWire":
        cp = pts(c, "controlPoints")
        ends = []
        for p in (cp[0], cp[-1]):
            off = off_board(p) if node(*p) is None else None
            ends.append(("off", off) if off else use(p, name))
        a, b = ends
        if isinstance(a, tuple) and isinstance(b, tuple):
            problems.append(f"{name}: both ends are off the board")
        elif isinstance(a, tuple) or isinstance(b, tuple):
            off, hole = (a, b) if isinstance(a, tuple) else (b, a)
            if hole:
                named.setdefault(hole, set()).add(off[1])
        elif a and b:
            union(a, b)
    elif kind == "TransistorTO220":
        for i, p in enumerate(pts(c, "controlPoints")):
            n = use(p, name)
            if n:
                named.setdefault(n, set()).add(f"{name}.{i + 1}")
    elif kind in ("Resistor", "RadialElectrolytic", "RadialMicaCapacitor", "RadialCeramicDiskCapacitor"):
        p = pts(c, "points")
        v = c.find("value")
        val = f'{v.get("value")}{v.get("unit")}' if v is not None else ""
        terminals.append((name, val, use(p[0], name), use(p[1], name)))
    elif kind == "TactileMicroSwitch":
        cp = pts(c, "controlPoints")
        ns = [use(p, name) for p in cp]
        # Legs 0/2 and 1/3 are joined inside; the press joins the two pairs.
        if ns[0] and ns[2]:
            union(ns[0], ns[2])
        if ns[1] and ns[3]:
            union(ns[1], ns[3])
        terminals.append((name, c.findtext("value") or "", ns[0], ns[1]))
    elif kind == "RaspberryPiPico":
        cp = pts(c, "controlPoints")[:40]
        for i, p in enumerate(cp):
            pin = i + 1 if i < 20 else 60 - i
            n = use(p, name)
            if n and pin in PICO:
                named.setdefault(n, set()).add("pico:" + PICO[pin])
    elif kind == "OLEDDisplay":
        for p, lab in zip(pts(c, "controlPoints"), ["GND", "VCC", "CLK", "MOSI", "RES", "DC", "CS"]):
            n = use(p, name)
            if n:
                named.setdefault(n, set()).add("oled:" + lab)
    elif kind == "PCBTerminalBlock":
        for i, p in enumerate(pts(c, "controlPoints")):
            n = use(p, name)
            if n:
                named.setdefault(n, set()).add(f"tb:{i + 1}")

nets = {}
for n, labels in named.items():
    nets.setdefault(find(n), set()).update(labels)


def net(n):
    if n is None:
        return "?"
    r = find(n)
    return "+".join(sorted(nets[r])) if r in nets else "~" + r


for name, val, a, b in terminals:
    print(f"{name:5} {val:8} {net(a)}  <->  {net(b)}")
print()
for r, labels in nets.items():
    if len(labels) > 1:
        print("net:", " + ".join(sorted(labels)))
for h, who in sorted(holes.items()):
    if len(who) > 1:
        problems.append(f"hole {h} used by {who}")
print("\nPROBLEMS:" if problems else "\nno problems", *problems, sep="\n  ")
sys.exit(1 if problems else 0)
