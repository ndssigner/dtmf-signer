"""Draws the breadboard layout as a 1:1 printable template (PDF): paste it on
a solderable breadboard before fitting the parts. It marks every part where
it goes (value, polarity, pin 1), names the buttons and the audio terminals,
and puts in the free end of the board the project's address, the main
controls and a QR code of the ur-tones web tool.

Everything is drawn from the DIYLC project, at its own coordinates (DIYLC
works in inches), so the template follows the layout file.

Usage:
  pip install -r tools/requirements-template.txt
  python3 tools/breadboard_template.py hardware/breadboard/dtmf-signer-breadboard.diy \\
      --lang en --out hardware/breadboard/template-en.pdf
  python3 tools/breadboard_template.py hardware/phone/dtmf-signer-pex.diy --build pex \\
      --lang en --out hardware/phone/template-en.pdf

A lead that leaves the board (to the PEX's boost module or its telephone
socket) is drawn only on the board, with the name of where it goes at the
board's edge.
"""
import argparse
import xml.etree.ElementTree as ET

from reportlab.graphics import renderPDF
from reportlab.graphics.barcode.qr import QrCodeWidget
from reportlab.graphics.shapes import Drawing
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas

REPO = "https://github.com/ndssigner/dtmf-signer"
WEB_TOOL = "https://ndssigner.github.io/ur-tones/"
INCH = 25.4 * mm

TEXT = {"cable-qr": {
    "en": {
        "title": "DTMF-Signer - Cable and QR - breadboard template",
        "print": "This bar must measure 100 mm.",
        "warn": ["PRINT AT ACTUAL SIZE (100 %)",
                 "Turn off \"fit to page\" or \"scale to fit\".",
                 "A4 landscape; on Letter paper it also fits at actual size.",
                 "Check the bar at the bottom: it must measure 100 mm."],
        "bom_title": "Parts",
        "bom": {"pico": "Raspberry Pi Pico (RP2040), or a clone with its layout",
                "oled": "1.3\" SH1106 OLED, 128 x 64, SPI (7 pins)",
                "button": "Push button, 6 x 6 mm",
                "res": "Resistor {v}, 1/4 W",
                "elec": "Electrolytic capacitor {v}",
                "film": "Film capacitor {v}",
                "tb": "Screw terminal block, 3 ways, 5.08 mm",
                "board": "Full-size breadboard, or a solderable one",
                "headers": "Female header strips (sockets for the Pico and the screen)",
                "wire": "Solid insulated wire",
                "extra": "Also: a USB cable and a USB-A charger, a wired headset with a microphone (to cut)"},
        "licence": "Hardware: CERN-OHL-S v2",
        "buttons": {"LEFT": "LEFT", "UP": "UP", "OK": "OK", "DOWN": "DOWN", "RIGHT": "RIGHT"},
        "tb": ["OUT", "COM", "IN"],
        "tb_name": "DTMF Audio",
        "rails": ["+3V3", "GND", "L (buttons) - NOT 3V3", "GND"],
        "info": ["DTMF-Signer", "Cable and QR", "Hold OK: sign", "Hold LEFT 3 s: wipe"],
        "elec_title": "Electrical",
        "elec": ["USB: 5 V only, from a USB-A charger (A-to-C cable).",
                 "IN: a headphone output at about 30 % volume, 2 V peak to peak at most. Never a speaker, an amplifier or a phone line.",
                 "OUT: GP15, up to 3.3 V peak to peak (not used yet).",
                 "COM: ground. No outside voltages."],
        "qr": "ur-tones web tool",
        "qr_note": "Not on the board: it would confuse a wallet's camera.",
        "usb": "USB",
        "screen": "screen (overhangs)",
    },
    "es": {
        "title": "DTMF-Signer - Cable y QR - plantilla de protoboard",
        "print": "Esta barra debe medir 100 mm.",
        "warn": ["IMPRIMIR A TAMAÑO REAL (100 %)",
                 "Desactiva \"ajustar a la página\" o \"escalar\".",
                 "A4 apaisado; en papel Carta (Letter) también cabe a tamaño real.",
                 "Comprueba la barra del pie: debe medir 100 mm."],
        "bom_title": "Piezas",
        "bom": {"pico": "Raspberry Pi Pico (RP2040), o un clon con su disposición",
                "oled": "OLED SH1106 de 1,3\", 128 x 64, SPI (7 pines)",
                "button": "Pulsador de 6 x 6 mm",
                "res": "Resistencia de {v}, 1/4 W",
                "elec": "Condensador electrolítico de {v}",
                "film": "Condensador de película de {v}",
                "tb": "Bornera de tornillo de 3 contactos, 5,08 mm",
                "board": "Protoboard de tamaño completo, o una soldable",
                "headers": "Tiras de pines hembra (zócalos de la Pico y la pantalla)",
                "wire": "Cable rígido aislado",
                "extra": "Además: un cable USB y un cargador USB-A, unos auriculares de cable con micrófono (para cortar)"},
        "licence": "Hardware: CERN-OHL-S v2",
        "buttons": {"LEFT": "IZQ", "UP": "ARRIBA", "OK": "OK", "DOWN": "ABAJO", "RIGHT": "DCHA"},
        "tb": ["OUT", "COM", "IN"],
        "tb_name": "DTMF Audio",
        "rails": ["+3V3", "GND", "L (botones) - NO es 3V3", "GND"],
        "info": ["DTMF-Signer", "Cable y QR", "Mantener OK: firmar", "Mantener IZQ 3 s: borrar"],
        "elec_title": "Electricidad",
        "elec": ["USB: solo 5 V, de un cargador USB-A (cable A-C).",
                 "IN: salida de auriculares al 30 % de volumen, 2 V pico a pico como mucho. Nunca un altavoz, un amplificador ni una línea de teléfono.",
                 "OUT: GP15, hasta 3,3 V pico a pico (aún sin usar).",
                 "COM: masa. Sin tensiones externas."],
        "qr": "herramienta web ur-tones",
        "qr_note": "Fuera de la placa: confundiría a la cámara de la cartera.",
        "usb": "USB",
        "screen": "pantalla (sobresale)",
    },
}}

