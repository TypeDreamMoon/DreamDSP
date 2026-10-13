"""Rebuild the raster icons from assets/logo.svg.

    python scripts/make-icons.py

Writes assets/logo.png (512 px) and assets/DreamDSP.ico (16..256 px). Both are
committed, so a build never needs this -- run it only after editing the SVG.

The SVG is rasterised by headless Edge or Chrome, because that is what a
Windows machine already has; the ICO is then assembled with Pillow.
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
SVG = ASSETS / "logo.svg"
RENDER = 1024
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]

BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
]


def find_browser() -> str:
    for candidate in BROWSERS:
        if os.path.exists(candidate):
            return candidate
    found = shutil.which("msedge") or shutil.which("chrome")
    if not found:
        sys.exit("need Edge or Chrome to rasterise the SVG")
    return found


def rasterise(browser: str, out: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        page = Path(tmp) / "page.html"
        page.write_text(
            "<html><body style='margin:0;background:transparent'>"
            f"<img src='{SVG.as_uri()}' width='{RENDER}' height='{RENDER}' style='display:block'>"
            "</body></html>",
            encoding="utf-8",
        )
        subprocess.run(
            [browser, "--headless", "--disable-gpu", "--hide-scrollbars",
             "--force-device-scale-factor=1", "--default-background-color=00000000",
             f"--window-size={RENDER},{RENDER}", f"--screenshot={out}", page.as_uri()],
            check=True, capture_output=True,
        )


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        big = Path(tmp) / "logo-1024.png"
        rasterise(find_browser(), big)
        master = Image.open(big).convert("RGBA")

    master.resize((512, 512), Image.LANCZOS).save(ASSETS / "logo.png")
    # Pillow downsizes the 256 master itself for each smaller entry; handing it
    # the 1024 render keeps the small sizes sharp.
    master.save(ASSETS / "DreamDSP.ico", sizes=[(s, s) for s in ICO_SIZES])
    print("wrote", ASSETS / "logo.png", "and", ASSETS / "DreamDSP.ico")


if __name__ == "__main__":
    main()
