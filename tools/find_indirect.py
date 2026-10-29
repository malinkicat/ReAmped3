"""Find code pointers in data that the recompiler did not register as functions.

usage: find_indirect.py image.bin amped3_init.cpp
"""
import re, struct, sys
from collections import Counter

BASE, CODE_LO, CODE_HI = 0x82000000, 0x82150000, 0x8284820C
img = open(sys.argv[1], 'rb').read()
known = {int(m, 16) for m in re.findall(r'\{ 0x(8[0-9A-Fa-f]{7}), ', open(sys.argv[2]).read())}
word = lambda a: struct.unpack_from('>I', img, a - BASE)[0]

BLR, BCTR, MFLR_R12, PAD = 0x4E800020, 0x4E800420, 0x7D8802A6, 0x00000000
is_b = lambda i: i >> 26 == 18 and i & 3 == 0  # unconditional branch, no link
refs = Counter()
for off in range(0, len(img) - 3, 4):
    if CODE_LO - BASE <= off < CODE_HI - BASE:
        continue  # skip the code section itself
    v = struct.unpack_from('>I', img, off)[0]
    if CODE_LO <= v < CODE_HI and v % 4 == 0 and v not in known:
        refs[v] += 1

def looks_like_entry(a):
    prev, ins = word(a - 4), word(a)
    return prev in (BLR, BCTR, PAD) or is_b(prev) or ins == MFLR_R12 or (ins >> 16) == 0x9421  # stwu r1,-x(r1)

cands = sorted(a for a in refs if looks_like_entry(a))
print(f'known={len(known)} unknown targets={len(refs)} entry-like={len(cands)}', file=sys.stderr)
for a in cands:
    print(f'0x{a:08X} refs={refs[a]} first={word(a):08X}')
