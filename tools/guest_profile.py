"""Main game thread profile with guest function names from build/amped3.map.

usage: guest_profile.py [--threads NAME] [--top N]
"""
import argparse, bisect, collections, csv, re

from toolpaths import ROOT
ap = argparse.ArgumentParser()
ap.add_argument('--threads', default='Main XThread')
ap.add_argument('--top', type=int, default=30)
args = ap.parse_args()

syms = []
for line in open(f'{ROOT}/build/amped3.map', errors='replace'):
    m = re.match(r'\s*0001:[0-9a-f]+\s+(\S+)\s+([0-9a-f]{16})\s', line)
    if m:
        syms.append((int(m.group(2), 16) - 0x140000000, m.group(1)))
syms.sort()
addrs = [a for a, _ in syms]

def name(module, rva):
    if module != 'amped3.exe':
        return f'{module}+{rva:X}'
    i = bisect.bisect_right(addrs, rva) - 1
    return syms[i][1].replace('__imp__', '') if i >= 0 else f'+{rva:X}'

self_c, total_c = collections.Counter(), collections.Counter()
busy = 0
samples = 0
for r in csv.reader(open(f'{ROOT}/build/cpu_profile.csv')):
    if r[0] == '#samples':
        samples = int(r[1])
    if r[0].startswith('#') or r[0] == 'thread' or args.threads.lower() not in r[0].lower():
        continue
    n = name(r[1], int(r[2], 16))
    self_c[n] += int(r[3])
    total_c[n] += int(r[4])
    busy += int(r[3])
print(f'{args.threads}: busy {100 * busy / max(samples, 1):.0f}% of {samples} rounds')
print('self:')
for n, c in self_c.most_common(args.top):
    print(f'  {100 * c / busy:5.1f}%  {n}')
print('total:')
for n, c in total_c.most_common(args.top):
    print(f'  {100 * c / busy:5.1f}%  {n}')