TEXT["pex"] = {
    "en": {
        "title": "DTMF-Signer - Phone Exchange (PEX) - breadboard template",
        "print": TEXT["cable-qr"]["en"]["print"],
        "warn": TEXT["cable-qr"]["en"]["warn"],
        "bom_title": "Parts",
        "bom": {"pico": "Raspberry Pi Pico (RP2040), or a clone with its layout",
                "res": "Resistor {v}, 1/4 W",
                "elec": "Electrolytic capacitor {v}, {volts}",
                "film": "Film capacitor {v}",
                "ceramic": "Ceramic capacitor {v}",
                "lm317": "LM317 regulator, TO-220",
                "mt3608": "MT3608 boost module, 5 to 12 V",
                "rj11": "RJ11 socket (its two centre contacts are the line)",
                "board": "Full-size breadboard, or a solderable one",
                "headers": "Female header strips (a socket for the Pico)",
                "wire": "Solid insulated wire",
                "extra": "Also: a USB cable and a USB charger or power bank, and a tone telephone "
                         "with its handset and telephone cable"},
        "licence": "Hardware: CERN-OHL-S v2",
        "rails": ["+3V3", "GND", "LINE A - up to 12 V", "GND"],
        "info": ["DTMF-Signer", "Phone Exchange (PEX)", "Lift the handset and listen",
                 "Hook flash: sign, wipe"],
        "elec_title": "Electrical",
        "elec": ["NEVER connect it to the real telephone network: 48 V, and about 90 V when it rings.",
                 "USB: 5 V only, from a USB charger or a power bank, never from a computer.",
                 "Set the MT3608 to 12.0 V before wiring its output.",
                 "LINE A: up to 12 V. Only a tone telephone, by its RJ11 socket."],
        "qr": "ur-tones web tool",
        "qr_note": "",
        "usb": "USB",
    },
    "es": {
        "title": "DTMF-Signer - Centralita (PEX) - plantilla de protoboard",
        "print": TEXT["cable-qr"]["es"]["print"],
        "warn": TEXT["cable-qr"]["es"]["warn"],
        "bom_title": "Piezas",
        "bom": {"pico": "Raspberry Pi Pico (RP2040), o un clon con su disposición",
                "res": "Resistencia de {v}, 1/4 W",
                "elec": "Condensador electrolítico de {v}, {volts}",
                "film": "Condensador de película de {v}",
                "ceramic": "Condensador cerámico de {v}",
                "lm317": "Regulador LM317, TO-220",
                "mt3608": "Módulo elevador MT3608, de 5 a 12 V",
                "rj11": "Conector RJ11 (sus dos contactos centrales son la línea)",
                "board": "Protoboard de tamaño completo, o una soldable",
                "headers": "Tiras de pines hembra (zócalo de la Pico)",
                "wire": "Cable rígido aislado",
                "extra": "Además: un cable USB y un cargador USB o una batería externa, y un teléfono "
                         "de tonos con su auricular y su cable telefónico"},
        "licence": "Hardware: CERN-OHL-S v2",
        "rails": ["+3V3", "GND", "LÍNEA A - hasta 12 V", "GND"],
        "info": ["DTMF-Signer", "Centralita (PEX)", "Descuelga y escucha",
                 "Colgar un instante: firmar, borrar"],
        # The layout's own notes (in English) as this sheet shows them.
        "labels": {"to the telephone": "al teléfono", "MT3608: set to 12.0 V": "MT3608: ajústalo a 12,0 V",
                   "RJ11 line A": "RJ11 línea A", "RJ11 line B": "RJ11 línea B"},
        "elec_title": "Electricidad",
        "elec": ["NUNCA la conectes a la red telefónica real: 48 V, y unos 90 V cuando suena.",
                 "USB: solo 5 V, de un cargador USB o una batería externa, nunca de un ordenador.",
                 "Ajusta el MT3608 a 12,0 V antes de conectar su salida.",
                 "LÍNEA A: hasta 12 V. Solo un teléfono de tonos, por su conector RJ11."],
        "qr": "herramienta web ur-tones",
        "qr_note": "",
        "usb": "USB",
    },
}

# What differs between the builds' sheets besides their text: where the rail
# names go (two rows each), the free part of the board that carries the
# project's notes (from row, to row, from u, to u) and the parts list's fixed
# lines.
BUILD = {
    "cable-qr": {"rail_rows": [(21, 53), (21, 53), (47, 58), (21, 53)],
                 "info": (10.5, 0.5, 0.1, 2.2), "headers": "2 x 20, 1 x 7", "extra_bom": []},
    "pex": {"rail_rows": [(22, 51), (22, 51), (22, 51), (22, 51)],
            "info": (26.5, 0.5, 0.1, 0.98), "headers": "2 x 20", "extra_bom": [(1, "rj11")],
            "bom_cols": 2, "scissors": True},
}
# The pins of a TO-220 part, by its value.
TO220_PINS = {"LM317": ("ADJ", "OUT", "IN")}
# DIYLC's MT3608 board: its pads, in the order of its control points.
MT3608_PADS = ("OUT+", "OUT-", "IN+", "IN-")


def pts(el, tag):
    h = el.find(tag)
    return [] if h is None else [(float(p.get("x")), float(p.get("y"))) for p in h.findall("point")]


