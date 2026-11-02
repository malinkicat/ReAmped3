"""Cut a FLUX logo candidate out of its flat black background and write the launcher's logo.bmp.

  logo_cut.py <candidate.png> <out.bmp> [height]

The background is what's connected to the image border through near-black pixels (the logo's own black outline is
enclosed by its white/pink edge, so it stays). The edge gets a 1 px alpha ramp. Output: 32-bit BMP with an alpha
mask (BITMAPV5HEADER), which SDL_LoadBMP reads with alpha, cropped to the logo and scaled to [height] (default 256).
"""
import struct, sys
from collections import deque

from PIL import Image, ImageFilter


def cut(src):
    im = Image.open(src).convert("RGB")
    w, h = im.size
    px = im.load()
    dark = lambda p: max(p) < 40
    bg = bytearray(w * h)
    q = deque()
    for x in range(w):
        q.extend([(x, 0), (x, h - 1)])
    for y in range(h):
        q.extend([(0, y), (w - 1, y)])
    while q:
        x, y = q.popleft()
        i = y * w + x
        if bg[i] or not dark(px[x, y]):
            continue
        bg[i] = 1
        if x > 0: q.append((x - 1, y))
        if x < w - 1: q.append((x + 1, y))
        if y > 0: q.append((x, y - 1))
        if y < h - 1: q.append((x, y + 1))
    alpha = Image.frombytes("L", (w, h), bytes(0 if b else 255 for b in bg)).filter(ImageFilter.GaussianBlur(0.7))
    out = im.convert("RGBA")
    out.putalpha(alpha)
    return out.crop(alpha.point(lambda v: 255 if v > 8 else 0).getbbox())


def write_bmp(im, path):
    w, h = im.size
    rows = []
    for y in range(h - 1, -1, -1):  # bottom-up
        row = bytearray()
        for x in range(w):
            r, g, b, a = im.getpixel((x, y))
            row += bytes((b, g, r, a))
        rows.append(bytes(row))
    data = b"".join(rows)
    header = struct.pack("<IiiHHIIiiII", 124, w, h, 1, 32, 3, len(data), 2835, 2835, 0, 0)
    masks = struct.pack("<IIII", 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    v5 = header + masks + struct.pack("<I", 0x73524742) + bytes(36) + struct.pack("<IIIIII", 0, 0, 0, 4, 0, 0)[:16]
    v5 = v5.ljust(124, b"\0")
    off = 14 + 124
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", off + len(data), 0, 0, off) + v5 + data)


if __name__ == "__main__":
    logo = cut(sys.argv[1])
    height = int(sys.argv[3]) if len(sys.argv) > 3 else 256
    logo = logo.resize((round(logo.width * height / logo.height), height), Image.LANCZOS)
    write_bmp(logo, sys.argv[2])
    logo.save(sys.argv[2].rsplit(".", 1)[0] + "_preview.png")
    print(sys.argv[2], logo.size)
