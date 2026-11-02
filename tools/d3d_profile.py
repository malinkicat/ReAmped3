#!/usr/bin/env python3
# Profiles guest functions from the recompiled code (native renderer
# groundwork): PM4 packet headers built from immediates (type 3 opcodes and
# type 0 register writes), loads/stores relative to the first argument (r3 at
# entry and its copies: usually the D3D device), and the functions called.
#   tools/d3d_profile.py sub_XXXXXXXX ...
import collections
import os
import re
import sys

GEN = 'project/generated/default'
OPS = {0x22: 'DRAW_INDX', 0x36: 'DRAW_INDX_2', 0x5A: 'EVENT_WRITE_EXT', 0x50: 'SET_BIN_MASK',
       0x51: 'SET_BIN_SELECT', 0x3F: 'INDIRECT_BUFFER', 0x2D: 'SET_CONSTANT', 0x27: 'IM_LOAD',
       0x2B: 'IM_LOAD_IMMEDIATE', 0x46: 'EVENT_WRITE', 0x3C: 'WAIT_REG_MEM', 0x54: 'INTERRUPT',
       0x2F: 'LOAD_ALU_CONSTANT', 0x2E: 'LOAD_CONSTANT_CONTEXT', 0x3D: 'MEM_WRITE',
       0x58: 'EVENT_WRITE_SHD', 0x5B: 'EVENT_WRITE_ZPD', 0x44: 'COND_EXEC', 0x10: 'NOP',
       0x3B: 'INVALIDATE_STATE', 0x21: 'REG_RMW', 0x55: 'SET_CONSTANT2',
       0x56: 'SET_SHADER_CONSTANTS', 0x3E: 'REG_TO_MEM', 0x25: 'SET_STATE', 0x23: 'VIZ_QUERY'}
FUNC = re.compile(r'DEFINE_REX_FUNC\((\w+)\)')
LIS = re.compile(r'// lis (r\d+),(-?\d+)')
LOW = re.compile(r'// (ori|addi) (r\d+),(r\d+),(-?\d+)')
MEM = re.compile(r'// (lbz|lhz|lha|lwz|ld|lfs|lfd|stb|sth|stw|std|stfs|stfd)u? (r\d+|f\d+),(-?\d+)\((r\d+)\)')
MR = re.compile(r'// mr (r\d+),(r\d+)')
BL = re.compile(r'// bl 0x([0-9a-f]+)')


def blocks(wanted):
    for name in sorted(os.listdir(GEN)):
        if not name.startswith('amped3_recomp'):
            continue
        cur, lines = None, []
        for line in open(os.path.join(GEN, name), errors='ignore'):
            m = FUNC.match(line)
            if m:
                if cur:
                    yield cur, lines
                cur, lines = (m.group(1) if m.group(1) in wanted else None), []
            elif cur and '// ' in line:
                lines.append(line.strip())
        if cur:
            yield cur, lines


def profile(lines):
    regs, ops, dev, calls, alias = {}, collections.Counter(), collections.Counter(), [], {'r3'}
    for line in lines:
        m = LIS.search(line)
        if m:
            regs[m.group(1)] = (int(m.group(2)) & 0xFFFF) << 16
            continue
        m = LOW.search(line)
        if m and m.group(3) in regs:
            imm = int(m.group(4))
            v = regs[m.group(3)] | (imm & 0xFFFF) if m.group(1) == 'ori' else (regs[m.group(3)] + imm) & 0xFFFFFFFF
            if v >> 30 == 3:
                ops[f"{OPS.get((v >> 8) & 0x7F, hex((v >> 8) & 0x7F))}/{((v >> 16) & 0x3FFF) + 1}"] += 1
            elif v >> 30 == 0 and v & 0x7FFF and (v & 0x7FFF) < 0x5003 and (v >> 16) & 0x3FFF < 0x200:
                ops[f"T0:{v & 0x7FFF:04X}x{((v >> 16) & 0x3FFF) + 1}"] += 1
            continue
        m = MR.search(line)
        if m:
            if m.group(2) in alias:
                alias.add(m.group(1))
            elif m.group(1) in alias:
                alias.discard(m.group(1))
        m = MEM.search(line)
        if m and m.group(4) in alias:
            dev[('st' if m.group(1).startswith('st') else 'ld') + str(int(m.group(3)))] += 1
        m = BL.search(line)
        if m:
            calls.append('sub_' + m.group(1).upper())
    return ops, dev, calls, len(lines)


if __name__ == '__main__':
    wanted = set(sys.argv[1:])
    for name, lines in blocks(wanted):
        ops, dev, calls, n = profile(lines)
        print(f"{name} ({n} instr)")
        print("  pm4:", ' '.join(f"{k}x{v}" for k, v in sorted(ops.items())) or '-')
        print("  r3 fields:", ' '.join(sorted(dev, key=lambda s: int(s[2:])))[:400] or '-')
        print("  calls:", ' '.join(dict.fromkeys(calls))[:300] or '-')
