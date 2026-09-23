"""Render docs/theory.html -> THEORY.pdf via headless Chrome/Edge.

Usage: py tools/make_theory.py
Requires a local Chrome or Edge install; MathJax loads from CDN.
"""

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "theory.html"
DST = ROOT / "THEORY.pdf"

CANDIDATES = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]


def find_browser() -> str:
    for c in CANDIDATES:
        if Path(c).exists():
            return c
    for name in ("chrome", "msedge"):
        found = shutil.which(name)
        if found:
            return found
    sys.exit("no Chrome/Edge found — needed for print-to-pdf")


def main() -> int:
    if not SRC.exists():
        print(f"missing {SRC}", file=sys.stderr)
        return 1
    browser = find_browser()
    url = SRC.as_uri()
    cmd = [
        browser,
        "--headless=new",
        "--disable-gpu",
        "--no-pdf-header-footer",
        "--virtual-time-budget=20000",   # let MathJax CDN load + typeset
        f"--print-to-pdf={DST}",
        url,
    ]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if not DST.exists() or DST.stat().st_size < 10_000:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        sys.exit("pdf not produced (or suspiciously small)")
    print(f"wrote {DST} ({DST.stat().st_size // 1024} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
