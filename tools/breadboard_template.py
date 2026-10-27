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

TEXT = {
    "en": {
        "title": "DTMF-Signer - Cable and QR - breadboard template",
        "print": "Print at 100 % (no scaling): this bar must measure 100 mm.",
        "licence": "Hardware: CERN-OHL-S v2",
        "buttons": {"LEFT": "LEFT", "UP": "UP", "OK": "OK", "DOWN": "DOWN", "RIGHT": "RIGHT"},
        "tb": ["OUT", "COM", "IN"],
        "rails": ["+3V3", "GND", "L (buttons) - NOT 3V3", "GND"],
        "info": ["DTMF-Signer", "Cable and QR", "", "Hold OK: sign", "Hold LEFT 3 s: wipe",
                 "Audio: IN / COM / OUT", ""],
        "qr": "ur-tones web tool",
        "usb": "USB",
        "screen": "screen (overhangs)",
    },
    "es": {
        "title": "DTMF-Signer - Cable y QR - plantilla de protoboard",
        "print": "Imprimir al 100 % (sin escalar): esta barra debe medir 100 mm.",
        "licence": "Hardware: CERN-OHL-S v2",
        "buttons": {"LEFT": "IZQ", "UP": "ARRIBA", "OK": "OK", "DOWN": "ABAJO", "RIGHT": "DCHA"},
        "tb": ["OUT", "COM", "IN"],
        "rails": ["+3V3", "GND", "L (botones) - NO es 3V3", "GND"],
        "info": ["DTMF-Signer", "Cable y QR", "", "Mantener OK: firmar", "Mantener IZQ 3 s: borrar",
                 "Audio: IN / COM / OUT", ""],
        "qr": "herramienta web ur-tones",
        "usb": "USB",
        "screen": "pantalla (sobresale)",
    },
}


def pts(el, tag):
    h = el.find(tag)
    return [] if h is None else [(float(p.get("x")), float(p.get("y"))) for p in h.findall("point")]


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
    def __init__(self, path, board, lang):
        # invariant: no creation date or time zone in the file, and the same bytes every run.
        self.c = canvas.Canvas(path, pagesize=landscape(A4), invariant=1)
        self.c.setAuthor("NDS-Signer")
        self.c.setTitle(TEXT[lang]["title"])
        self.c.setSubject(REPO)
        self.c.setCreator(REPO)
        self.board, self.t = board, TEXT[lang]
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


