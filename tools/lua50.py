#!/usr/bin/env python3
"""Lua 5.0 bytecode (Amped 3's UI scenes in guipackages/*.rui): read, list, write.

  tools/lua50.py dump FILE      constants and instructions of every function
  tools/lua50.py roundtrip FILE check that load + save reproduces the file

The .rui packages are ZIP files; the scenes' .scene / .sceneData entries are
big-endian Lua 5.0 chunks (sizeof int / size_t / Instruction 4, number 8).
"""
import struct
import sys

OPS = ('MOVE LOADK LOADBOOL LOADNIL GETUPVAL GETGLOBAL GETTABLE SETGLOBAL SETUPVAL SETTABLE '
       'NEWTABLE SELF ADD SUB MUL DIV POW UNM NOT CONCAT JMP EQ LT LE TEST CALL TAILCALL RETURN '
       'FORLOOP TFORLOOP TFORPREP SETLIST SETLISTO CLOSE CLOSURE').split()
OP = {name: i for i, name in enumerate(OPS)}
MAXSTACK = 250  # RK operands >= this are constants


class Function:
    def __init__(self):
        self.source = None
        self.line = 0
        self.nups = self.numparams = self.is_vararg = self.maxstack = 0
        self.lineinfo, self.locvars, self.upvalues = [], [], []
        self.consts, self.protos, self.code = [], [], []


class Reader:
    def __init__(self, data):
        self.d, self.o = data, 0

    def take(self, n):
        b = self.d[self.o:self.o + n]
        self.o += n
        return b

    def byte(self): return self.take(1)[0]
    def int(self): return struct.unpack('>i', self.take(4))[0]
    def num(self): return struct.unpack('>d', self.take(8))[0]

    def string(self):
        n = struct.unpack('>I', self.take(4))[0]
        return None if n == 0 else self.take(n)[:-1]

    def function(self):
        f = Function()
        f.source = self.string()
        f.line = self.int()
        f.nups, f.numparams, f.is_vararg, f.maxstack = self.take(4)
        f.lineinfo = [self.int() for _ in range(self.int())]
        f.locvars = [(self.string(), self.int(), self.int()) for _ in range(self.int())]
        f.upvalues = [self.string() for _ in range(self.int())]
        for _ in range(self.int()):
            t = self.byte()
            if t == 9:
                n = self.int()
                f.consts.append(WStr(self.take(2 * n).decode('utf-16-be')[:-1]))
                continue
            assert t in (0, 3, 4), f'constant type {t}'
            f.consts.append(self.num() if t == 3 else self.string() if t == 4 else None)
        f.protos = [self.function() for _ in range(self.int())]
        f.code = [struct.unpack('>I', self.take(4))[0] for _ in range(self.int())]
        return f


class WStr(str):
    """A UTF-16 string constant (type 9, the game's Lua: a count of code
    units including the terminator, then big-endian code units)."""


HEADER = b'\x1bLua\x50\x00\x04\x04\x04\x06\x08\x09\x09\x08'


def load(data):
    assert data[:len(HEADER)] == HEADER, 'not a big-endian Lua 5.0 chunk'
    r = Reader(data)
    r.o = len(HEADER) + 8  # the test number
    f = r.function()
    assert r.o == len(data), f'{len(data) - r.o} trailing bytes'
    return data[:len(HEADER) + 8], f


def save(header, f):
    out = bytearray(header)

    def string(s):
        if s is None:
            out.extend(struct.pack('>I', 0))
        else:
            out.extend(struct.pack('>I', len(s) + 1) + s + b'\0')

    def i(v): out.extend(struct.pack('>i', v))

    def function(f):
        string(f.source)
        i(f.line)
        out.extend(bytes([f.nups, f.numparams, f.is_vararg, f.maxstack]))
        i(len(f.lineinfo)); [i(x) for x in f.lineinfo]
        i(len(f.locvars))
        for name, a, b in f.locvars:
            string(name); i(a); i(b)
        i(len(f.upvalues)); [string(u) for u in f.upvalues]
        i(len(f.consts))
        for k in f.consts:
            if k is None:
                out.append(0)
            elif isinstance(k, float):
                out.append(3); out.extend(struct.pack('>d', k))
            elif isinstance(k, WStr):
                out.append(9); i(len(k) + 1); out.extend((k + '\0').encode('utf-16-be'))
            else:
                out.append(4); string(k)
        i(len(f.protos)); [function(p) for p in f.protos]
        i(len(f.code)); [out.extend(struct.pack('>I', c)) for c in f.code]

    function(f)
    return bytes(out)


def decode(ins):
    op = ins & 0x3F
    return op, (ins >> 24) & 0xFF, (ins >> 15) & 0x1FF, (ins >> 6) & 0x1FF, (ins >> 6) & 0x3FFFF


def encode(op, a, b=0, c=0, bx=None):
    if bx is not None:
        return op | (bx << 6) | (a << 24)
    return op | (c << 6) | (b << 15) | (a << 24)


def show_k(f, v):
    k = f.consts[v]
    return repr(k.decode('latin1')) if isinstance(k, bytes) else f'w{str(k)!r}' if isinstance(k, WStr) else repr(k)


def rk(f, v):
    return show_k(f, v - MAXSTACK) if v >= MAXSTACK else f'r{v}'


def dump(f, depth=0):
    pad = '  ' * depth
    print(f'{pad}function line {f.line}: {len(f.code)} ins, {len(f.consts)} k, {len(f.protos)} protos')
    for pc, ins in enumerate(f.code):
        op, a, b, c, bx = decode(ins)
        name = OPS[op] if op < len(OPS) else f'op{op}'
        if name in ('LOADK', 'GETGLOBAL', 'SETGLOBAL'):
            arg = f'r{a} {show_k(f, bx)}'
        elif name in ('GETTABLE', 'SELF'):
            arg = f'r{a} r{b} [{rk(f, c)}]'
        elif name == 'SETTABLE':
            arg = f'r{a}[{rk(f, b)}] = {rk(f, c)}'
        elif name in ('JMP',):
            arg = f'-> {pc + 1 + bx - 131071}'
        elif name in ('CLOSURE', 'SETLIST', 'SETLISTO'):
            arg = f'r{a} {bx}'
        else:
            arg = f'{a} {b} {c}'
        print(f'{pad}  {pc:4} {name:9} {arg}')
    for p in f.protos:
        dump(p, depth + 1)


if __name__ == '__main__':
    cmd, path = sys.argv[1], sys.argv[2]
    data = open(path, 'rb').read()
    header, f = load(data)
    if cmd == 'dump':
        dump(f)
    elif cmd == 'roundtrip':
        print('identical' if save(header, f) == data else 'DIFFERENT')
