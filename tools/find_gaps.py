"""Find code the recompiler never reached: function-like blocks hidden in the gaps
between a function's last reachable instruction and the next known function.

usage: find_gaps.py image.bin amped3_init.cpp
"""
import re, struct, sys

BASE, CODE_LO, CODE_HI = 0x82000000, 0x82150000, 0x8284820C
img = open(sys.argv[1], 'rb').read()
known = sorted({int(m, 16) for m in re.findall(r'\{ 0x(8[0-9A-Fa-f]{7}), ', open(sys.argv[2]).read())})
word = lambda a: struct.unpack_from('>I', img, a - BASE)[0]
MFLR_R12, BLR, BCTR = 0x7D8802A6, 0x4E800020, 0x4E800420


def successors(pc, ins):
    op = ins >> 26
    if ins in (BLR, BCTR):
        return []
    if op == 18:  # b / bl
        target = (pc + ((ins & 0x3FFFFFC) - (0x4000000 if ins & 0x2000000 else 0))) & 0xFFFFFFFF
        if ins & 2:
            target = ins & 0x3FFFFFC
        if ins & 1:
            return [pc + 4]  # bl returns
        return [target]
    if op == 16:  # bc
        bd = ins & 0xFFFC
        target = (pc + (bd - 0x10000 if bd & 0x8000 else bd)) & 0xFFFFFFFF
        return [pc + 4, target] if not ins & 1 else [pc + 4]
    if op == 19 and (ins >> 1) & 0x3FF in (16, 528):  # bclr / bcctr (conditional forms)
        bo = (ins >> 21) & 0x1F
        return [] if bo & 0x14 == 0x14 else [pc + 4]
    return [pc + 4]


def straight_thunk(a, limit):
    for k in range(12):
        pc = a + 4 * k
        if pc >= limit:
            return False
        ins = word(pc)
        if ins in (BLR, BCTR):
            return k >= 1
        if ins >> 26 in (16, 18) or ins == 0:
            return False
    return False


def after_terminator(prev):
    return prev in (BLR, BCTR, 0) or (prev >> 26 == 18 and not prev & 1)  # or b without link


found = []
for i, start in enumerate(known):
    end = known[i + 1] if i + 1 < len(known) else CODE_HI
    if not (CODE_LO <= start < CODE_HI):
        continue
    seen, todo = set(), [start]
    while todo:
        pc = todo.pop()
        if pc in seen or not (start <= pc < end):
            continue
        seen.add(pc)
        todo.extend(successors(pc, word(pc)))
    pc = start
    while pc < end:
        if pc not in seen and word(pc) != 0 and after_terminator(word(pc - 4)):
            if word(pc) == MFLR_R12 or straight_thunk(pc, end):
                found.append((pc, start))
        pc += 4
print(f'gap candidates: {len(found)}', file=sys.stderr)
for a, owner in found:
    print(f'0x{a:08X} owner=0x{owner:08X} first={word(a):08X}')
