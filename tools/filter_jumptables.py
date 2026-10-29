"""Split find_indirect.py candidates into vtable-like and jump-table-like refs.

A reference is vtable-like when its neighbours are mostly known function starts.

usage: filter_jumptables.py image.bin amped3_init.cpp indirect.txt
"""
import bisect, re, struct, sys

BASE, CODE_LO, CODE_HI = 0x82000000, 0x82150000, 0x8284820C
img = open(sys.argv[1], 'rb').read()
known = sorted({int(m, 16) for m in re.findall(r'\{ 0x(8[0-9A-Fa-f]{7}), ', open(sys.argv[2]).read())})
cands = [int(l.split()[0], 16) for l in open(sys.argv[3])]
u32 = lambda off: struct.unpack_from('>I', img, off)[0]

def func_of(a):
    i = bisect.bisect_right(known, a) - 1
    return known[i] if i >= 0 else None

data_offs = [o for o in range(0, len(img) - 3, 4) if not (CODE_LO - BASE <= o < CODE_HI - BASE)]
by_value = {}
for o in data_offs:
    v = u32(o)
    if v in cands_set if (cands_set := set(cands)) else False:
        by_value.setdefault(v, []).append(o)

known_set = set(known)
for a in cands:
    owner, jt = func_of(a), True
    for o in by_value.get(a, []):
        neigh = [u32(o + d) for d in (-16, -12, -8, -4, 4, 8, 12, 16) if 0 <= o + d < len(img) - 3]
        starts = sum(n in known_set for n in neigh)
        labels = sum(CODE_LO <= n < CODE_HI and n not in known_set and func_of(n) == owner for n in neigh)
        if starts >= labels:
            jt = False  # neighbours are function starts: a vtable or callback table
    print(f'{"JT " if jt else "FN "}0x{a:08X} owner=0x{owner:08X}')
