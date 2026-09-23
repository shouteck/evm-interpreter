"""Render THEORY.md -> THEORY.pdf. Usage: py tools/make_theory.py

Markdown is rendered with headings/code/bullets, plus HTML-comment
markers `<!-- diagram:NAME -->` that draw real vector diagrams.
"""

import math
import re
import sys
from pathlib import Path

from fpdf import FPDF

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "THEORY.md"
DST = ROOT / "THEORY.pdf"

REPLACEMENTS = {
    "\u2014": "-", "\u2013": "-", "\u2018": "'", "\u2019": "'",
    "\u201c": '"', "\u201d": '"', "\u2192": "->", "\u2026": "...",
    "\u2264": "<=", "\u2265": ">=", "\u00d7": "x", "\u2261": "==",
}

BLUE = (30, 90, 180)
RED = (200, 60, 40)
ORANGE = (200, 120, 20)
GRAY = (130, 130, 130)


def clean(text: str) -> str:
    for k, v in REPLACEMENTS.items():
        text = text.replace(k, v)
    return text.encode("latin-1", "replace").decode("latin-1")


class Guide(FPDF):
    def header(self):
        self.set_font("helvetica", "I", 8)
        self.set_text_color(120)
        self.cell(0, 8, "EVM - the clerk's arithmetic manual", align="R",
                  new_x="LMARGIN", new_y="NEXT")
        self.set_text_color(0)

    def footer(self):
        self.set_y(-12)
        self.set_font("helvetica", "I", 8)
        self.set_text_color(120)
        self.cell(0, 8, f"page {self.page_no()}", align="C")


# ---------------------------------------------------------------- helpers

def label(pdf, x, y, w, s, size=7.5, style="", align="C", color=None):
    if color:
        pdf.set_text_color(*color)
    pdf.set_font("helvetica", style, size)
    pdf.set_xy(x, y)
    pdf.cell(w, 3.5, s, align=align)
    pdf.set_text_color(0)


def arrow(pdf, x1, y1, x2, y2, size=2.2):
    pdf.line(x1, y1, x2, y2)
    ang = math.atan2(y2 - y1, x2 - x1)
    for da in (math.radians(150), math.radians(-150)):
        pdf.line(x2, y2, x2 + size * math.cos(ang + da),
                 y2 + size * math.sin(ang + da))


def arc(pdf, cx, cy, r, deg0, deg1, n=36):
    """Polyline arc; degrees measured clockwise from the top."""
    def pt(d):
        t = math.radians(d)
        return cx + r * math.sin(t), cy - r * math.cos(t)

    x0, y0 = pt(deg0)
    step = (deg1 - deg0) / n
    for k in range(1, n + 1):
        x1, y1 = pt(deg0 + step * k)
        pdf.line(x0, y0, x1, y1)
        x0, y0 = x1, y1
    return x0, y0


def arc_arrow(pdf, cx, cy, r, deg0, deg1, n=36, size=2.2):
    x_prev, y_prev = None, None
    def pt(d):
        t = math.radians(d)
        return cx + r * math.sin(t), cy - r * math.cos(t)

    x0, y0 = pt(deg0)
    step = (deg1 - deg0) / n
    for k in range(1, n + 1):
        x_prev, y_prev = x0, y0
        x1, y1 = pt(deg0 + step * k)
        pdf.line(x0, y0, x1, y1)
        x0, y0 = x1, y1
    ang = math.atan2(y0 - y_prev, x0 - x_prev)
    for da in (math.radians(150), math.radians(-150)):
        pdf.line(x0, y0, x0 + size * math.cos(ang + da),
                 y0 + size * math.sin(ang + da))


def room(pdf, h):
    if pdf.get_y() + h > pdf.page_break_trigger:
        pdf.add_page()


# ---------------------------------------------------------------- diagrams

