#!/usr/bin/env python3
"""Sharp UI texture pack: every UI texture of Amped 3 (guipackages/*.ruirr,
and the loading screens in loadingscreen.res) enlarged 4x with Real-ESRGAN, named by the runtime's texture key so the
texture replacement (texture_replace_enabled) swaps it in.
  tools/ui_pack.py GAME_DIR WORK_DIR PACK_DIR
WORK_DIR: extracted originals and upscaler output (kept between runs: images
already upscaled are skipped). PACK_DIR: gets <key>.png for each texture.
Needs Pillow and xxhash, and the Real-ESRGAN ncnn build in
private_work/esrgan (realesrgan-ncnn-vulkan.exe).
"""
import os
import shutil
import struct
import subprocess
import sys
import zipfile

import xxhash
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import xpr_extract  # noqa: E402

ESRGAN = os.path.join(os.path.dirname(__file__), '..', 'private_work', 'esrgan', 'realesrgan-ncnn-vulkan.exe')
# Photos (the menus' mountain backgrounds) keep their detail with the general
# model; drawn art, icons and fonts stay cleanest with the art model.
PHOTO_PREFIXES = ('Background',)


def texture_key(xpr):
    """The runtime's replacement key: XXH3 of the guest base level (tiled,
    whole 32x32-block tiles), which is the XPR's (or XTX's) data as stored."""
    fetch, data_offset = xpr_extract.layout(xpr)
    data = xpr[data_offset:]
    d1, d2 = struct.unpack('>2I', xpr[fetch + 4:fetch + 12])
    width, height = (d2 & 0x1FFF) + 1, ((d2 >> 13) & 0x1FFF) + 1
    _, bytes_per_block, block = xpr_extract.FORMATS[d1 & 0x3F]
    bw, bh = (width + block - 1) // block, (height + block - 1) // block
    size = ((bw + 31) & ~31) * ((bh + 31) & ~31) * bytes_per_block
    return xxhash.xxh3_64_hexdigest(data[:size])


def winpath(path):
    return subprocess.check_output(['wslpath', '-w', os.path.abspath(path)], text=True).strip()


def upscale(src_dir, dst_dir, model):
    if not os.listdir(src_dir):
        return
    os.makedirs(dst_dir, exist_ok=True)
    # Run directly (WSL interop): cmd.exe mangles paths with spaces.
    subprocess.run([os.path.abspath(ESRGAN), '-i', winpath(src_dir), '-o', winpath(dst_dir), '-n', model, '-f', 'png'],
                   check=True, cwd=os.path.dirname(os.path.abspath(ESRGAN)), stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)


def main():
    game, work, pack = sys.argv[1:4]
    packages = os.path.join(game, 'guipackages')
    # The loading screens are opaque full-screen pictures: the photo model,
    # kept as JPEG (a tenth of a PNG).
    todo = {k: os.path.join(work, 'in_' + k) for k in ('art', 'photo', 'screen')}
    done = {k: os.path.join(work, 'out_' + k) for k in ('art', 'photo', 'screen')}
    for d in list(todo.values()):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    keys, kinds = {}, {}
    sources = [(os.path.join(packages, p), '.xpr', False) for p in sorted(os.listdir(packages)) if p.endswith('.ruirr')]
    screens = os.path.join(game, 'loadingscreen.res')
    if os.path.exists(screens):
        sources.append((screens, '.xtx', True))
    for path, ext, is_screen in sources:
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                if not info.filename.lower().endswith(ext):
                    continue
                name = os.path.splitext(os.path.basename(info.filename))[0]
                if name in keys:
                    continue
                xpr = z.read(info)
                keys[name] = texture_key(xpr)
                kind = 'screen' if is_screen else 'photo' if name.startswith(PHOTO_PREFIXES) else 'art'
                kinds[name] = kind
                if os.path.exists(os.path.join(done[kind], name + '.png')):
                    continue
                xpr_extract.decode(xpr)[0].save(os.path.join(todo[kind], name + '.png'))
    upscale(todo['art'], done['art'], 'realesrgan-x4plus-anime')
    upscale(todo['photo'], done['photo'], 'realesrgan-x4plus')
    upscale(todo['screen'], done['screen'], 'realesrgan-x4plus')
    os.makedirs(pack, exist_ok=True)
    count = 0
    for name, key in sorted(keys.items()):
        src = os.path.join(done[kinds[name]], name + '.png')
        if not os.path.exists(src):
            continue
        if kinds[name] == 'screen':
            Image.open(src).convert('RGB').save(os.path.join(pack, key + '.jpg'), quality=92)
        else:
            shutil.copyfile(src, os.path.join(pack, key + '.png'))
        count += 1
    with open(os.path.join(work, 'keys.csv'), 'w') as f:
        f.writelines(f'{key},{name}\n' for name, key in sorted(keys.items()))
    print(f'{count} of {len(keys)} textures in {pack}')


if __name__ == '__main__':
    main()
