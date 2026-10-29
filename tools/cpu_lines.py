"""Line-level self samples for functions matching a pattern, from build/cpu_profile.csv.

usage: cpu_lines.py --threads "GPU Commands" PATTERN [--top N]
"""
import argparse, collections, csv, os, struct, subprocess

from toolpaths import ROOT, SYMBOLIZER
ap = argparse.ArgumentParser()
ap.add_argument('pattern')
ap.add_argument('--threads', default='GPU Commands')
ap.add_argument('--top', type=int, default=30)
args = ap.parse_args()

rows = []
for r in csv.reader(open(f'{ROOT}/build/cpu_profile.csv')):
    if r[0].startswith('#') or r[0] == 'thread' or args.threads.lower() not in r[0].lower():
        continue
    if int(r[3]):
        rows.append((r[1], int(r[2], 16), int(r[3])))
busy = sum(s for _, _, s in rows)

def image_base(path):
    data = open(path, 'rb').read(4096)
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    return struct.unpack_from('<Q', data, pe + 24 + 24)[0]

lines = collections.Counter()
for module in {m for m, _, _ in rows}:
    path = f'{ROOT}/build/{module}'
    if not os.path.exists(path):
        continue
    mr = [(rva, s) for m, rva, s in rows if m == module]
    base = image_base(path)
    win = subprocess.check_output(['wslpath', '-w', path], text=True).strip()
    out = subprocess.run([SYMBOLIZER, f'--obj={win}', '--functions=linkage', '--demangle'],
                         input='\n'.join(hex(base + rva) for rva, _ in mr), capture_output=True,
                         text=True).stdout.split('\n\n')
    for (rva, s), block in zip(mr, out):
        ls = block.strip().splitlines()
        # Outermost frame (last pair) is the real function; innermost line is where.
        if len(ls) < 2 or args.pattern not in ls[-2]:
            continue
        where = ls[1].split('\\')[-1]
        inl = ls[0][:60] if len(ls) > 2 else ''
        lines[f'{where:<40} {inl}'] += s
tot = sum(lines.values())
print(f'{100*tot/busy:.1f}% of busy in {args.pattern}')
for k, c in lines.most_common(args.top):
    print(f'  {100*c/busy:5.2f}%  {k}')
