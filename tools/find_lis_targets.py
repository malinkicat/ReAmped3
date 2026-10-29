"""Find code addresses built with lis+addi/ori that are not registered functions.

usage: find_lis_targets.py image.bin amped3_init.cpp [check_addr ...]
"""
import bisect, re, struct, sys

BASE, CODE_LO, CODE_HI = 0x82000000, 0x82150000, 0x8284820C
img = open(sys.argv[1], 'rb').read()
known = sorted({int(m, 16) for m in re.findall(r'\{ 0x(8[0-9A-Fa-f]{7}), ', open(sys.argv[2]).read())})
known_set = set(known)
word = lambda a: struct.unpack_from('>I', img, a - BASE)[0]

found = {}
for pc in range(CODE_LO, CODE_HI - 4, 4):
    ins = word(pc)
    if ins >> 26 != 15 or (ins >> 16) & 0x1F != 0:  # lis rD, imm  (addis rD, 0, imm)
        continue
    rd, hi = (ins >> 21) & 0x1F, ins & 0xFFFF
    for k in range(1, 9):
        nxt = word(pc + 4 * k)
        op, rt, ra, lo = nxt >> 26, (nxt >> 21) & 0x1F, (nxt >> 16) & 0x1F, nxt & 0xFFFF
        if op == 14 and ra == rd:      # addi rT, rD, simm
            addr = ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
        elif op == 24 and rt == rd:    # ori rA, rS, uimm (rS field is bits 21-25)
            addr = (hi << 16) | lo
        else:
            continue
        if CODE_LO <= addr < CODE_HI and addr % 4 == 0 and addr not in known_set:
            found.setdefault(addr, []).append(pc)
        break

def containing(a):
    i = bisect.bisect_right(known, a) - 1
    return known[i] if i >= 0 else None

is_b = lambda i: i >> 26 == 18 and i & 3 == 0  # unconditional branch, no link
prev_ok = lambda a: (word(a - 4) in (0x4E800020, 0x4E800420, 0) or is_b(word(a - 4))
                     or word(a) == 0x7D8802A6 or word(a) >> 16 == 0x9421)
for a in sys.argv[3:]:
    a = int(a, 16)
    print(f'check 0x{a:08X}: built_by={["%08X" % p for p in found.get(a, [])]} inside=0x{containing(a):08X} entry_like={prev_ok(a)} prev={word(a-4):08X} ins={word(a):08X}', file=sys.stderr)
cands = [a for a in sorted(found) if prev_ok(a)]
print(f'unknown lis targets={len(found)} entry-like={len(cands)}', file=sys.stderr)
for a in cands:
    print(f'0x{a:08X} inside=0x{containing(a):08X} built_by={",".join("%08X" % p for p in found[a][:3])}')