def diag_clock(pdf):
    """The mod-2^256 ring: walk clockwise to add, wrap at the top."""
    h = 56
    room(pdf, h)
    y = pdf.get_y() + 4
    cx, cy, r = pdf.w / 2, y + h / 2, 17

    pdf.set_draw_color(*GRAY)
    pdf.ellipse(cx - r, cy - r, 2 * r, 2 * r)
    pdf.set_draw_color(*BLUE)          # left half = the negative readings
    arc(pdf, cx, cy, r, 180, 360)
    pdf.set_draw_color(0)

    def mark(deg, txt, color=None):
        t = math.radians(deg)
        px, py = cx + r * math.sin(t), cy - r * math.cos(t)
        pdf.set_fill_color(30)
        pdf.ellipse(px - 0.8, py - 0.8, 1.6, 1.6, style="F")
        lx = cx + (r + 9) * math.sin(t) - 20
        ly = cy - (r + 9) * math.cos(t) - 1.75
        label(pdf, lx, ly, 40, txt, 8, "B", "C", color)

    mark(0, "0")
    mark(135, "-2^255", BLUE)
    mark(330, "-1 = MAX", BLUE)
    mark(55, "1, 2, 3, ...")

    pdf.set_draw_color(*RED)           # the wrap: MAX + 1 -> 0
    arc_arrow(pdf, cx, cy, r + 4.5, 330, 392)
    label(pdf, cx - 30, cy - r - 11, 60, "MAX + 1 wraps to 0",
          8, "B", "C", RED)
    pdf.set_draw_color(0)

    label(pdf, cx - r - 62, cy - 6, 42,
          "the negative half\n(bit 255 set)", 7.5, "", "L", BLUE)
    label(pdf, cx + r + 6, cy - 2, 46, "walk clockwise: +1 each step",
          7.5, "", "L", GRAY)
    pdf.set_y(y + h)


def diag_carry(pdf):
    """U(M,0,0,0) + 1: wheel 0 rolls over and clicks wheel 1."""
    h = 46
    room(pdf, h)
    y = pdf.get_y() + 8
    cw, ch, gap = 20, 8, 4
    x0 = (pdf.w - (4 * cw + 3 * gap)) / 2 + 8

    for r_i, (name, vals) in enumerate(
            [("a", ["0", "0", "0", "M"]), ("+ b", ["0", "0", "0", "1"])]):
        ry = y + r_i * 10
        label(pdf, x0 - 24, ry + 2, 20, name, 9, "B", "R")
        for i, v in enumerate(vals):
            x = x0 + i * (cw + gap)
            pdf.rect(x, ry, cw, ch)
            label(pdf, x, ry + 2, cw, v, 9)

    sep = y + 20
    pdf.line(x0, sep, x0 + 4 * cw + 3 * gap, sep)
    ry = sep + 3
    for i, v in enumerate(["0", "0", "1", "0"]):
        x = x0 + i * (cw + gap)
        pdf.rect(x, ry, cw, ch)
        label(pdf, x, ry + 2, cw, v, 9, "B")

    ya = y - 5                                  # carry: l[0] -> l[1]
    c_from = x0 + 3 * (cw + gap) + cw / 2
    c_to = x0 + 2 * (cw + gap) + cw / 2
    pdf.set_draw_color(*RED)
    pdf.line(c_from, y - 1, c_from, ya)
    pdf.line(c_from, ya, c_to, ya)
    arrow(pdf, c_to, ya, c_to, y - 1)
    label(pdf, (c_from + c_to) / 2 - 15, ya - 4.5, 30, "carry 1",
          7.5, "B", "C", RED)
    pdf.set_draw_color(0)
    pdf.set_y(ry + ch + 6)


def diag_doorway(pdf):
    """Left shift: bits cross the limb boundary through a doorway."""
    h = 50
    room(pdf, h)
    y = pdf.get_y() + 4
    cell, n = 7.5, 8
    lw = cell * n
    x0 = (pdf.w - 2 * lw) / 2
    yrow = y + 16

    label(pdf, x0, y + 10, lw, "l[1]", 9, "B")
    label(pdf, x0 + lw, y + 10, lw, "l[0]", 9, "B")

    for i in range(2 * n):                      # l[0] top bits -> orange,
        x = x0 + i * cell                       # l[1] landing bits -> blue
        if i in (8, 9, 10):
            pdf.set_fill_color(255, 220, 150)
        elif i in (5, 6, 7):
            pdf.set_fill_color(150, 210, 255)
        else:
            pdf.set_fill_color(255, 255, 255)
        pdf.rect(x, yrow, cell, 7, style="DF")
    pdf.set_line_width(0.7)                     # the doorway
    pdf.line(x0 + lw, yrow, x0 + lw, yrow + 7)
    pdf.set_line_width(0.2)

    pdf.set_draw_color(*ORANGE)                 # spill: l[0] top -> l[1] bot
    xf = x0 + 9 * cell
    xt = x0 + 6.5 * cell
    pdf.line(xf, yrow - 1, xf, yrow - 9)
    pdf.line(xf, yrow - 9, xt, yrow - 9)
    arrow(pdf, xt, yrow - 9, xt, yrow - 1)
    label(pdf, xt - 58, yrow - 13.5, 64,
          "spill: l[0] >> (64 - 10)", 7.5, "B", "L", ORANGE)
    pdf.set_draw_color(0)

    pdf.set_draw_color(*GRAY)                   # zeros in / bits out
    arrow(pdf, x0 + 2 * lw + 10, yrow + 3.5, x0 + 2 * lw + 2, yrow + 3.5)
    label(pdf, x0 + 2 * lw + 12, yrow + 2, 30, "zeros in", 7.5, "", "L", GRAY)
    arrow(pdf, x0 - 2, yrow + 3.5, x0 - 10, yrow + 3.5)
    label(pdf, x0 - 34, yrow + 7.5, 32, "off the page", 7.5, "", "L", GRAY)
    pdf.set_draw_color(0)
    pdf.set_y(yrow + 13)