def arrow(c, x, y, angle, size):
    """A filled arrow centred on (x, y) pointing at angle (degrees, 0 = right)."""
    c.saveState()
    c.translate(x, y)
    c.rotate(angle)
    h = size / 2
    p = c.beginPath()
    p.moveTo(h, 0)
    p.lineTo(0, h * 0.8)
    p.lineTo(0, h * 0.3)
    p.lineTo(-h, h * 0.3)
    p.lineTo(-h, -h * 0.3)
    p.lineTo(0, -h * 0.3)
    p.lineTo(0, -h * 0.8)
    p.close()
    c.drawPath(p, fill=1, stroke=0)
    c.restoreState()


def ground(c, x, y, size):
    """The ground symbol, centred on (x, y)."""
    h = size / 2
    c.saveState()
    c.setLineWidth(size * 0.09)
    c.line(x, y + h, x, y + h * 0.15)
    for i, w in enumerate((1.0, 0.62, 0.25)):
        yy = y + h * 0.15 - i * h * 0.42
        c.line(x - h * w, yy, x + h * w, yy)
    c.restoreState()


def scissors(c, x, y, angle):
    """Open scissors on a cutting line through (x, y), the blades along it."""
    c.saveState()
    c.translate(x, y)
    c.rotate(angle)
    c.setFillGray(1)
    c.rect(-3.4 * mm, -2.2 * mm, 7 * mm, 4.4 * mm, fill=1, stroke=0)
    c.setFillGray(0)
    c.setLineWidth(0.7)
    for s_ in (1, -1):
        c.circle(-2.2 * mm, s_ * 1.1 * mm, 0.85 * mm)                       # the rings
        c.line(-1.5 * mm, s_ * 0.6 * mm, 3.3 * mm, -s_ * 1.3 * mm)         # the blades, crossed
    c.circle(0.3 * mm, -0.0 * mm, 0.25 * mm, fill=1)                        # the pivot
    c.restoreState()


def icon(c, name, x, y, size):
    """Button and terminal icons."""
    angles = {"LEFT": 180, "UP": 90, "DOWN": 270, "RIGHT": 0, "IN": 90, "OUT": 270}
    if name == "OK":
        c.circle(x, y, size * 0.32, fill=1, stroke=0)
    elif name == "COM":
        ground(c, x, y, size)
    elif name in angles:
        arrow(c, x, y, angles[name], size)


class Board:
    """DIYLC Full breadboard geometry: rows 1..63 along the board, u across."""

    U_HOLES = [0.2, 0.3] + [0.6 + 0.1 * i for i in range(5)] + [1.3 + 0.1 * i for i in range(5)] + [2.0, 2.1]

    def __init__(self, el):
        p = el.find("point")
        self.px, self.py = float(p.get("x")), float(p.get("y"))
        self.rot = el.findtext("orientation") == "_90"

    def point(self, row, u):
        """DIYLC coordinates (inches) of row/u."""
        if self.rot:
            return self.px - 0.1 * row, self.py + u
        return self.px + u, self.py + 0.1 * row

    def bbox(self):
        corners = [self.point(r, u) for r in (-0.3, 64.3) for u in (0.05, 2.25)]
        xs, ys = [c[0] for c in corners], [c[1] for c in corners]
        return min(xs), min(ys), max(xs), max(ys)


class Sheet:
    def __init__(self, path, board, t):
        # invariant: no creation date or time zone in the file, and the same bytes every run.
        self.c = canvas.Canvas(path, pagesize=landscape(A4), invariant=1)
        self.c.setAuthor("NDS-Signer")
        self.c.setTitle(t["title"])
        self.c.setSubject(REPO)
        self.c.setCreator(REPO)
        self.board, self.t = board, t
        x0, y0, x1, y1 = board.bbox()
        self.ox, self.oy = x0, y0
        pw, ph = landscape(A4)
        self.left = (pw - (x1 - x0) * INCH) / 2
        self.top = ph - 45 * mm

    def xy(self, x, y):
        """Page coordinates of a DIYLC point (y grows downwards in DIYLC)."""
        return self.left + (x - self.ox) * INCH, self.top - (y - self.oy) * INCH

    def text(self, x, y, s, size=5, angle=0, bold=False, anchor="middle", bg=False):
        c = self.c
        font = "Helvetica-Bold" if bold else "Helvetica"
        c.saveState()
        c.setFont(font, size)
        c.translate(*self.xy(x, y))
        c.rotate(angle)
        w = c.stringWidth(s, font, size)
        x0 = {"middle": -w / 2, "start": 0, "end": -w}[anchor]
        if bg:
            c.setFillGray(1)
            c.rect(x0 - 0.4, -size * 0.45, w + 0.8, size * 0.95, fill=1, stroke=0)
            c.setFillGray(0)
        c.drawString(x0, -size * 0.35, s)
        c.restoreState()


def value_text(v):
    """A DIYLC value element as text: 4.7K -> 4.7 kΩ, 1.0uF -> 1 µF."""
    num = float(v.get("value"))
    unit = {"K": "kΩ", "M": "MΩ", "R": "Ω", "uF": "µF", "nF": "nF", "pF": "pF"}.get(v.get("unit"), v.get("unit"))
    return f"{num:g} {unit}"


