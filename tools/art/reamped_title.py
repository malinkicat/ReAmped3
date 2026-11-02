#!/usr/bin/env python3
"""The title logo as REAMPED3: the game's AMPED3 logo (Shell.ruirr
Textures/AmpedBoxLogo.xpr, decoded by xpr_texture.py) with "RE" as two more
letters of the word: as tall as M P E D, on their top and bottom lines,
leaning like them, one letter gap left of the A, on the same black plate.
  tools/art/reamped_title.py LOGO.png OUT.png [--view VIEW.png]
Prototype of what the game does at start from the player's own texture
(project/src/title_logo.cpp Compose; no game art ships). The letters are
vector shapes drawn 8x oversampled.
"""
import math
import sys

from PIL import Image, ImageChops, ImageDraw

PINK = (255, 106, 181)
MAGENTA = (255, 28, 148)
RIM = (190, 20, 110)
SS = 8

# Letters in an upright box W x H (the original E, in texture pixels).
W, H = 34.0, 58.0
STEM = 14.0   # vertical stroke
ARM = 13.0    # horizontal stroke


def letter_r(x):
    """(outer polygon, counters) for R at x."""
    outer = [(0, 0), (36, 0), (36, 31), (28, 35), (36, 58), (22, 58), (17, 38), (14, 38), (14, 58), (0, 58)]
    counters = [[(14, 13), (22, 13), (22, 25), (14, 25)]]
    return [(x + a, b) for a, b in outer], [[(x + a, b) for a, b in c] for c in counters]


def letter_e(x):
    outer = [(0, 0), (34, 0), (34, 13), (14, 13), (14, 23), (30, 23), (30, 36), (14, 36), (14, 45),
             (34, 45), (34, 58), (0, 58)]
    return [(x + a, b) for a, b in outer], []


def build(logo, scale=0.8, shift=(100, 18), lean=0.32, slope=0.0425, height=55.0, gap=6.3, e_right=82.5,
          border=6.0, shadow=(2, 4)):
    out_w, out_h = logo.size
    f = height / H
    ang = math.atan(slope)
    word = W + gap + 34
    xb = e_right - f * word
    yb = 135 + 0.042 * (168 - xb)          # MPED's bottom line, extended left
    def place(p):
        u, v = p
        x = f * (u + lean * (H - v)); y = f * (v - H)
        rx = x * math.cos(ang) + y * math.sin(ang); ry = -x * math.sin(ang) + y * math.cos(ang)
        X, Y = xb + rx, yb + ry                 # original's pixels
        return ((shift[0] + scale * X) * SS, (shift[1] + scale * Y) * SS)
    r, e = letter_r(0), letter_e(W + gap)
    big = (out_w * SS, out_h * SS)
    bw = 2 * border * scale * SS
    plate = Image.new('L', big, 0); pd = ImageDraw.Draw(plate)
    rim = Image.new('L', big, 0); rd = ImageDraw.Draw(rim)
    for poly in (r[0], e[0]):
        pts = [place(p) for p in poly]
        pd.polygon(pts, fill=255); pd.line(pts + [pts[0]], fill=255, width=int(bw), joint='curve')
        rd.polygon(pts, fill=255); rd.line(pts + [pts[0]], fill=255, width=int(bw + 1.6 * scale * SS), joint='curve')
    off = lambda m: ImageChops.lighter(m, ImageChops.offset(m, shadow[0] * SS, shadow[1] * SS))
    plate, rim = off(plate), off(rim)
    fill = Image.new('L', big, 0); fd = ImageDraw.Draw(fill)
    for poly in (r[0], e[0]): fd.polygon([place(p) for p in poly], fill=255)
    for poly in r[1]: fd.polygon([place(p) for p in poly], fill=0)
    down = lambda m: m.resize((out_w, out_h), Image.LANCZOS)
    small = logo.resize((int(out_w * scale), int(out_h * scale)), Image.LANCZOS)
    placed = Image.new('RGBA', logo.size, (0, 0, 0, 0)); placed.alpha_composite(small, shift)
    out = Image.new('RGBA', logo.size, (0, 0, 0, 0))
    out.paste(RIM + (255,), (0, 0), down(rim))          # RE's rim behind the original
    out.alpha_composite(placed)
    layer = Image.new('RGBA', logo.size, (0, 0, 0, 0))
    layer.paste((0, 0, 0, 255), (0, 0), down(plate))
    layer.paste(PINK + (255,), (0, 0), down(fill))
    # Left of the gap between E and A: RE over the original; right: the original (the A) in front.
    keep = Image.new('L', logo.size, 0)
    x_at = lambda y: shift[0] + scale * (e_right + 3.2) + (shift[1] + scale * 135 - y) * lean
    ImageDraw.Draw(keep).polygon([(x_at(0), 0), (out_w, 0), (out_w, out_h), (x_at(out_h), out_h)], fill=255)
    layer = Image.composite(Image.new('RGBA', logo.size, (0, 0, 0, 0)), layer, keep)
    out.alpha_composite(layer)
    return out

if __name__ == '__main__':
    logo = Image.open(sys.argv[1]).convert('RGBA')
    out = build(logo)
    out.save(sys.argv[2])
    if '--view' in sys.argv:
        view = sys.argv[sys.argv.index('--view') + 1]
        big = out.resize((out.width * 2, out.height * 2), Image.LANCZOS)
        bg = Image.new('RGBA', big.size, (40, 120, 140, 255))
        bg.alpha_composite(big)
        bg.save(view)