def diag_columns(pdf):
    """Multiplication: one tile ai*bj lands its halves on two columns."""
    h = 52
    room(pdf, h)
    y = pdf.get_y() + 6
    cw, ch, gap = 22, 9, 3
    x0 = (pdf.w - (5 * cw + 4 * gap)) / 2
    ycols = y + 28

    for i, name in enumerate(
            ["col 4", "col 3", "col 2", "col 1", "col 0"]):
        x = x0 + i * (cw + gap)
        if i == 0:
            pdf.set_fill_color(240, 240, 240)
            pdf.rect(x, ycols, cw, ch, style="DF")
            label(pdf, x, ycols + ch + 1.5, cw, "off page!",
                  7, "I", "C", GRAY)
        else:
            pdf.rect(x, ycols, cw, ch)
        label(pdf, x, ycols + 2.5, cw, name, 8, "B")

    mid = x0 + 1.5 * (cw + gap) + cw / 2        # tile sits over col3|col2
    tw = 60
    pdf.set_fill_color(235, 242, 255)
    pdf.rect(mid - tw / 2, y + 4, tw, 12, style="DF")
    label(pdf, mid - tw / 2, y + 8, tw,
          "a1 * b1 = hi : lo   (a 128-bit tile)", 8.5, "B")

    c3 = x0 + (cw + gap) + cw / 2
    c2 = x0 + 2 * (cw + gap) + cw / 2
    pdf.set_draw_color(*BLUE)
    arrow(pdf, mid - tw / 2 + 12, y + 16, c3, ycols - 1)
    label(pdf, mid - tw / 2 - 16, y + 18, 24, "hi", 8, "B", "C", BLUE)
    pdf.set_draw_color(*RED)
    arrow(pdf, mid + tw / 2 - 12, y + 16, c2, ycols - 1)
    label(pdf, mid + tw / 2 - 8, y + 18, 24, "lo", 8, "B", "C", RED)
    pdf.set_draw_color(0)
    pdf.set_y(ycols + ch + 9)