def draw_bom(c, t, comps, x, top, build):
    """The bill of materials, counted from the layout's parts."""
    count = {}

    def add(key, n=1):
        count[key] = count.get(key, 0) + n
    for el in comps:
        kind = el.tag.split(".")[-1]
        v = el.find("value")
        if kind == "RaspberryPiPico":
            add(("1", t["bom"]["pico"]))
        elif kind == "OLEDDisplay":
            add(("2", t["bom"]["oled"]))
        elif kind == "TactileMicroSwitch":
            add(("3", t["bom"]["button"]))
        elif kind == "MT3608BoostConverter":
            add(("2", t["bom"]["mt3608"]))
        elif kind == "TransistorTO220":
            add(("3", t["bom"]["lm317"]))
        elif kind == "Resistor":
            val = float(v.get("value")) * {"K": 1e3, "M": 1e6}.get(v.get("unit"), 1)
            add((f"4{val:012.0f}", t["bom"]["res"].format(v=value_text(v))))
        elif kind == "RadialElectrolytic":
            volts = (el.findtext("voltage") or "").lstrip("_")
            add((f"5{value_text(v)}", t["bom"]["elec"].format(v=value_text(v), volts=volts)))
        elif kind == "RadialMicaCapacitor":
            add((f"6{value_text(v)}", t["bom"]["film"].format(v=value_text(v))))
        elif kind == "RadialCeramicDiskCapacitor":
            add((f"6{value_text(v)}", t["bom"]["ceramic"].format(v=value_text(v))))
        elif kind == "PCBTerminalBlock":
            add(("7", t["bom"]["tb"]))
    rows = [(n, name) for (_, name), n in sorted(count.items())]
    rows += [(n, t["bom"][key]) for n, key in BUILD[build]["extra_bom"]]
    rows += [(1, t["bom"]["board"]), (BUILD[build]["headers"], t["bom"]["headers"]), ("", t["bom"]["wire"])]
    c.setFont("Helvetica-Bold", 9)
    c.drawString(x, top, t["bom_title"])
    cols = BUILD[build].get("bom_cols", 1)
    per = -(-len(rows) // cols)
    lowest = top
    for k in range(cols):
        y = top - 5 * mm
        for n, name in rows[k * per:(k + 1) * per]:
            c.setFont("Helvetica-Bold", 7.5)
            c.drawRightString(x + k * 100 * mm + 18 * mm, y, str(n))
            c.setFont("Helvetica", 7.5)
            c.drawString(x + k * 100 * mm + 21 * mm, y, name)
            y -= 3.6 * mm
        lowest = min(lowest, y)
    c.setFont("Helvetica-Oblique", 7)
    c.drawString(x, lowest - 1 * mm, t["bom"]["extra"])


def draw(path, out, lang, build="cable-qr"):
    root = ET.parse(path).getroot()
    comps = list(root.find("components"))
    board = Board(next(c for c in comps if c.tag.endswith("Breadboard")))
    s = Sheet(out, board, TEXT[build][lang])
    c, t = s.c, s.t
    bx0, by0, bx1, by1 = board.bbox()

    def on_board(q):
        return bx0 <= q[0] <= bx1 and by0 <= q[1] <= by1

    # What a lead that leaves the board reaches: a pad of a module drawn off
    # the board, or the label at its free end.
    pads = {}
    for el in comps:
        if el.tag.endswith(".MT3608BoostConverter"):
            for q, pin in zip(pts(el, "controlPoints"), MT3608_PADS):
                pads[(round(q[0], 3), round(q[1], 3))] = "MT3608 " + pin
    labels = [((float(el.find("point").get("x")), float(el.find("point").get("y"))), el.findtext("text"))
              for el in comps if el.tag.endswith(".Label")]

    def lead_target(raw):
        """A lead that leaves the board: its hole, its free end and where it goes."""
        hole, free = (raw[0], raw[-1]) if on_board(raw[0]) else (raw[-1], raw[0])
        name = pads.get((round(free[0], 3), round(free[1], 3)))
        if name is None:
            near = sorted((abs(q[0] - free[0]) + abs(q[1] - free[1]), txt) for q, txt in labels
                          if abs(q[0] - free[0]) <= 0.4 and abs(q[1] - free[1]) <= 0.4)
            name = near[0][1] if near else ""
        return hole, free, name

    def lead_tag(raw):
        """Names, at the board's edge, where a lead that leaves the board goes, so
        the board still says it once it is cut out."""
        hole, free, name = lead_target(raw)
        name = t.get("labels", {}).get(name, name)
        seq = raw if hole is raw[0] else raw[::-1]
        nxt = next((q for q in seq[1:] if q != hole), hole)
        if nxt[0] < hole[0] - 1e-6 and abs(nxt[1] - hole[1]) < 1e-6:  # leaves by the left end
            s.text(bx0 + 0.07, hole[1] + 0.3, name, size=3, angle=90, anchor="start", bold=True, bg=True)
        elif nxt[1] > hole[1]:  # leaves by the bottom edge
            s.text(hole[0] - 0.05, by1 - 0.03, name, size=3, angle=90, anchor="start", bold=True, bg=True)
        else:  # by the top edge
            s.text(hole[0] - 0.05, by0 + 0.03, name, size=3, angle=90, anchor="end", bold=True, bg=True)

    # Header, calibration bar. Everything stays 22 mm from the sides, so the
    # page also prints whole at actual size on Letter paper (9 mm narrower
    # on each side than A4 landscape).
    pw, ph = landscape(A4)
    margin = 22 * mm
    c.setFont("Helvetica-Bold", 11)
    c.drawString(margin, ph - 15 * mm, t["title"])
    c.setFont("Helvetica", 8)
    c.drawString(margin, ph - 21 * mm, f"{REPO}   ·   {t['licence']}")
    bx, by = margin, 20 * mm
    c.setLineWidth(0.8)
    c.line(bx, by, bx + 100 * mm, by)
    for i in range(11):
        c.line(bx + i * 10 * mm, by, bx + i * 10 * mm, by + (3 if i % 5 else 5) * mm)
    c.setFont("Helvetica", 8)
    c.drawString(bx, by - 5 * mm, t["print"])

    # The print warning, boxed, bottom right.
    wx, wy, ww, wh = pw - margin - 105 * mm, 14 * mm, 105 * mm, 26 * mm
    c.setLineWidth(1.4)
    c.rect(wx, wy, ww, wh)
    c.setFont("Helvetica-Bold", 11)
    c.drawString(wx + 4 * mm, wy + wh - 7 * mm, t["warn"][0])
    c.setFont("Helvetica", 8)
    for i, line in enumerate(t["warn"][1:]):
        c.drawString(wx + 4 * mm, wy + wh - 13 * mm - i * 4.2 * mm, line)
    c.setLineWidth(0.8)

    # The parts list, read from the layout, under the board and under what is
    # drawn off the board below it.
    low = board.bbox()[3]
    for el in comps:
        for q in el.iter("point"):
            low = max(low, float(q.get("y")))
        if el.tag.endswith(".MT3608BoostConverter"):
            low = max(low, pts(el, "controlPoints")[0][1] - 0.12 + 17 / 25.4)
    draw_bom(c, t, comps, margin, s.xy(0, low)[1] - 8 * mm, build)

    # Board outline, holes, rails, row and column marks.
    x0, y0, x1, y1 = board.bbox()
    (ax, ay), (bx2, by2) = s.xy(x0, y0), s.xy(x1, y1)
    c.setLineWidth(0.6)
    c.rect(ax, by2, bx2 - ax, ay - by2)
    if BUILD[build].get("scissors"):
        # Scissors on the outline: the board is cut out along it, and what is
        # drawn off the board stays on the sheet.
        for sx, sy, angle in ((ax + (bx2 - ax) * 0.25, ay, 0), (ax + (bx2 - ax) * 0.3, by2, 180),
                              (ax, (ay + by2) / 2, 90), (bx2, (ay + by2) / 2, 270)):
            scissors(c, sx, sy, angle)
    used = set()
    for el in comps:
        kind = el.tag.split(".")[-1]
        if kind == "OrthogonalWire":
            q = pts(el, "controlPoints")
            used.update([q[0], q[-1]])
        elif kind in ("Resistor", "RadialElectrolytic", "RadialMicaCapacitor", "RadialCeramicDiskCapacitor"):
            used.update(pts(el, "points")[:2])
        elif kind in ("TactileMicroSwitch", "PCBTerminalBlock", "OLEDDisplay", "TransistorTO220"):
            used.update(pts(el, "controlPoints"))
        elif kind == "RaspberryPiPico":
            used.update(pts(el, "controlPoints")[:40])
    used = {(round(x, 2), round(y, 2)) for x, y in used}
    for row in range(1, 64):
        for u in Board.U_HOLES:
            x, y = board.point(row, u)
            hx, hy = s.xy(x, y)
            if (round(x, 2), round(y, 2)) in used:
                c.setStrokeGray(0)
                c.setLineWidth(0.5)
                c.circle(hx, hy, 0.55 * mm)
            else:  # unused: barely there, only to line the sheet up with the board
                c.setStrokeGray(0.85)
                c.setLineWidth(0.15)
                c.circle(hx, hy, 0.4 * mm)
    c.setStrokeGray(0)
    letters = "abcdefghij"
    for i, u in enumerate([0.6 + 0.1 * k for k in range(5)] + [1.3 + 0.1 * k for k in range(5)]):
        for row in (0, 64):
            s.text(*board.point(row, u), letters[i], size=4)
    # Everything stays on the board: the sheet is cut along its outline. Rail
    # names go beside their rail (outer rails in the margin, inner rails in the
    # gap before the strips); they are drawn last, over the wires.
    late = []
    for u, lab, rows in zip((0.12, 0.42, 1.88, 2.18), t["rails"], BUILD[build]["rail_rows"]):
        for row in rows:
            late.append(lambda r=row, u=u, lab=lab: s.text(*board.point(r, u), lab, size=4, bold=True, bg=True))

    # Parts. Their signs and names are drawn last, over the wires.
    for el in comps:
        kind = el.tag.split(".")[-1]
        name = el.findtext("name")
        if kind == "Resistor":
            (xa, ya), (xb, yb) = pts(el, "points")[:2]
            pa, pb = s.xy(xa, ya), s.xy(xb, yb)
            c.setLineWidth(0.5)
            c.line(*pa, *pb)
            mx, my = (xa + xb) / 2, (ya + yb) / 2
            v = el.find("value")
            unit = "" if v is not None and v.get("unit") == "R" else (v.get("unit").lower() if v is not None else "")
            val = f"{float(v.get('value')):g}{unit}" if v is not None else ""
            c.setFillGray(1)
            cx, cy = s.xy(mx, my)
            c.circle(cx, cy, 1.6 * mm, fill=1)
            c.setFillGray(0)
            s.text(mx, my, val, size=3.2, bold=True)
        elif kind in ("RadialElectrolytic", "RadialMicaCapacitor", "RadialCeramicDiskCapacitor"):
            (xa, ya), (xb, yb) = pts(el, "points")[:2]
            mx, my = (xa + xb) / 2, (ya + yb) / 2
            cx, cy = s.xy(mx, my)
            c.setLineWidth(0.5)
            c.line(*s.xy(xa, ya), *s.xy(xb, yb))
            c.setFillGray(1)
            if kind == "RadialElectrolytic":
                # The first lead is +. A black band on the - side, as on the part itself.
                c.circle(cx, cy, 2.6 * mm, fill=1)
                c.setFillGray(0)
                pbx, pby = s.xy(xb, yb)
                dx, dy = pbx - cx, pby - cy
                n = (dx * dx + dy * dy) ** 0.5 or 1
                c.saveState()
                p = c.beginPath()
                p.circle(cx, cy, 2.6 * mm)
                c.clipPath(p, stroke=0)
                c.translate(cx, cy)
                c.rotate(__import__("math").degrees(__import__("math").atan2(dy, dx)))
                c.rect(1.3 * mm, -3 * mm, 1.4 * mm, 6 * mm, fill=1, stroke=0)
                c.restoreState()
                # A + inside the circle, on the + lead's side; the band marks the - side.
                pax, pay = s.xy(xa, ya)
                ux_, uy_ = pax - cx, pay - cy
                n = (ux_ * ux_ + uy_ * uy_) ** 0.5 or 1
                px_, py_ = cx + ux_ / n * 1.75 * mm, cy + uy_ / n * 1.75 * mm

                def plus(px_=px_, py_=py_):
                    c.setFont("Helvetica-Bold", 6)
                    c.drawCentredString(px_, py_ - 2.1, "+")
                late.append(plus)
            elif kind == "RadialCeramicDiskCapacitor":  # a disc: rounded ends
                c.roundRect(cx - 2.2 * mm, cy - 1.2 * mm, 4.4 * mm, 2.4 * mm, 1.2 * mm, fill=1)
                c.setFillGray(0)
            else:
                c.rect(cx - 2 * mm, cy - 1.2 * mm, 4 * mm, 2.4 * mm, fill=1)
                c.setFillGray(0)
            v = el.find("value")
            vx, vy = (mx + (xb - xa) * 0.02, my + (yb - ya) * 0.02) if kind == "RadialElectrolytic" else (mx, my)
            s.text(vx, vy, f"{float(v.get('value')):g}{v.get('unit').replace('u', 'µ')}", size=2.6 if kind == "RadialElectrolytic" else 3.0, bold=True)
        elif kind == "TactileMicroSwitch":
            p = pts(el, "controlPoints")
            mx, my = sum(q[0] for q in p) / 4, sum(q[1] for q in p) / 4
            cx, cy = s.xy(mx, my)
            c.setLineWidth(0.6)
            c.rect(cx - 3 * mm, cy - 3 * mm, 6 * mm, 6 * mm)
            c.circle(cx, cy, 1.7 * mm)
            key = el.findtext("value") or ""
            label = t["buttons"].get(key, "")
            # Just below the switch: the name between rows i and j, the icon under it.
            lx, ly = (mx, board.point(0, 1.65)[1]) if board.rot else (board.point(0, 1.65)[0], my)
            ix_, iy_ = (mx, board.point(0, 1.765)[1]) if board.rot else (board.point(0, 1.765)[0], my)

            def button_label(lx=lx, ly=ly, ix_=ix_, iy_=iy_, key=key, label=label):
                s.text(lx, ly, label, size=3.6, bold=True, bg=True)
                bx, by = s.xy(ix_, iy_)
                c.setFillGray(1)
                c.circle(bx, by, 1.2 * mm, fill=1, stroke=0)
                c.setFillGray(0)
                icon(c, key, bx, by, 1.9 * mm)
            late.append(button_label)
        elif kind == "PCBTerminalBlock":
            p = pts(el, "controlPoints")
            xs = [s.xy(*q)[0] for q in p]
            ys = [s.xy(*q)[1] for q in p]
            c.setLineWidth(0.6)
            c.rect(min(xs) - 2.5 * mm, min(ys) - 3.8 * mm, max(xs) - min(xs) + 5 * mm,
                   max(ys) - min(ys) + 7.6 * mm)
            for q, lab, key in zip(p, t["tb"], ("OUT", "COM", "IN")):
                def tb_label(q=q, lab=lab, key=key):
                    # Icon and name on one line, between the block's lower edge and the rail below.
                    w = c.stringWidth(lab, "Helvetica-Bold", 3.7) / INCH
                    x0 = q[0] - (w + 0.08) / 2
                    ty = q[1] + 0.193
                    ix, iy = s.xy(x0 + 0.03, ty)
                    c.setFillGray(1)
                    c.rect(ix - 1.1 * mm, iy - 1.3 * mm, (w + 0.08) * INCH + 0.6 * mm, 2.6 * mm, fill=1, stroke=0)
                    c.setFillGray(0)
                    icon(c, key, ix, iy, 1.8 * mm)
                    s.text(x0 + 0.07, ty, lab, size=3.7, bold=True, anchor="start")
                late.append(tb_label)
            # The block's name under its terminals, just above the rail.
            late.append(lambda p=p: s.text(sum(q[0] for q in p) / len(p), p[0][1] + 0.257,
                                           t["tb_name"], size=3.2, bold=True, bg=True))
        elif kind == "TransistorTO220":
            # Standing on its three legs (in a row along the board): a box over
            # them that reaches into the centre channel, with the part's name and
            # each leg's name above its hole.
            p = pts(el, "controlPoints")
            (x0_, y0_), (x1_, y1_) = s.xy(min(q[0] for q in p), min(q[1] for q in p) - 0.2), \
                s.xy(max(q[0] for q in p), max(q[1] for q in p))
            c.setLineWidth(0.6)
            c.setFillGray(1)
            c.rect(min(x0_, x1_) - 1.6 * mm, min(y0_, y1_) - 1.6 * mm, abs(x1_ - x0_) + 3.2 * mm,
                   abs(y1_ - y0_) + 3.2 * mm, fill=1)
            c.setFillGray(0)
            value = el.findtext("value") or ""
            mx, my = sum(q[0] for q in p) / 3, sum(q[1] for q in p) / 3
            s.text(mx, my - 0.165, value, size=4.2, bold=True)
            for q, pin in zip(p, TO220_PINS.get(value, ("1", "2", "3"))):
                c.circle(*s.xy(*q), 0.55 * mm)
                s.text(q[0], q[1] - 0.085, pin, size=2.6, bold=True)
        elif kind == "MT3608BoostConverter":
            # Off the board: DIYLC's 36 x 17 mm board, its first pad 22 and 24 px
            # (200 px to the inch) in from the corner; pads named as on the board.
            p = pts(el, "controlPoints")
            x0_, y0_ = p[0][0] - 0.11, p[0][1] - 0.12
            (ax_, ay_), (bx_, by_) = s.xy(x0_, y0_), s.xy(x0_ + 36 / 25.4, y0_ + 17 / 25.4)
            c.setLineWidth(0.6)
            c.roundRect(ax_, by_, bx_ - ax_, ay_ - by_, 1.5 * mm)
            mx, my = x0_ + 18 / 25.4, y0_ + 8.5 / 25.4
            s.text(mx, my - 0.05, "MT3608", size=6, bold=True)
            s.text(mx, my + 0.07, "5 - 12 V", size=4.5)
            for q, pin in zip(p, MT3608_PADS):
                c.circle(*s.xy(*q), 1.0 * mm)
                # Each name towards the middle of the board, clear of the wires
                # that reach the pads from above or from the side.
                s.text(q[0], q[1] + (0.1 if q[1] < my else -0.1), pin, size=4, bold=True)
        elif kind == "Label":
            # Notes DIYLC shows beside the parts off the board. The turned ones
            # name a lead, which its tag at the board's edge already does.
            q = (float(el.find("point").get("x")), float(el.find("point").get("y")))
            if not on_board(q) and el.findtext("orientation") not in ("_90", "_270"):
                anchor = {"LEFT": "start", "RIGHT": "end"}.get(el.findtext("horizontalAlignment"), "middle")
                text_ = el.findtext("text") or ""
                text_ = t.get("labels", {}).get(text_, text_)
                if text_.startswith("RJ11 "):  # beside the socket's box, which says RJ11
                    text_ = text_[5:]
                s.text(*q, text_, size=4.5, bold=True, anchor=anchor)
        elif kind == "RaspberryPiPico":
            p = pts(el, "controlPoints")[:40]
            xs = [s.xy(*q)[0] for q in p]
            ys = [s.xy(*q)[1] for q in p]
            cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
            horiz = max(xs) - min(xs) > max(ys) - min(ys)
            w, h = (51 * mm, 21 * mm) if horiz else (21 * mm, 51 * mm)
            c.setLineWidth(0.8)
            c.rect(cx - w / 2, cy - h / 2, w, h)
            c.setFont("Helvetica-Bold", 7)
            c.drawCentredString(cx, cy, "Raspberry Pi Pico")
            # Pin 1 and the USB end (pins 1 and 40 are at the USB end).
            p1x, p1y = s.xy(*p[0])
            c.circle(p1x, p1y, 1.0 * mm, fill=1)
            c.setFont("Helvetica", 4.5)
            c.drawCentredString(p1x, p1y + 1.6 * mm, "1")
            # DIYLC lists pins 1..20, then 40..21: p[20] is pin 40, beside pin 1.
            ux, uy = (s.xy(*p[0])[0] + s.xy(*p[20])[0]) / 2, (s.xy(*p[0])[1] + s.xy(*p[20])[1]) / 2
            ex = ux + (cx - ux) / abs(cx - ux) * 6 * mm if horiz and cx != ux else ux
            out_angle = (180 if cx > ux else 0) if horiz else (270 if cy > uy else 90)

            def usb_label(ex=ex, uy=uy, out_angle=out_angle):
                c.setFillGray(1)
                c.rect(ex - 3.5 * mm, uy - 4.2 * mm, 7 * mm, 8.6 * mm, fill=1, stroke=0)
                c.setFillGray(0)
                arrow(c, ex, uy + 2 * mm, out_angle, 3.4 * mm)
                c.setFont("Helvetica-Bold", 6)
                c.drawCentredString(ex, uy - 3 * mm, t["usb"])
            late.append(usb_label)
        elif kind == "OLEDDisplay":
            p = pts(el, "controlPoints")
            for q, lab in zip(p, ["GND", "VCC", "CLK", "MOSI", "RES", "DC", "CS"]):
                s.text(q[0], q[1] + 0.05, lab, size=3.0, angle=90, anchor="end", bg=True)
            xs = [s.xy(*q)[0] for q in p]
            ys = [s.xy(*q)[1] for q in p]
            cx = (min(xs) + max(xs)) / 2
            c.setDash(2, 2)
            c.setLineWidth(0.5)
            # The module (35.4 x 33.5 mm) lies on the side of its pins away from the channel.
            c.rect(cx - 35.4 * mm / 2, max(ys) + 1.5 * mm, 35.4 * mm, 33.5 * mm)
            c.setDash()
            if board.rot:
                late.append(lambda mx=sum(q[0] for q in p) / len(p): s.text(
                    mx, board.point(0, 0.48)[1], "OLED SH1106 1.3\" - " + t["screen"], size=4.5, bg=True))

    # Wires last, thin, over everything: an orthogonal path through the points.
    sockets = []
    c.setLineWidth(0.3)
    c.setStrokeGray(0.45)
    for el in comps:
        if el.tag.endswith("OrthogonalWire"):
            raw = pts(el, "controlPoints")
            p = [s.xy(*q) for q in raw]
            leaves = not (on_board(raw[0]) and on_board(raw[-1]))
            vertical = el.findtext("startDirection") == "VERTICAL"
            path = c.beginPath()
            path.moveTo(*p[0])
            for a, b in zip(p, p[1:]):
                corner = (a[0], b[1]) if vertical else (b[0], a[1])
                path.lineTo(*corner)
                path.lineTo(*b)
                vertical = not vertical
            c.drawPath(path)
            if leaves:
                late.append(lambda raw=raw: lead_tag(raw))
                if lead_target(raw)[2].startswith("RJ11"):
                    sockets.append(lead_target(raw)[1])
            for q in (p[0], p[-1]):
                c.circle(*q, 0.7 * mm, fill=1)
    c.setStrokeGray(0)
    if sockets:  # the telephone's RJ11 socket, off the board, where its two leads end
        (ax_, ay_), (bx_, by_) = s.xy(min(q[0] for q in sockets) - 0.08, min(q[1] for q in sockets) - 0.12), \
            s.xy(max(q[0] for q in sockets) + 0.08, max(q[1] for q in sockets) + 0.03)
        c.setLineWidth(0.6)
        c.setFillGray(1)
        c.rect(ax_, by_, bx_ - ax_, ay_ - by_, fill=1)
        c.setFillGray(0)
        s.text(sum(q[0] for q in sockets) / len(sockets), min(q[1] for q in sockets) - 0.05, "RJ11",
               size=4.5, bold=True)
    for draw_late in late:
        draw_late()

    # The free end of the board (rows 1..10): project, controls, addresses.
    # No QR code on the board: a wallet's camera reading the signer's screen
    # would read it first. The web tool's QR goes on the sheet, outside the
    # board, and is cut away with it.
    r0, r1, u0, u1 = BUILD[build]["info"]
    (ix0, iy0), (ix1, iy1) = s.xy(*board.point(r0, u0)), s.xy(*board.point(r1, u1))
    lx, rx = min(ix0, ix1), max(ix0, ix1)
    ty, bty = max(iy0, iy1), min(iy0, iy1)
    c.setFillGray(1)
    c.rect(lx, bty, rx - lx, ty - bty, fill=1, stroke=1)
    c.setFillGray(0)
    # A tall box holds one column; a wide one, the project and the addresses
    # on its left third and the electrical notes on the rest.
    wide = rx - lx > ty - bty
    ex, ew = (lx + (rx - lx) / 3, (rx - lx) * 2 / 3) if wide else (lx, rx - lx)
    cx = (lx + ex) / 2 if wide else (lx + rx) / 2
    yy = ty - 3.6 * mm
    for i, line in enumerate(t["info"]):
        c.setFont("Helvetica-Bold" if i == 0 else "Helvetica", 6 if i == 0 else 4.2)
        c.drawCentredString(cx, yy, line)
        yy -= 2.6 * mm if i else 3 * mm

    # The DTMF Audio block as it sits on the board: its terminals in their
    # real order (read from the layout), so the drawing cannot mislead.
    tb = next((el for el in comps if el.tag.endswith("PCBTerminalBlock")), None)
    if tb is not None:
        tp = pts(tb, "controlPoints")
        order = sorted(range(len(tp)), key=lambda i: s.xy(*tp[i])[0])  # left to right on the page
        names = [("OUT", "COM", "IN")[i] for i in order]
        yy -= 1 * mm
        c.setFont("Helvetica-Bold", 4.2)
        c.drawCentredString(cx, yy, t["tb_name"])
        cell = 6.5 * mm
        x0 = cx - cell * 1.5
        top = yy - 1.3 * mm
        c.setLineWidth(0.5)
        c.rect(x0, top - 7 * mm, cell * 3, 7 * mm)
        for k, name in enumerate(names):
            xk = x0 + cell * (k + 0.5)
            c.circle(xk, top - 2 * mm, 1.0 * mm)
            icon(c, name, xk, top - 4.6 * mm, 1.6 * mm)
            c.setFont("Helvetica-Bold", 3.8)
            c.drawCentredString(xk, top - 6.6 * mm, name)
        yy = top - 10 * mm

    # Short electrical warnings.
    from reportlab.lib.utils import simpleSplit
    if wide:
        yy = ty - 3.6 * mm
        c.setLineWidth(0.3)
        c.line(ex, ty - 1.5 * mm, ex, bty + 1.5 * mm)
    c.setFont("Helvetica-Bold", 4.2)
    c.drawString(ex + 1.5 * mm, yy, t["elec_title"])
    yy -= 2.4 * mm
    for para in t["elec"]:
        for line in simpleSplit(para, "Helvetica", 3.5, ew - 3 * mm):
            c.setFont("Helvetica", 3.5)
            c.drawString(ex + 1.5 * mm, yy, line)
            yy -= 1.75 * mm
        yy -= 0.6 * mm

    c.setFont("Helvetica", 3.8)
    c.drawCentredString(cx, bty + 5 * mm, WEB_TOOL.replace("https://", ""))
    c.drawCentredString(cx, bty + 2.4 * mm, REPO.replace("https://", ""))

    # The web tool's QR code on the sheet, outside the board (cut away).
    qr = QrCodeWidget(WEB_TOOL, barLevel="M")
    x_a, y_a, x_b, y_b = qr.getBounds()
    size = 22 * mm
    d = Drawing(size, size, transform=[size / (x_b - x_a), 0, 0, size / (y_b - y_a), 0, 0])
    d.add(qr)
    qx, qy = pw - margin - size, ph - 12 * mm - size
    renderPDF.draw(d, c, qx, qy)
    c.setFont("Helvetica", 6)
    c.drawRightString(qx - 2 * mm, qy + size - 4 * mm, t["qr"])
    c.drawRightString(qx - 2 * mm, qy + size - 8 * mm, WEB_TOOL)
    c.drawRightString(qx - 2 * mm, qy + size - 12 * mm, t["qr_note"])

    c.showPage()
    c.save()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("diy")
    ap.add_argument("--build", choices=sorted(BUILD), default="cable-qr")
    ap.add_argument("--lang", choices=["en", "es"], default="en")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    draw(a.diy, a.out, a.lang, a.build)


if __name__ == "__main__":
    main()