def draw(path, out, lang):
    root = ET.parse(path).getroot()
    comps = list(root.find("components"))
    board = Board(next(c for c in comps if c.tag.endswith("Breadboard")))
    s = Sheet(out, board, lang)
    c, t = s.c, s.t

    # Header, calibration bar.
    pw, ph = landscape(A4)
    c.setFont("Helvetica-Bold", 11)
    c.drawString(15 * mm, ph - 15 * mm, t["title"])
    c.setFont("Helvetica", 8)
    c.drawString(15 * mm, ph - 21 * mm, f"{REPO}   ·   {t['licence']}")
    bx, by = 15 * mm, 20 * mm
    c.setLineWidth(0.8)
    c.line(bx, by, bx + 100 * mm, by)
    for i in range(11):
        c.line(bx + i * 10 * mm, by, bx + i * 10 * mm, by + (3 if i % 5 else 5) * mm)
    c.drawString(bx, by - 5 * mm, t["print"])

    # Board outline, holes, rails, row and column marks.
    x0, y0, x1, y1 = board.bbox()
    (ax, ay), (bx2, by2) = s.xy(x0, y0), s.xy(x1, y1)
    c.setLineWidth(0.6)
    c.rect(ax, by2, bx2 - ax, ay - by2)
    c.setLineWidth(0.25)
    for row in range(1, 64):
        for u in Board.U_HOLES:
            hx, hy = s.xy(*board.point(row, u))
            c.circle(hx, hy, 0.45 * mm)
    letters = "abcdefghij"
    for i, u in enumerate([0.6 + 0.1 * k for k in range(5)] + [1.3 + 0.1 * k for k in range(5)]):
        for row in (0, 64):
            s.text(*board.point(row, u), letters[i], size=4)
    for row in range(5, 64, 5):
        for u in (-0.05, 2.35):
            s.text(*board.point(row, u), str(row), size=3.6)
    # Rail names outside the long edges: outer rail first.
    for (u, lab_outer, lab_inner) in ((-0.17, t["rails"][0], t["rails"][1]),
                                      (2.47, t["rails"][3], t["rails"][2])):
        for row in (12, 52):
            s.text(*board.point(row, u), f"{lab_outer} (outer)  ·  {lab_inner} (inner)"
                   if TEXT["en"] is t else f"{lab_outer} (exterior)  ·  {lab_inner} (interior)",
                   size=4.2, bold=True)

    # Parts.
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
            val = f"{float(v.get('value')):g}{v.get('unit').lower()}" if v is not None else ""
            c.setFillGray(1)
            cx, cy = s.xy(mx, my)
            c.circle(cx, cy, 1.6 * mm, fill=1)
            c.setFillGray(0)
            s.text(mx, my, val, size=3.2, bold=True)
        elif kind in ("RadialElectrolytic", "RadialMicaCapacitor"):
            (xa, ya), (xb, yb) = pts(el, "points")[:2]
            mx, my = (xa + xb) / 2, (ya + yb) / 2
            cx, cy = s.xy(mx, my)
            c.setLineWidth(0.5)
            c.line(*s.xy(xa, ya), *s.xy(xb, yb))
            c.setFillGray(1)
            if kind == "RadialElectrolytic":
                c.circle(cx, cy, 2.6 * mm, fill=1)
                c.setFillGray(0)
                s.text(xa + (xa - mx) * 0.6, ya + (ya - my) * 0.6, "+", size=6, bold=True)
            else:
                c.rect(cx - 2 * mm, cy - 1.2 * mm, 4 * mm, 2.4 * mm, fill=1)
                c.setFillGray(0)
            v = el.find("value")
            s.text(mx, my, f"{float(v.get('value')):g}{v.get('unit').replace('u', 'µ')}", size=3.2, bold=True)
        elif kind == "TactileMicroSwitch":
            p = pts(el, "controlPoints")
            mx, my = sum(q[0] for q in p) / 4, sum(q[1] for q in p) / 4
            cx, cy = s.xy(mx, my)
            c.setLineWidth(0.6)
            c.rect(cx - 3 * mm, cy - 3 * mm, 6 * mm, 6 * mm)
            c.circle(cx, cy, 1.7 * mm)
            label = t["buttons"].get(el.findtext("value") or "", "")
            lx, ly = board.point(0, 1.235)
            s.text(mx if board.rot else lx, ly if board.rot else my, label, size=5.5, bold=True, bg=True)
        elif kind == "PCBTerminalBlock":
            p = pts(el, "controlPoints")
            xs = [s.xy(*q)[0] for q in p]
            ys = [s.xy(*q)[1] for q in p]
            c.setLineWidth(0.6)
            c.rect(min(xs) - 2.5 * mm, min(ys) - 3.8 * mm, max(xs) - min(xs) + 5 * mm,
                   max(ys) - min(ys) + 7.6 * mm)
            for q, lab in zip(p, t["tb"]):
                s.text(q[0], q[1] - 0.17, lab, size=4.5, bold=True, bg=True)
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
            ex = ux - (cx - ux) / abs(cx - ux) * 4 * mm if horiz and cx != ux else ux
            c.saveState()
            c.translate(ex, uy)
            c.rotate(90 if horiz else 0)
            c.setFont("Helvetica-Bold", 6)
            c.drawCentredString(0, -2, t["usb"])
            c.restoreState()
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
            c.setFont("Helvetica", 6)
            c.drawCentredString(cx, max(ys) + 31 * mm, "OLED SH1106 1.3\" - " + t["screen"])

    # Wires last, thin, over everything: an orthogonal path through the points.
    c.setLineWidth(0.3)
    c.setStrokeGray(0.45)
    for el in comps:
        if el.tag.endswith("OrthogonalWire"):
            p = [s.xy(*q) for q in pts(el, "controlPoints")]
            vertical = el.findtext("startDirection") == "VERTICAL"
            path = c.beginPath()
            path.moveTo(*p[0])
            for a, b in zip(p, p[1:]):
                corner = (a[0], b[1]) if vertical else (b[0], a[1])
                path.lineTo(*corner)
                path.lineTo(*b)
                vertical = not vertical
            c.drawPath(path)
            for q in (p[0], p[-1]):
                c.circle(*q, 0.7 * mm, fill=1)
    c.setStrokeGray(0)

    # The free end of the board (rows 1..10): project, controls, QR of the web tool.
    (ix0, iy0), (ix1, iy1) = s.xy(*board.point(10.5, 0.1)), s.xy(*board.point(0.5, 2.2))
    lx, rx = min(ix0, ix1), max(ix0, ix1)
    ty, bty = max(iy0, iy1), min(iy0, iy1)
    c.setFillGray(1)
    c.rect(lx, bty, rx - lx, ty - bty, fill=1, stroke=1)
    c.setFillGray(0)
    cx = (lx + rx) / 2
    yy = ty - 4 * mm
    for i, line in enumerate(t["info"]):
        c.setFont("Helvetica-Bold" if i == 0 else "Helvetica", 6 if i == 0 else 4.4)
        c.drawCentredString(cx, yy, line)
        yy -= 3 * mm
    qr = QrCodeWidget(WEB_TOOL, barLevel="M")
    x_a, y_a, x_b, y_b = qr.getBounds()
    size = min(rx - lx - 3 * mm, 19 * mm)
    d = Drawing(size, size, transform=[size / (x_b - x_a), 0, 0, size / (y_b - y_a), 0, 0])
    d.add(qr)
    renderPDF.draw(d, c, cx - size / 2, bty + 6 * mm)
    c.setFont("Helvetica", 4.2)
    c.drawCentredString(cx, bty + 3.6 * mm, t["qr"])
    c.drawCentredString(cx, bty + 1.6 * mm, WEB_TOOL.replace("https://", ""))

    c.showPage()
    c.save()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("diy")
    ap.add_argument("--lang", choices=sorted(TEXT), default="en")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    draw(a.diy, a.out, a.lang)


if __name__ == "__main__":
    main()
