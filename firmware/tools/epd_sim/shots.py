#!/usr/bin/env python3
"""Turns the simulator's PBM frames into e-paper-looking PNGs (paper tone, 2x, dark bezel).

Usage: python3 shots.py out/ ../../../docs/tutorial/screens/
"""
import glob
import os
import sys

from PIL import Image, ImageDraw

PAPER = (233, 231, 225)
INK = (26, 26, 26)
BEZEL = (43, 43, 43)
SCALE, BORDER, RADIUS = 2, 14, 18


def render(src, dst):
    bw = Image.open(src).convert("1")
    w, h = bw.size
    screen = Image.new("RGB", (w, h), PAPER)
    screen.paste(INK, mask=bw.point(lambda v: 255 if v == 0 else 0, "1"))
    screen = screen.resize((w * SCALE, h * SCALE), Image.NEAREST)
    W, H = screen.width + 2 * BORDER, screen.height + 2 * BORDER
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(out).rounded_rectangle((0, 0, W - 1, H - 1), RADIUS, fill=BEZEL)
    out.paste(screen, (BORDER, BORDER))
    out.save(dst, optimize=True)


def main(src_dir, dst_dir):
    os.makedirs(dst_dir, exist_ok=True)
    for f in sorted(glob.glob(os.path.join(src_dir, "*.pbm"))):
        dst = os.path.join(dst_dir, os.path.basename(f)[:-4] + ".png")
        render(f, dst)
        print(dst)


if __name__ == "__main__":
    main(*sys.argv[1:3])
