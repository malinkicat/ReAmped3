#!/usr/bin/env python3
"""Xbox 360 XPR2 texture (one TX2D, DXT1/3/5, tiled) <-> PNG, for the game's UI
textures in GuiPackages/*.ruirr.
  tools/art/xpr_texture.py decode IN.xpr OUT.png
"""
import struct
import sys

FORMATS = {0x12: ('DXT1', 8), 0x13: ('DXT3', 16), 0x14: ('DXT5', 16)}


def tiled_block(x, y, width, log_bpb):
    """Xenos 2D tiling: block (x, y) of a surface `width` blocks wide -> block index."""
    aligned = (width + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpb + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpb
    offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpb)) + ((y & 1) << 4)
    return (((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) +
            ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpb


def parse(data):
    assert data[:4] == b'XPR2'
    header_size, data_size, count = struct.unpack('>III', data[4:16])
    kind, offset, size, name = struct.unpack('>4sIII', data[16:32])
    assert kind == b'TX2D' and count == 1
    tex = 12 + offset
    fetch = struct.unpack(">6I", data[tex + 16:tex + 40])
    fmt = fetch[1] & 0x3F
    endian = (fetch[1] >> 6) & 3
    width = (fetch[2] & 0x1FFF) + 1
    height = ((fetch[2] >> 13) & 0x1FFF) + 1
    tiled = bool(fetch[0] & 0x80000000)
    pixels_at = 12 + header_size
    return dict(fmt=fmt, endian=endian, width=width, height=height, tiled=tiled,
                pixels_at=pixels_at, data_size=data_size)


def linear_blocks(data, info):
    name, bpb = FORMATS[info['fmt']]
    bw, bh = (info['width'] + 3) // 4, (info['height'] + 3) // 4
    src = data[info['pixels_at']:info['pixels_at'] + info['data_size']]
    log = bpb.bit_length() - 1
    aligned_w = (bw + 31) & ~31
    out = bytearray(bw * bh * bpb)
    for y in range(bh):
        for x in range(bw):
            i = tiled_block(x, y, aligned_w, log) if info['tiled'] else y * bw + x
            block = bytearray(src[i * bpb:(i + 1) * bpb])
            if info['endian'] == 1:  # 8-in-16
                block[0::2], block[1::2] = block[1::2], block[0::2]
            out[(y * bw + x) * bpb:(y * bw + x + 1) * bpb] = block
    return name, bytes(out)


def dds(name, width, height, blocks):
    head = struct.pack('<4sIIIIIII44sIIII4sIIIIIIIIII', b'DDS ', 124, 0x81007, height, width, len(blocks), 0, 1,
                       b'\0' * 44, 32, 4, 0, 0, name.encode(), 0, 0, 0, 0, 0x1000, 0, 0, 0, 0, 0)
    head = struct.pack('<4s', b'DDS ') + struct.pack('<7I', 124, 0x81007, height, width, len(blocks), 0, 1) + \
        b'\0' * 44 + struct.pack('<2I4s5I', 32, 4, name.encode(), 0, 0, 0, 0, 0) + struct.pack('<5I', 0x1000, 0, 0, 0, 0)
    return head + blocks


if __name__ == '__main__':
    import io
    from PIL import Image
    cmd, src, dst = sys.argv[1:4]
    data = open(src, 'rb').read()
    info = parse(data)
    print(info)
    name, blocks = linear_blocks(data, info)
    Image.open(io.BytesIO(dds(name, info['width'], info['height'], blocks))).save(dst)


def encode_dxt3(img):
    """RGBA image -> DXT3 blocks (explicit 4-bit alpha; colour endpoints from
    each block's extremes along its main axis, 4-colour mode)."""
    import numpy as np
    a = np.asarray(img.convert('RGBA'), dtype=np.float32)
    h, w = a.shape[:2]
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            b = a[by:by + 4, bx:bx + 4].reshape(16, 4)
            alpha = np.clip(np.round(b[:, 3] / 17), 0, 15).astype(np.uint8)
            ab = bytearray(8)
            for i in range(16):
                ab[i // 2] |= int(alpha[i]) << (4 * (i & 1))
            rgb = b[:, :3]
            weight = b[:, 3:4] / 255 + 1e-3
            mean = (rgb * weight).sum(0) / weight.sum()
            cov = ((rgb - mean) * weight).T @ (rgb - mean)
            axis = np.linalg.eigh(cov)[1][:, -1]
            proj = (rgb - mean) @ axis
            lo, hi = mean + axis * proj.min(), mean + axis * proj.max()

            def to565(c):
                c = np.clip(np.round(c), 0, 255)
                return (int(c[0]) >> 3) << 11 | (int(c[1]) >> 2) << 5 | int(c[2]) >> 3

            def from565(v):
                return np.array([(v >> 11 & 31) * 255 / 31, (v >> 5 & 63) * 255 / 63, (v & 31) * 255 / 31])
            c0, c1 = to565(hi), to565(lo)
            if c0 < c1:
                c0, c1 = c1, c0
            p0, p1 = from565(c0), from565(c1)
            pal = np.array([p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3])
            idx = np.argmin(((rgb[:, None, :] - pal[None]) ** 2).sum(-1), axis=1)
            if c0 == c1:
                idx[:] = 0
            bits = 0
            for i in range(16):
                bits |= int(idx[i]) << (2 * i)
            out += ab + struct.pack('<HHI', c0, c1, bits)
    return bytes(out)


def encode_xpr(original, img):
    """`original` XPR2 with its pixels replaced by `img` (same size, DXT3)."""
    info = parse(original)
    assert FORMATS[info['fmt']][0] == 'DXT3' and img.size == (info['width'], info['height'])
    blocks = encode_dxt3(img)
    bpb = 16
    bw, bh = info['width'] // 4, info['height'] // 4
    aligned_w = (bw + 31) & ~31
    data = bytearray(original[info['pixels_at']:info['pixels_at'] + info['data_size']])
    for y in range(bh):
        for x in range(bw):
            block = bytearray(blocks[(y * bw + x) * bpb:(y * bw + x + 1) * bpb])
            if info['endian'] == 1:
                block[0::2], block[1::2] = block[1::2], block[0::2]
            i = tiled_block(x, y, aligned_w, 4) if info['tiled'] else y * bw + x
            data[i * bpb:(i + 1) * bpb] = block
    return original[:info['pixels_at']] + bytes(data) + original[info['pixels_at'] + info['data_size']:]
