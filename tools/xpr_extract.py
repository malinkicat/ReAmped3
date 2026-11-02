#!/usr/bin/env python3
"""Amped 3's UI textures (XPR2 files in guipackages/*.ruirr) to PNG, for the
sharp-UI inventory: the base level of each, untiled and decoded.
  tools/xpr_extract.py GAME_DIR OUT_DIR
Writes OUT_DIR/<package>/<name>.png and OUT_DIR/inventory.csv.
"""
import csv
import io
import os
import struct
import sys
import zipfile

from PIL import Image

# Xenos texture formats (fetch constant dword 1, bits 0-5) we decode:
# name, bytes per block, block size (texels).
FORMATS = {
    2: ('8', 1, 1),
    4: ('5_6_5', 2, 1),
    6: ('8_8_8_8', 4, 1),
    10: ('8_8', 2, 1),
    15: ('4_4_4_4', 2, 1),
    18: ('DXT1', 8, 4),
    19: ('DXT2_3', 16, 4),
    20: ('DXT4_5', 16, 4),
}


def tiled_offset(x, y, width, bpb):
    """XGAddress2DTiledOffset: element index of (x, y) in a tiled surface
    `width` elements wide (blocks for compressed formats)."""
    aligned = (width + 31) & ~31
    log_bpp = (bpb >> 2) + ((bpb >> 1) >> (bpb >> 2))
    macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
    offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4)
    return (((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp


def swap(data, endian):
    if endian == 1:  # 8in16
        b = bytearray(data)
        b[0::2], b[1::2] = data[1::2], data[0::2]
        return bytes(b)
    if endian == 2:  # 8in32
        b = bytearray(len(data))
        for i in range(4):
            b[i::4] = data[3 - i::4]
        return bytes(b)
    if endian == 3:  # 16in32
        b = bytearray(len(data))
        b[0::4], b[1::4], b[2::4], b[3::4] = data[2::4], data[3::4], data[0::4], data[1::4]
        return bytes(b)
    return data


def dds(width, height, fourcc, blocks):
    header = struct.pack('<4s7I44x2I4s20x4I4x', b'DDS ', 124, 0x1007, height, width, len(blocks), 0, 0, 32, 4,
                         fourcc, 0x1000, 0, 0, 0)
    return header + blocks


def layout(xpr):
    """(offset of the texture fetch constant, offset of the pixel data):
    XPR2 files (the menus' packages), or bare XTX textures (a D3D texture
    header and its data: the loading screens)."""
    magic, header_size = struct.unpack('>4sI', xpr[:8])
    if magic == b'XPR2':
        return 0x3C, 12 + header_size  # the header size excludes the first 12 bytes
    if struct.unpack('>I', xpr[:4])[0] == 0x00030001:
        return 0x10, 40
    raise ValueError('not an XPR2 or XTX texture')


def decode(xpr):
    fetch, data_offset = layout(xpr)
    d0, d1, d2 = struct.unpack('>3I', xpr[fetch:fetch + 12])
    fmt, endian, tiled = d1 & 0x3F, (d1 >> 6) & 3, bool(d0 & 0x80000000)
    width, height = (d2 & 0x1FFF) + 1, ((d2 >> 13) & 0x1FFF) + 1
    pitch = ((d0 >> 22) & 0x1FF) * 32 or width
    if fmt not in FORMATS:
        raise ValueError(f'format {fmt}')
    name, bpb, block = FORMATS[fmt]
    data = swap(xpr[data_offset:], endian)
    bw, bh = (width + block - 1) // block, (height + block - 1) // block
    pw = max(pitch // block, bw)
    out = bytearray(bw * bh * bpb)
    for y in range(bh):
        for x in range(bw):
            src = (tiled_offset(x, y, pw, bpb) if tiled else y * pw + x) * bpb
            out[(y * bw + x) * bpb:(y * bw + x + 1) * bpb] = data[src:src + bpb]
    if block == 4:
        fourcc = {18: b'DXT1', 19: b'DXT3', 20: b'DXT5'}[fmt]
        image = Image.open(io.BytesIO(dds(bw * 4, bh * 4, fourcc, bytes(out)))).convert('RGBA')
    elif fmt == 6:
        # Amped's 8888 UI textures (grey art) keep alpha where this order puts
        # blue: matched against the runtime's own dump of RadialTubes.
        r, g, b, a = Image.frombytes('RGBA', (bw, bh), bytes(out), 'raw', 'ARGB').split()
        image = Image.merge('RGBA', (r, g, a, b))
    elif fmt == 15:
        px = struct.unpack(f'<{bw * bh}H', bytes(out))
        image = Image.frombytes('RGBA', (bw, bh), bytes(
            v for p in px for v in (((p >> 8) & 15) * 17, ((p >> 4) & 15) * 17, (p & 15) * 17, (p >> 12) * 17)))
    elif fmt == 4:
        image = Image.frombytes('RGB', (bw, bh), bytes(out), 'raw', 'BGR;16').convert('RGBA')
    elif fmt == 10:
        image = Image.frombytes('LA', (bw, bh), bytes(out)).convert('RGBA')
    else:
        image = Image.frombytes('L', (bw, bh), bytes(out)).convert('RGBA')
    return image.crop((0, 0, width, height)), name, width, height


def main():
    game, out = sys.argv[1], sys.argv[2]
    packages = os.path.join(game, 'guipackages')
    rows, seen = [], set()
    for package in sorted(os.listdir(packages)):
        if not package.endswith('.ruirr'):
            continue
        with zipfile.ZipFile(os.path.join(packages, package)) as z:
            for info in z.infolist():
                if not info.filename.lower().endswith('.xpr'):
                    continue
                base = os.path.splitext(os.path.basename(info.filename))[0]
                if base in seen:
                    continue
                seen.add(base)
                try:
                    image, fmt, w, h = decode(z.read(info))
                except Exception as e:  # noqa: BLE001 - listed, not fatal
                    rows.append([package, info.filename, '', '', '', f'error: {e}'])
                    continue
                folder = os.path.join(out, os.path.splitext(package)[0])
                os.makedirs(folder, exist_ok=True)
                image.save(os.path.join(folder, base + '.png'))
                rows.append([package, info.filename, fmt, w, h, ''])
    with open(os.path.join(out, 'inventory.csv'), 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['package', 'file', 'format', 'width', 'height', 'note'])
        writer.writerows(rows)
    print(f'{len(rows)} textures, {sum(1 for r in rows if r[5])} not decoded')


if __name__ == '__main__':
    main()
