"""Render THEORY.md -> THEORY.pdf. Usage: py tools/make_theory.py"""

import re
import sys
from pathlib import Path

from fpdf import FPDF

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "THEORY.md"
DST = ROOT / "THEORY.pdf"

# fpdf core fonts are latin-1; map the common Unicode we use in the doc.
REPLACEMENTS = {
    "\u2014": "-", "\u2013": "-", "\u2018": "'", "\u2019": "'",
    "\u201c": '"', "\u201d": '"', "\u2192": "->", "\u2026": "...",
    "\u2264": "<=", "\u2265": ">=", "\u00d7": "x", "\u2261": "==",
}


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
            indent = 6 + len(m.group(1)) * 2
            pdf.set_font("helvetica", "", 10)
            pdf.set_x(pdf.l_margin + indent)
            pdf.multi_cell(0, 5, "- " + m.group(2),
                           new_x="LMARGIN", new_y="NEXT")
            continue

        m = re.match(r"^(\s*)(\d+)\.\s+(.*)", line)
        if m:
            indent = 6 + len(m.group(1)) * 2
            pdf.set_font("helvetica", "", 10)
            pdf.set_x(pdf.l_margin + indent)
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
