#!/usr/bin/env python3
"""Render the built-in key icons (lib/deck_core/src/icons.h) as a contact sheet PNG.

Usage: python3 tools/icons_preview.py <out.png>
Needs Pillow. Parses the ASCII row strings in icons.h: 5x4 grid, 6x scale, name under each icon.
"""
import os
import re
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "lib", "deck_core", "src", "icons.h")
SCALE = 6
SIZE = 24
COLS, ROWS = 5, 4
CELL_W = SIZE * SCALE + 60
CELL_H = SIZE * SCALE + 50
BG = (11, 11, 26)        # kThemeBg
TILE = (90, 40, 60)      # a dim key tile colour, like the LCD
INK = (255, 255, 255)


def parse(text):
    """Return [(name, [24 rows])] in id order, from the names table and row tables."""
    names_block = re.search(r"kIconNames\[kIconCount\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    names = re.findall(r'"([^"]+)"', names_block)
    order_block = re.search(r"kIconArt\[kIconCount\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    idents = re.findall(r"(kIcon\w+)", order_block)
    icons = []
    for name, ident in zip(names, idents):
        body = re.search(ident + r"\[kIconSize\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
        rows = re.findall(r'"([#.]+)"', body)
        icons.append((name, rows))
    return icons


def render(icons, out):
    img = Image.new("RGB", (COLS * CELL_W, ROWS * CELL_H), BG)
    d = ImageDraw.Draw(img)
    for i, (name, rows) in enumerate(icons):
        cx, cy = (i % COLS) * CELL_W + 20, (i // COLS) * CELL_H + 10
        d.rectangle([cx, cy, cx + SIZE * SCALE - 1, cy + SIZE * SCALE - 1], fill=TILE)
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch == "#":
                    d.rectangle([cx + x * SCALE, cy + y * SCALE,
                                 cx + x * SCALE + SCALE - 1, cy + y * SCALE + SCALE - 1], fill=INK)
        # 1:1 copy in the corner: real size on the panel.
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch == "#":
                    img.putpixel((cx + SIZE * SCALE + 4 + x, cy + y), INK)
        d.text((cx, cy + SIZE * SCALE + 6), "%d %s" % (i + 1, name), fill=INK)
    img.save(out)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    with open(HEADER) as f:
        icons = parse(f.read())
    render(icons, sys.argv[1])
    print("%d icons -> %s" % (len(icons), sys.argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
