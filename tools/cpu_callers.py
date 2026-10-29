"""Callers of a function from the #chain lines (leaf + 4 callers) in build/cpu_profile.csv.

usage: cpu_callers.py PATTERN [--threads NAME] [--top N]
"""
import argparse, collections, csv, struct, subprocess

from toolpaths import ROOT, SYMBOLIZER
ap = argparse.ArgumentParser()
ap.add_argument('pattern')
ap.add_argument('--threads', default='GPU Commands')
ap.add_argument('--top', type=int, default=15)
args = ap.parse_args()

chains = []
for r in csv.reader(open(f'{ROOT}/build/cpu_profile.csv')):
    if r[0] == '#chain' and args.threads.lower() in r[1].lower() and 'waiting' not in r[1]:
        chains.append((r[2].split(';'), int(r[3])))
frames = {f for c, _ in chains for f in c}

def image_base(path):
    data = open(path, 'rb').read(4096)
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    return struct.unpack_from('<Q', data, pe + 24 + 24)[0]

names = {}
by_mod = collections.defaultdict(list)
for f in frames:
    m, _, rva = f.rpartition('+')
    by_mod[m].append((f, int(rva, 16)))
for m, items in by_mod.items():
    path = f'{ROOT}/build/{m}'
    try:
        base = image_base(path)
    except OSError:
        names.update({f: f for f, _ in items})
        continue
    win = subprocess.check_output(['wslpath', '-w', path], text=True).strip()
    out = subprocess.run([SYMBOLIZER, f'--obj={win}', '--functions=linkage', '--demangle', '--no-inlines'],
                         input='\n'.join(hex(base + r) for _, r in items), capture_output=True,
                         text=True).stdout.split('\n\n')
    for (f, _), block in zip(items, out):
        ls = block.strip().splitlines()
        fn = ls[0].split('(')[0] if ls and ls[0] != '??' else f
        line = ls[1].split('\\')[-1] if len(ls) > 1 else ''
        names[f] = f'{fn} @{line}'
total = sum(n for _, n in chains)
callers = collections.Counter()
hits = 0
for c, n in chains:
    syms = [names[f] for f in c]
    for i, s in enumerate(syms):
        if args.pattern in s:
            hits += n
            callers[' <- '.join(x.split(' @')[0].split('::')[-1] + '@' + x.split('@')[-1].split(':')[1] if ':' in x.split('@')[-1] else x for x in syms[i + 1:i + 4])] += n
            break
print(f'{100 * hits / total:.1f}% of chained samples contain {args.pattern}')
for k, n in callers.most_common(args.top):
    print(f'  {100 * n / total:5.2f}%  {k}')
