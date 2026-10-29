"""Exit 0 if a Deck screenshot (1280x800) shows the given screen.

usage: is_screen.py shot.png map
  menu: the rider menu with "Hit the Slopes" highlighted
  map: the resort map (orange objective banner across the bottom, black bar at the top)
"""
import struct, sys, zlib

def load(path):
    data = open(path, 'rb').read()
    w, h = struct.unpack('>II', data[16:24])
    bpp = 4 if data[25] == 6 else 3
    idat, i = b'', 8
    while i < len(data):
        n = struct.unpack('>I', data[i:i + 4])[0]
        if data[i + 4:i + 8] == b'IDAT':
            idat += data[i + 8:i + 8 + n]
        i += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line); prev = line
    return w, h, bpp, rows

w, h, bpp, rows = load(sys.argv[1])
px = lambda x, y: tuple(rows[y][x * bpp:x * bpp + 3])
if sys.argv[2] == 'map':
    # Orange-yellow banner (~ 240,180,40) along y 640..660, x 300..980.
    orange = sum(1 for y in range(642, 662, 4) for x in range(300, 980, 8)
                 if (lambda r, g, b: r > 200 and 140 < g < 220 and b < 110)(*px(x, y)))
    # Black top bar at y 85..110, x 380..1080.
    black = sum(1 for y in range(85, 112, 4) for x in range(380, 1080, 8) if sum(px(x, y)) < 60)
    sys.exit(0 if orange > 250 and black > 300 else 1)
if sys.argv[2] == 'menu':
    # The rider menu: "Hit the Slopes" highlighted in yellow (x 515..745, y 160..195)
    # on the black arrow (black around x 560..900, y 420..470).
    yellow = sum(1 for y in range(160, 196, 2) for x in range(515, 745, 3)
                 if (lambda r, g, b: r > 220 and g > 170 and b < 80)(*px(x, y)))
    black = sum(1 for y in range(420, 470, 4) for x in range(560, 900, 8) if sum(px(x, y)) < 60)
    sys.exit(0 if yellow > 120 and black > 300 else 1)
sys.exit(2)
