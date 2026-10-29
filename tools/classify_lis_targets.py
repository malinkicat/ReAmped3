"""Keep lis+addi-built code addresses that are used as function pointers: passed in an
argument register (r3-r10) or stored to memory. Jump-table bases (r12 + mtctr) are dropped.

usage: classify_lis_targets.py image.bin lis_targets.txt
"""
import struct, sys

BASE = 0x82000000
img = open(sys.argv[1], 'rb').read()
w = lambda a: struct.unpack_from('>I', img, a - BASE)[0]


def completed_reg(site):
    rd = (w(site) >> 21) & 31
    for k in range(1, 9):
        i = w(site + 4 * k)
        op = i >> 26
        if op == 14 and (i >> 16) & 31 == rd:
            return (i >> 21) & 31, site + 4 * (k + 1)
        if op == 24 and (i >> 21) & 31 == rd:
            return (i >> 16) & 31, site + 4 * (k + 1)
    return None, None


for line in open(sys.argv[2]):
    if not line.startswith('0x'):
        continue
    addr = line.split()[0]
    sites = line.split('built_by=')[1].split(',')
    keep = False
    for s in sites:
        reg, pc = completed_reg(int(s, 16))
        if reg is None:
            continue
        if 3 <= reg <= 10:
            keep = True
        for k in range(6):  # stored: stw reg, d(rA)
            i = w(pc + 4 * k)
            if i >> 26 == 36 and (i >> 21) & 31 == reg:
                keep = True
    if keep:
        print(addr)
