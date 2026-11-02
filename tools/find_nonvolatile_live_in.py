#!/usr/bin/env python3
"""Functions that read a non-volatile register before writing it.

For trying the recompiler's non_volatile_as_local (r14-r31, f14-f31, v14-v31,
v64-v127 as C++ locals): a function entered from another function's body (a
branch into its middle, or a shared epilogue) expects the caller's values in
those registers, but would start with fresh locals. Such functions need
`share_registers = true` in overrides.toml.

Reads the current generated code, which must be generated WITHOUT
non_volatile_as_local (so the registers appear as ctx.rN). The check is in
source order per function (not full control flow), so it's an approximation:
  tools/find_nonvolatile_live_in.py [--toml]   (--toml: overrides.toml lines)
"""
import glob
import os
import re
import sys

GEN = os.path.join(os.path.dirname(__file__), '..', 'project', 'generated', 'default')
FUNC = re.compile(r'^(?:DEFINE_REX_FUNC|REX_FUNC)\((\w+)\)')
REG = re.compile(r'ctx\.([rfv])(\d+)\b')
WRITE = re.compile(r'^\s*ctx\.([rfv])(\d+)(?:\.\w+(?:\[[^\]]*\])?)?\s*=[^=]')
SAVE = re.compile(r'REX_STORE_U(?:64|32)\(ctx\.r1\.u32 \+ -?\d+, ctx\.[rf]\d+\.u(?:64|32)\)|'
                  r'store\w*\([^;]*ctx\.r1\.u32[^;]*ctx\.v\d+')
STORE = re.compile(r'(?:store\w*|memcpy)\(\s*\(?[^,]*?ctx\.([rfv])(\d+)')


def nonvolatile(kind, n):
    if kind in 'rf':
        return n >= 14
    return 14 <= n <= 31 or 64 <= n <= 127


def scan():
    found = {}
    for path in sorted(glob.glob(os.path.join(GEN, 'amped3_recomp.*.cpp'))):
        name, written, first_read = None, set(), None
        for line in open(path, errors='ignore'):
            m = FUNC.match(line)
            if m:
                if name and first_read and not name.startswith('__'):
                    found[name] = first_read
                name, written, first_read = m.group(1), set(), None
                continue
            if not name or first_read or line.lstrip().startswith('//'):
                continue
            # Saving a register to the stack frame (an inline prologue) isn't a
            # use: with locals each function saves and restores its own copy.
            if SAVE.search(line):
                continue
            writes = set()
            w = WRITE.match(line)
            if w:
                writes.add((w.group(1), int(w.group(2))))
            for s in STORE.finditer(line):
                writes.add((s.group(1), int(s.group(2))))
            # Reads on this line: occurrences that aren't the written target
            # (a self-update like ctx.r31.u64 = ctx.r31.u64 + 1 reads it).
            occurrences = [(k, int(n)) for k, n in REG.findall(line)]
            for i, reg in enumerate(occurrences):
                if not nonvolatile(*reg) or reg in written:
                    continue
                is_target = reg in writes and i == 0
                if not is_target:
                    first_read = f'{reg[0]}{reg[1]}: {line.strip()[:90]}'
                    break
            written |= writes
        if name and first_read and not name.startswith('__'):
            found[name] = first_read
    return found


if __name__ == '__main__':
    found = scan()
    if '--toml' in sys.argv:
        for name in sorted(found):
            if name.startswith('sub_'):
                print(f'0x{name[4:]} = {{ share_registers = true }}')
    else:
        for name, why in sorted(found.items()):
            print(f'{name}  {why}')
        print(f'{len(found)} functions', file=sys.stderr)