def diag_scoop(pdf):
    """Long division: drop a bit into the scoop, ask if the stick fits."""
    h = 56
    room(pdf, h)
    y = pdf.get_y() + 6
    cw, ch, gap = 12, 9, 2
    x0 = pdf.w / 2 - 70

    label(pdf, x0 - 26, y + 2.5, 24, "a = 13", 9, "B", "R")
    for i, b in enumerate(["1", "1", "0", "1"]):
        x = x0 + i * (cw + gap)
        pdf.rect(x, y, cw, ch)
        label(pdf, x, y + 2.5, cw, b, 9)
    label(pdf, x0 + 4 * (cw + gap) + 3, y + 2.5, 62,
          "peeled MSB first", 7.5, "", "L", GRAY)

    ry = y + 24
    pdf.set_draw_color(*BLUE)
    arrow(pdf, x0 + cw / 2, y + ch, x0 + cw / 2, ry - 1)
    label(pdf, x0 + 9, y + 15, 55, "next bit drops in",
          7.5, "", "L", BLUE)

    pdf.set_fill_color(255, 220, 150)
    pdf.rect(x0 - 6, ry, 30, 11, style="DF")
    label(pdf, x0 - 6, ry + 3.5, 30, "scoop r", 9, "B")
    bx = x0 + 62
    pdf.set_fill_color(150, 210, 255)
    pdf.rect(bx, ry, 30, 11, style="DF")
    label(pdf, bx, ry + 3.5, 30, "stick b", 9, "B")
    label(pdf, bx + 4, ry + 12.5, 40, "never changes", 7, "I", "L", GRAY)
    pdf.set_draw_color(0)
    label(pdf, x0 + 26, ry + 3.5, 34, "fits?", 8, "I")
    arrow(pdf, x0 + 24, ry + 5.5, bx - 2, ry + 5.5)
    label(pdf, x0 - 6, ry + 15.5, 150,
          "if r >= b:  r = r - b,  write a 1 into quotient bit i", 8)

    qy = ry + 23
    label(pdf, x0 - 26, qy + 2.5, 24, "q", 9, "B", "R")
    for i in range(4):
        x = x0 + i * (cw + gap)
        pdf.rect(x, qy, cw, ch)
    label(pdf, x0 + cw + gap + 1, qy + 2.5, cw, "1", 9, "B", "C", RED)
    label(pdf, x0 + 4 * (cw + gap) + 3, qy + 2.5, 70,
          "built MSB first, bit by bit", 7.5, "", "L", GRAY)
    pdf.set_y(qy + ch + 5)


DIAGRAMS = {
    "clock": diag_clock,
    "carry": diag_carry,
    "doorway": diag_doorway,
    "columns": diag_columns,
    "scoop": diag_scoop,
}

# ---------------------------------------------------------------- renderer

def render(md: str) -> None:
    pdf = Guide()
    pdf.set_auto_page_break(True, margin=18)
    pdf.set_margins(18, 14, 18)
    pdf.add_page()

    in_code = False
    for raw in md.splitlines():
        line = clean(raw.rstrip())

        if line.startswith("```"):
            in_code = not in_code
            pdf.ln(2)
            continue

        if in_code:
            pdf.set_font("courier", "", 8.5)
            pdf.set_fill_color(240, 240, 240)
            pdf.multi_cell(0, 4, "  " + line if line else " ",
                           fill=True, new_x="LMARGIN", new_y="NEXT")
            continue

        m = re.match(r"^<!--\s*diagram:(\w+)\s*-->", raw)
        if m and m.group(1) in DIAGRAMS:
            pdf.ln(2)
            DIAGRAMS[m.group(1)](pdf)
            continue

        if not line.strip():
            pdf.ln(3)
            continue

        m = re.match(r"^(#{1,4})\s+(.*)", line)
        if m:
            level = len(m.group(1))
            size = {1: 17, 2: 14, 3: 12, 4: 11}[level]
            pdf.ln(4 if level == 1 else 6)
            pdf.set_font("helvetica", "B", size)
            pdf.set_text_color(20, 60, 120)
            pdf.multi_cell(0, size * 0.55, m.group(2),
                           new_x="LMARGIN", new_y="NEXT")
            pdf.set_text_color(0)
            pdf.ln(1)
            continue

        m = re.match(r"^(\s*)-\s+(.*)", line)
        if m:
            pdf.set_x(pdf.l_margin + 6 + len(m.group(1)) * 2)
            pdf.set_font("helvetica", "", 10)
            pdf.multi_cell(0, 5, "- " + m.group(2),
                           new_x="LMARGIN", new_y="NEXT")
            continue

        m = re.match(r"^(\s*)(\d+)\.\s+(.*)", line)
        if m:
            pdf.set_x(pdf.l_margin + 6 + len(m.group(1)) * 2)
            pdf.set_font("helvetica", "", 10)
            pdf.multi_cell(0, 5, f"{m.group(2)}. {m.group(3)}",
                           new_x="LMARGIN", new_y="NEXT")
            continue

        if line.startswith("|"):
            pdf.set_font("courier", "", 8)
            pdf.multi_cell(0, 4.5, line, new_x="LMARGIN", new_y="NEXT")
            continue

        pdf.set_font("helvetica", "", 10)
        pdf.multi_cell(0, 5, line, new_x="LMARGIN", new_y="NEXT")

    pdf.output(str(DST))


def main() -> int:
    if not SRC.exists():
        print(f"missing {SRC}", file=sys.stderr)
        return 1
    render(SRC.read_text(encoding="utf-8"))
    print(f"wrote {DST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
