"""Summarize build/cpu_profile.csv (written by the "profile N" scripted command):
top functions per thread by self and total samples, symbolized with
llvm-symbolizer and the modules' PDBs.

usage: cpu_profile.py [--threads NAME,...] [--top N]
"""
import argparse, collections, csv, os, struct, subprocess

from toolpaths import ROOT, SYMBOLIZER

ap = argparse.ArgumentParser()
ap.add_argument('--threads', default='')
ap.add_argument('--top', type=int, default=25)
args = ap.parse_args()

rows, samples, idle = [], 0, {}
for r in csv.reader(open(f'{ROOT}/build/cpu_profile.csv')):
    if r[0] == 'thread':
        continue
    if r[0] == '#chain':
        continue
    if r[0] == '#idle':
        idle[r[1]] = int(r[2])
        continue
    if r[0] == '#samples':
        samples = int(r[1])
        continue
    rows.append((r[0], r[1], int(r[2], 16), int(r[3]), int(r[4])))


def image_base(path):
    data = open(path, 'rb').read(4096)
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    return struct.unpack_from('<Q', data, pe + 24 + 24)[0]  # PE32+ OptionalHeader.ImageBase


def map_symbols():
    # build/amped3.map (the exe has no PDB): public symbols by RVA, so guest
    # functions show as sub_XXXXXXXX (the recompiled bodies are __imp__sub_*).
    path = f'{ROOT}/build/amped3.map'
    if not os.path.exists(path):
        return []
    syms, base = [], 0x140000000
    for line in open(path, errors='ignore'):
        parts = line.split()
        if 'Preferred load address' in line:
            base = int(parts[-1], 16)
        elif len(parts) >= 3 and parts[0].startswith('0001:'):
            try:
                syms.append((int(parts[2], 16) - base, parts[1]))
            except ValueError:
                pass
    return sorted(syms)


import bisect
EXE_SYMS = map_symbols()
EXE_ADDRS = [a for a, _ in EXE_SYMS]


def exe_symbol(rva):
    i = bisect.bisect_right(EXE_ADDRS, rva) - 1
    if i < 0:
        return None
    return EXE_SYMS[i][1].replace('__imp__', '')


names = {}
for module in {m for _, m, _, _, _ in rows}:
    path = f'{ROOT}/build/{module}'
    rvas = sorted({rva for _, m, rva, _, _ in rows if m == module})
    if module == '?' or not os.path.exists(path):
        names.update({(module, rva): f'{module}+{rva:X}' for rva in rvas})
        continue
    base = image_base(path)
    win = subprocess.check_output(['wslpath', '-w', path], text=True).strip()
    out = subprocess.run([SYMBOLIZER, f'--obj={win}', '--functions=linkage', '--demangle',
                          '--no-inlines'],
                         input='\n'.join(hex(base + rva) for rva in rvas), capture_output=True,
                         text=True).stdout.split('\n\n')
    for rva, block in zip(rvas, out):
        lines = block.strip().splitlines()
        func = lines[0] if lines and lines[0] != '??' else None
        if func is None and module.startswith('amped3') and EXE_SYMS:
            func = exe_symbol(rva)
        func = func or f'{module}+{rva:X}'
        names[(module, rva)] = f'{func} [{module}]' if not module.startswith('amped3') else func

by_thread = collections.defaultdict(lambda: [collections.Counter(), collections.Counter()])
for thread, module, rva, self_, total in rows:
    n = names[(module, rva)]
    by_thread[thread][0][n] += self_
    by_thread[thread][1][n] += total

wanted = [t for t in args.threads.split(',') if t]
print(f'{samples} sampling rounds')
for thread, (self_c, total_c) in sorted(by_thread.items(), key=lambda kv: -sum(kv[1][0].values())):
    busy = sum(self_c.values())
    if wanted and not any(w.lower() in thread.lower() for w in wanted):
        continue
    if not wanted and busy < samples * 0.05:
        continue
    print(f'\n=== {thread}: busy {100 * busy / samples:.0f}% of the time ({busy} samples)')
    print('  self:')
    for n, c in self_c.most_common(args.top):
        print(f'    {100 * c / busy:5.1f}%  {n[:150]}')
    print('  total:')
    for n, c in total_c.most_common(args.top):
        print(f'    {100 * c / busy:5.1f}%  {n[:150]}')
