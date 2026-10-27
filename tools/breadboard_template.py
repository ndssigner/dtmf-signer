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
        "tb_name": "DTMF Audio",
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
        "tb_name": "DTMF Audio",
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
    used = set()
    for el in comps:
        kind = el.tag.split(".")[-1]
        if kind == "OrthogonalWire":
            q = pts(el, "controlPoints")
            used.update([q[0], q[-1]])
        elif kind in ("Resistor", "RadialElectrolytic", "RadialMicaCapacitor"):
            used.update(pts(el, "points")[:2])
        elif kind in ("TactileMicroSwitch", "PCBTerminalBlock", "OLEDDisplay"):
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
    for u, lab, rows in ((0.12, t["rails"][0], (21, 53)), (0.42, t["rails"][1], (21, 53)),
                         (1.88, t["rails"][2], (47, 58)), (2.18, t["rails"][3], (21, 53))):
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
    for draw_late in late:
        draw_late()

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
