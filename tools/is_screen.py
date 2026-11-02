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
# The UI keeps its 16:9 shape: cropped to fill 1280x800 (older builds) or
# drawn in the middle 720 rows with a taller view around it (--tall_squeeze),
# so each check looks for its band over a range of rows.
def band(ys, xs, test):
    """The most matching pixels in any 6 consecutive sampled rows."""
    counts = [sum(1 for x in xs if test(*px(x, y))) for y in ys]
    return max(sum(counts[i:i + 6]) for i in range(max(1, len(counts) - 5)))
if sys.argv[2] == 'map':
    # Orange-yellow objective banner (~ 240,180,40) across x 300..980, and the
    # black bar at the top across x 380..1080.
    orange = band(range(560, 700, 3), range(300, 980, 8), lambda r, g, b: r > 200 and 140 < g < 220 and b < 110)
    black = band(range(60, 190, 4), range(380, 1080, 8), lambda r, g, b: r + g + b < 60)
    sys.exit(0 if orange > 300 and black > 380 else 1)
if sys.argv[2] == 'menu':
    # The rider menu: "Hit the Slopes" highlighted in yellow (x 515..745) on the
    # black arrow (x 560..900, lower down).
    yellow = band(range(120, 260, 2), range(515, 745, 3), lambda r, g, b: r > 220 and g > 170 and b < 80)
    black = band(range(380, 540, 4), range(560, 900, 8), lambda r, g, b: r + g + b < 60)
    sys.exit(0 if yellow > 120 and black > 220 else 1)
sys.exit(2)
