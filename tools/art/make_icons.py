#!/usr/bin/env python3
"""App icon candidates (our own art: the ReAmped 3 menu's star, an "R3"
monogram in the title logo's letters, a crop of the launcher logo), rendered at
icon sizes on light and dark tiles for choosing.
  tools/art/make_icons.py OUT_DIR
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(__file__))
import reamped_title as rt  # noqa: E402

PINK = (255, 46, 154, 255)       # game_menu.cpp kPink
PINK_DARK = (176, 16, 95, 255)   # kPinkDark
YELLOW = (255, 225, 77, 255)     # kYellow
BLACK = (11, 11, 18, 255)        # kBlack
N = 1024


def star_points(cx, cy, r, inner=0.45, rot=-90.0):
    pts = []
    for i in range(10):
        a = math.radians(rot + i * 36)
        rr = r if i % 2 == 0 else r * inner
        pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr))
    return pts


def star_badge():
    """The menu header's star: yellow, pink outline, on a black outline."""
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pts = star_points(N / 2, N / 2 + 30, 470, rot=-80)
    d.polygon(pts, fill=BLACK)
    d.line(pts + [pts[0]], fill=BLACK, width=90, joint='curve')
    inner = star_points(N / 2, N / 2 + 30, 400, rot=-80)
    d.polygon(inner, fill=PINK)
    core = star_points(N / 2, N / 2 + 30, 300, rot=-80)
    d.polygon(core, fill=YELLOW)
    return im


def monogram():
    """R3 in the title logo's letters (reamped_title.py's R, a matching 3),
    leaned, on the black plate with the magenta band."""
    W, H = rt.W, rt.H
    r_outer, r_counters = rt.letter_r(0)
    x = W + 8
    three = [(x, 0), (x + 34, 0), (x + 34, 58), (x, 58), (x, 45), (x + 20, 45), (x + 20, 36),
             (x + 4, 36), (x + 4, 23), (x + 20, 23), (x + 20, 13), (x, 13)]
    shapes = [r_outer, three]
    lean = 0.3
    word_w = x + 34
    scale = 9.0
    ox = (N - (word_w + lean * H) * scale) / 2 + lean * H * scale * 0.2
    oy = (N - (H + 30) * scale) / 2

    def place(p):
        px, py = p
        px += lean * (H - py)
        return (ox + px * scale, oy + py * scale)

    band = [(0, H + 8), (word_w, H + 8), (word_w, H + 24), (0, H + 24)]
    # One plate behind both letters (no gap between them).
    backing = [(0, 6), (word_w, 6), (word_w, H), (0, H)]
    ox += N / 2
    oy += N / 2
    im = Image.new('RGBA', (2 * N, 2 * N), (0, 0, 0, 0))
    plate = Image.new('L', (2 * N, 2 * N), 0)
    pd = ImageDraw.Draw(plate)
    # One plate: the convex outline of the letters and the band, stroked
    # thick, so its edge runs cleanly all the way round (no steps).
    pts = [place(p) for poly in shapes + [band, backing] for p in poly]
    hull = convex_hull(pts)
    pd.polygon(hull, fill=255)
    pd.line(hull + [hull[0]], fill=255, width=120, joint="curve")
    for x, y in hull:  # round every corner, including where the line closes
        pd.ellipse([x - 60, y - 60, x + 60, y + 60], fill=255)
    im.paste(BLACK, (0, 0), plate)
    d = ImageDraw.Draw(im)
    d.polygon([place(p) for p in band], fill=PINK_DARK)
    for poly in shapes:
        d.polygon([place(p) for p in poly], fill=PINK)
    for c in r_counters:
        d.polygon([place(p) for p in c], fill=BLACK)
    return fit(im)


def convex_hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def fit(im, margin=0.06):
    """Crops to what's drawn and centres it in the square with a margin, so
    the outline goes all the way round inside the icon."""
    box = im.getbbox()
    art = im.crop(box)
    room = int(N * (1 - 2 * margin))
    k = room / max(art.size)
    art = art.resize((max(1, int(art.width * k)), max(1, int(art.height * k))), Image.LANCZOS)
    out = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    out.alpha_composite(art, ((N - art.width) // 2, (N - art.height) // 2))
    return out


def launcher_crop(path):
    """A square crop of the launcher logo (FLUX-made, ours) around "Re"."""
    logo = Image.open(path).convert('RGBA')
    w, h = logo.size
    side = int(h * 0.82)
    box = (int(w * 0.04), int(h * 0.12), int(w * 0.04) + side, int(h * 0.12) + side)
    crop = logo.crop(box).resize((N, N), Image.LANCZOS)
    return crop


def sheet(icons, out):
    sizes = [256, 64, 48, 32, 16]
    pad = 24
    tile_w = sum(sizes) + pad * (len(sizes) + 1)
    rows = []
    for name, im in icons:
        for bg in [(244, 246, 250, 255), (24, 28, 36, 255)]:
            row = Image.new('RGBA', (tile_w, 256 + 2 * pad), bg)
            x = pad
            for s in sizes:
                icon = im.resize((s, s), Image.LANCZOS)
                row.alpha_composite(icon, (x, pad + (256 - s) // 2))
                x += s + pad
            rows.append(row)
    full = Image.new('RGBA', (tile_w, sum(r.height for r in rows)), (0, 0, 0, 255))
    y = 0
    for r in rows:
        full.alpha_composite(r, (0, y))
        y += r.height
    full.save(out)


if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    root = os.path.join(os.path.dirname(__file__), '..', '..')
    icons = [('star', star_badge()), ('r3', monogram()),
             ('launcher', launcher_crop(os.path.join(root, 'project', 'assets', 'logo', 'logo.bmp')))]
    for name, im in icons:
        im.save(os.path.join(out, f'icon_{name}_1024.png'))
        sheet([(name, im)], os.path.join(out, f'icon_{name}_sheet.png'))
