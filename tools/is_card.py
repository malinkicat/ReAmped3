"""Exit 0 if a screenshot shows the yellow-titled help card (e.g. "Bunny Hop"), or with
--hint, the white hint arrow on the left edge during gameplay.

usage: is_card.py shot.png [--hint]
"""
import struct, sys, zlib

data = open(sys.argv[1], 'rb').read()
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
for y in range(h):  # undo PNG row filters
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
if '--hint' in sys.argv:
    # The hint box is a flat neutral grey (~203,202,204); snow is bluer or brighter.
    grey = 0
    for y in range(470, 552, 4):
        for x in range(150, 420, 4):
            r, g, b = rows[y][x * bpp:x * bpp + 3]
            grey += 193 <= r <= 213 and 193 <= g <= 213 and 193 <= b <= 213 and max(r, g, b) - min(r, g, b) <= 5
    sys.exit(0 if grey > 700 else 1)
# Card: black body (430..1000 x 330..560) and yellow title text (590..800 x 165..210).
dark = sum(1 for y in range(330, 560, 6) for x in range(440, 1000, 6) if sum(rows[y][x * bpp:x * bpp + 3]) < 60)
yellow = sum(1 for y in range(165, 210, 2) for x in range(590, 800, 2)
             if rows[y][x * bpp] > 200 and rows[y][x * bpp + 1] > 170 and rows[y][x * bpp + 2] < 90)
sys.exit(0 if dark > 2500 and yellow > 150 else 1)
