#!/usr/bin/env python3
"""Experiments on Amped 3's UI package (guipackages/Game.rui, a ZIP of Lua 5.0
scenes): writes a patched copy.
  tools/rui_patch.py ORIGINAL_GAME_DIR OUT_GAME_DIR
"""
import struct
import sys
import zlib

sys.path.insert(0, __import__('os').path.dirname(__file__))
import lua50  # noqa: E402


def const(f, value):
    """Index of a constant, added if missing."""
    if value not in f.consts:
        f.consts.append(value)
    i = f.consts.index(value)
    assert i < lua50.MAXSTACK, 'too many constants for an RK operand'
    return i + lua50.MAXSTACK


def find_settable(f, key, value):
    for pc, ins in enumerate(f.code):
        op, a, b, c, _ = lua50.decode(ins)
        if op == lua50.OP['SETTABLE'] and b >= lua50.MAXSTACK and c >= lua50.MAXSTACK and \
                f.consts[b - lua50.MAXSTACK] == key and f.consts[c - lua50.MAXSTACK] == value:
            return pc, a
    raise KeyError(f'{key}={value}')


# StringTables/<lang>/StringTable.dat: u32 font count, 48-byte font names, u32
# string count, u32 record offsets (sorted by id, from after the next u32), u32
# (file size - 0xFC), then records: u8 id length, id, NUL, pad to an even file
# offset, u16 length, UTF-16BE text, u8 font, f32 scale.
def load_strings(d):
    fonts = struct.unpack('>I', d[:4])[0]
    top = 4 + fonts * 48
    count = struct.unpack('>I', d[top:top + 4])[0]
    offs = struct.unpack(f'>{count}I', d[top + 4:top + 4 + 4 * count])
    base = top + 8 + 4 * count
    recs = []
    for off in offs:
        p = base + off
        n = d[p]
        sid = d[p + 1:p + 1 + n]
        q = p + 2 + n
        q += q & 1
        m = struct.unpack('>H', d[q:q + 2])[0]
        text = d[q + 2:q + 2 + 2 * m]
        r = q + 2 + 2 * m
        recs.append([sid, text, d[r], d[r + 1:r + 5]])
    return d[:top], recs


def save_strings(head, recs):
    recs = sorted(recs, key=lambda r: r[0])
    base = len(head) + 8 + 4 * len(recs)
    body, offs = bytearray(), []
    for sid, text, font, scale in recs:
        offs.append(len(body))
        body += bytes([len(sid)]) + sid + b'\0'
        if (base + len(body)) & 1:
            body += b'\0'
        body += struct.pack('>H', len(text) // 2) + text + bytes([font]) + scale
    out = bytearray(head) + struct.pack('>I', len(recs)) + struct.pack(f'>{len(recs)}I', *offs)
    out += struct.pack('>I', base + len(body) - 0xFC) + body
    return bytes(out)


def add_strings(data, strings):
    head, recs = load_strings(data)
    known = {r[0] for r in recs}
    for sid, text in strings.items():
        if sid not in known:
            recs.append([sid, text.encode('utf-16-be'), 0, struct.pack('>f', 1.0)])
    return save_strings(head, recs)


REAMPED_STRINGS = {
    b'ReAmped3_Options': 'ReAmped 3',
    b'ReAmped3_Title': 'ReAmped 3',
    # Rewritten in memory with the current values (options_hooks.cpp): padded
    # to the longest value (values centred).
    b'ReAmped3_Row1': 'Music: Game'.ljust(20),
    b'ReAmped3_Row2': 'More options',
    b'ReAmped3_Row3': 'Frame rate',
    b'ReAmped3_Row4': 'Anti-aliasing',
    b'ReAmped3_Val3': ' 60 ',
    b'ReAmped3_Val4': ' SMAA ',
}


def patch_options_scene_data(data):
    header, f = lua50.load(data)
    pc, a = find_settable(f, b'TextID', b'Options_Button02')
    f.code[pc] = lua50.encode(lua50.OP['SETTABLE'], a, const(f, b'TextID'), const(f, b'ReAmped3_Options'))
    return lua50.save(header, f)


def enable_reamped_item(data, index):
    """The scene's ButtonList item `index` (1-based; NEWTABLE order in its
    Items list): enabled, A runs $Select(ReAmped3)."""
    header, f = lua50.load(data)
    OP = lua50.OP
    k = lambda v: v - lua50.MAXSTACK
    # The Items list: tables whose first field set is 'Enabled' or 'AButton'.
    items = []
    for pc, ins in enumerate(f.code):
        op, a, b, c, _ = lua50.decode(ins)
        if op == OP['SETTABLE'] and b >= lua50.MAXSTACK and f.consts[k(b)] == b'Enabled':
            items.append((pc, a, c))
    pc, a, c = items[index - 1]
    load = f.code[pc - 1]
    lop, la, _, _, _ = lua50.decode(load)
    assert lop == OP['LOADBOOL'] and la == c
    f.code[pc - 1] = lua50.encode(OP['LOADBOOL'], la, 1, 0)
    f.code.insert(pc + 1, lua50.encode(OP['SETTABLE'], a, const(f, b'AButton'), const(f, b'$Select(ReAmped3)')))
    f.lineinfo.insert(pc + 1, f.lineinfo[pc])
    return lua50.save(header, f)


NATIVE_SCENE = True


def settable_pcs(f, key, value=None):
    """pcs of SETTABLE r[key] = value (constants)."""
    out = []
    for pc, ins in enumerate(f.code):
        op, a, b, c, _ = lua50.decode(ins)
        if op == lua50.OP['SETTABLE'] and b >= lua50.MAXSTACK and f.consts[b - lua50.MAXSTACK] == key and \
                (value is None or (c >= lua50.MAXSTACK and f.consts[c - lua50.MAXSTACK] == value)):
            out.append(pc)
    return out


def set_value(f, pc, value):
    op, a, b, c, _ = lua50.decode(f.code[pc])
    f.code[pc] = lua50.encode(op, a, b, const(f, value))


def insert(f, pc, ins):
    f.code.insert(pc, ins)
    f.lineinfo.insert(pc, f.lineinfo[min(pc, len(f.lineinfo) - 1)])


# The game-styled ReAmped 3 screen: GlobalOptionsMenu2 cloned. Rows (top to
# bottom): Music (A / left / right cycle), More (the overlay menu), Frame rate
# and Anti-aliasing with the game's arrows and value text. Labels and values are
# our strings; options_hooks.cpp rewrites the value texts in memory.
NATIVE_ROWS = [  # (A, Left, Right) commands per row: letters and _ only
    ('music_next', 'music_prev', 'music_next'),
    ('more', None, None),
    ('fps_next', 'fps_prev', 'fps_next'),
    ('aa_next', 'aa_prev', 'aa_next'),
]


def native_scene(data):
    """Scenes/ReAmped3.scene."""
    header, f = lua50.load(data)
    f.consts[f.consts.index(b'GlobalOptionsMenu2')] = b'ReAmped3'
    for pc in settable_pcs(f, b'TextID', b'OnOff_Button_Off')[:1] + settable_pcs(f, b'TextID', b'OnOff_Button_On')[:1]:
        set_value(f, pc, b'ReAmped3_Val3')
    for pc in settable_pcs(f, b'TextID', b'OnOff_Button_Off') + settable_pcs(f, b'TextID', b'OnOff_Button_On'):
        if f.consts[lua50.decode(f.code[pc])[3] - lua50.MAXSTACK] != b'ReAmped3_Val3':
            set_value(f, pc, b'ReAmped3_Val4')
    # The ButtonList items: each item table is the target of an 'Enabled' set.
    enabled = settable_pcs(f, b'Enabled')
    assert len(enabled) == 4
    for row in reversed(range(4)):
        pc = enabled[row]
        _, a, _, c, _ = lua50.decode(f.code[pc])
        load = lua50.decode(f.code[pc - 1])
        assert load[0] == lua50.OP['LOADBOOL'] and load[1] == c
        f.code[pc - 1] = lua50.encode(lua50.OP['LOADBOOL'], c, 1, 0)
        # Drop the item's own A / Left / Right, then set ours after 'Enabled'.
        start = pc
        while start > 0 and lua50.decode(f.code[start - 1])[0] != lua50.OP['NEWTABLE']:
            start -= 1
        end = pc
        while end + 1 < len(f.code) and lua50.decode(f.code[end + 1])[0] == lua50.OP['SETTABLE'] and \
                lua50.decode(f.code[end + 1])[1] == a:
            end += 1
        cmds = dict(zip((b'AButton', b'Left', b'Right'), NATIVE_ROWS[row]))
        present = set()
        for q in range(start, end + 1):
            op, qa, qb, qc, _ = lua50.decode(f.code[q])
            key = f.consts[qb - lua50.MAXSTACK] if qb >= lua50.MAXSTACK else None
            if op == lua50.OP['SETTABLE'] and qa == a and key in cmds:
                set_value(f, q, f'$Select(ReAmped3_{cmds[key]})'.encode() if cmds[key] else b'')
                present.add(key)
        for key in (b'Right', b'Left', b'AButton'):
            if cmds[key] and key not in present:
                insert(f, pc + 1, lua50.encode(lua50.OP['SETTABLE'], a, const(f, key),
                                               const(f, f'$Select(ReAmped3_{cmds[key]})'.encode())))
    return lua50.save(header, f)


def native_scene_data(data):
    """Scenes/ReAmped3.sceneData: our title and row labels."""
    header, f = lua50.load(data)
    for pc in settable_pcs(f, b'TextID', b'Options_Title'):
        set_value(f, pc, b'ReAmped3_Title')
    for old, new in ((b'Options_Button02', b'ReAmped3_Row2'), (b'Options_Button03', b'ReAmped3_Row3'),
                     (b'Options_Button05', b'ReAmped3_Row4')):
        set_value(f, settable_pcs(f, b'TextID', old)[0], new)
    # The top row's label has no TextID (the template's default): add one to
    # its empty ChangedAttributes table.
    pc = settable_pcs(f, b'Name', b'TMP_Button_OptionsSecond:Menu')[0]
    while True:
        pc -= 1
        op, a, b, c, _ = lua50.decode(f.code[pc])
        if op == lua50.OP['SETTABLE'] and f.consts[b - lua50.MAXSTACK] == b'ChangedAttributes':
            break
    attrs = c  # register of the empty table
    tmp = attrs + 1
    assert tmp + 1 < f.maxstack
    for ins in reversed([
        lua50.encode(lua50.OP['NEWTABLE'], tmp, 0, 1),
        lua50.encode(lua50.OP['SETTABLE'], tmp, const(f, b'TextID'), const(f, b'ReAmped3_Row1')),
        lua50.encode(lua50.OP['SETTABLE'], attrs, const(f, b'Menu|Color_Text|Text'), tmp),
    ]):
        insert(f, pc, ins)
    return lua50.save(header, f)


def package_list(data):
    return data.replace(b'\t\t"GlobalOptionsMenu2",\r\n', b'\t\t"GlobalOptionsMenu2",\r\n\t\t"ReAmped3",\r\n', 1)


def front_options_scene(data):
    """Shell.rui GlobalOptionsMenu.scene: a sixth, scrolling item "ReAmped 3".
    The button list's row texts come from its items (ItemMapping, as the
    On/Off lists do), so a sixth item scrolls into the five cells."""
    header, f = lua50.load(data)
    OP = lua50.OP
    items_pc = settable_pcs(f, b'Items')[0]  # SETTABLE r3['Items'] = r4 (the ButtonList's)
    _, info, _, items_reg, _ = lua50.decode(f.code[items_pc])
    setlist = items_pc - 1
    op, a, b, c, bx = lua50.decode(f.code[setlist])
    assert a == items_reg and bx == 4, (op, a, bx)
    # Each item's text.
    enabled = [pc for pc in settable_pcs(f, b'Enabled') if pc < setlist]
    assert len(enabled) == 5
    for n in reversed(range(5)):
        pc = enabled[n]
        reg = lua50.decode(f.code[pc])[1]
        insert(f, pc + 1, lua50.encode(OP['SETTABLE'], reg, const(f, b'TextID'), const(f, f'Options_Button0{n + 1}'.encode())))
    # The sixth item, before the SETLIST.
    setlist = settable_pcs(f, b'Items')[0] - 1
    r = items_reg + 6
    f.maxstack = max(f.maxstack, r + 2)
    new = [
        lua50.encode(OP['NEWTABLE'], r, 0, 3),
        lua50.encode(OP['LOADBOOL'], r + 1, 1, 0),
        lua50.encode(OP['SETTABLE'], r, const(f, b'Enabled'), r + 1),
        lua50.encode(OP['SETTABLE'], r, const(f, b'AButton'), const(f, b'$Select(ReAmped3Menu)')),
        lua50.encode(OP['SETTABLE'], r, const(f, b'TextID'), const(f, b'ReAmped3_Options')),
    ]
    for ins in reversed(new):
        insert(f, setlist, ins)
    setlist += len(new)
    op, a, b, c, bx = lua50.decode(f.code[setlist])
    f.code[setlist] = lua50.encode(op, a, bx=5)
    # The items table's array size hint.
    for pc in range(setlist, -1, -1):
        op, a, b, c, _ = lua50.decode(f.code[pc])
        if op == OP['NEWTABLE'] and a == items_reg and b == 5:
            f.code[pc] = lua50.encode(op, a, 6, c)
            break
    # The mapping, after SETTABLE info['Items'] = items.
    items_pc = settable_pcs(f, b'Items')[0]
    t, m = items_reg, items_reg + 1
    mapping = [
        lua50.encode(OP['NEWTABLE'], t, 0, 0),
        lua50.encode(OP['NEWTABLE'], m, 0, 2),
        lua50.encode(OP['SETTABLE'], m, const(f, b'Control'), const(f, b'Text.textID')),
        lua50.encode(OP['SETTABLE'], m, const(f, b'SourceAttribute'), const(f, b'TextID')),
        lua50.encode(OP['SETTABLE'], t, const_num(f, 1.0), m),
        lua50.encode(OP['SETTABLE'], info, const(f, b'ItemMapping'), t),
    ]
    for ins in reversed(mapping):
        insert(f, items_pc + 1, ins)
    return lua50.save(header, f)


def const_num(f, value):
    if value not in f.consts:
        f.consts.append(value)
    return [i for i, k in enumerate(f.consts) if isinstance(k, float) and k == value][0] + lua50.MAXSTACK


SHELL_PATCHES = {
    'Scenes/GlobalOptionsMenu.scene': front_options_scene,
}


GAME_PATCHES = {
    'Game.package': package_list,
    'Scenes/GlobalOptionsMenu2.sceneData': patch_options_scene_data,
    # In-ride Global Options: Cheat Codes (item 2, always disabled while riding)
    # becomes ReAmped 3.
    'Scenes/GlobalOptionsMenu2.scene': lambda d: enable_reamped_item(d, 2),
}
BASE_PATCHES = {
    'StringTables/eng/StringTable.dat': lambda d: add_strings(d, REAMPED_STRINGS),
}


def stored_deflate(raw):
    """A deflate stream of stored blocks (what the C++ patcher writes)."""
    out = bytearray()
    for i in range(0, max(len(raw), 1), 65535):
        chunk = raw[i:i + 65535]
        final = 1 if i + 65535 >= len(raw) else 0
        out += bytes([final]) + struct.pack('<HH', len(chunk), len(chunk) ^ 0xFFFF) + chunk
    return bytes(out)


def rebuild(data, replace, add=None):
    """The ZIP with some entries' contents replaced, every other byte as it was
    (the game's reader rejects a ZIP rewritten by Python's zipfile). `add`:
    new entries, name -> (template entry name, contents), appended with the
    template's headers."""
    add = add or {}
    templates = {}
    eocd = data.rindex(b'PK\x05\x06')
    count, cd_size, cd_off = struct.unpack('<HII', data[eocd + 10:eocd + 20])
    out, central, pos = bytearray(), bytearray(), cd_off
    for _ in range(count):
        fixed = data[pos:pos + 46]
        nlen, xlen, clen = struct.unpack('<HHH', fixed[28:34])
        name = data[pos + 46:pos + 46 + nlen].decode('latin1')
        rest = data[pos + 46:pos + 46 + nlen + xlen + clen]
        local = struct.unpack('<I', fixed[42:46])[0]
        lnlen, lxlen = struct.unpack('<HH', data[local + 26:local + 30])
        csize = struct.unpack('<I', fixed[20:24])[0]
        lhead = bytearray(data[local:local + 30 + lnlen + lxlen])
        body = data[local + 30 + lnlen + lxlen:local + 30 + lnlen + lxlen + csize]
        fixed = bytearray(fixed)
        if name in replace:
            raw = replace[name](zlib.decompress(body, -15))
            body = stored_deflate(raw)
            fields = struct.pack('<III', zlib.crc32(raw), len(body), len(raw))
            lhead[14:26] = fields
            fixed[16:28] = fields
            print('patched', name)
        fixed[42:46] = struct.pack('<I', len(out))
        templates[name] = (bytes(lhead), bytes(fixed), lnlen, nlen)
        out += lhead + body
        central += fixed + rest
        pos += 46 + nlen + xlen + clen
    for name, (template, raw) in add.items():
        lhead, fixed, lnlen, nlen = templates[template]
        nb = name.encode('latin1')
        body = stored_deflate(raw)
        fields = struct.pack('<III', zlib.crc32(raw), len(body), len(raw))
        lhead = bytearray(lhead[:26]) + struct.pack('<HH', len(nb), 0) + nb
        lhead[14:26] = fields
        fixed = bytearray(fixed)
        fixed[16:28] = fields
        fixed[28:34] = struct.pack('<HHH', len(nb), 0, 0)
        fixed[42:46] = struct.pack('<I', len(out))
        out += lhead + body
        central += fixed + nb
        print('added', name)
    end = bytearray(data[eocd:])
    end[8:12] = struct.pack('<HH', count + len(add), count + len(add))
    end[12:20] = struct.pack('<II', len(central), len(out))
    return bytes(out + central + end)


# resources.dir (a ZIP at the game root) holds an index per package, e.g.
# "GuiPackages/Game.rui": records of u32 0, 8-byte stamp, u32 compressed size,
# u32 size, u32 local header offset, u32 flags, NUL-terminated name; then u32
# record count. The game finds a package's entries through it.
def reindex(index, package, added_entries=None):
    entries = {}
    eocd = package.rindex(b'PK\x05\x06')
    count, _, pos = struct.unpack('<HII', package[eocd + 10:eocd + 20])
    for _ in range(count):
        csize, size = struct.unpack('<II', package[pos + 20:pos + 28])
        nlen, xlen, clen = struct.unpack('<HHH', package[pos + 28:pos + 34])
        offset = struct.unpack('<I', package[pos + 42:pos + 46])[0]
        entries[package[pos + 46:pos + 46 + nlen]] = (csize, size, offset)
        pos += 46 + nlen + xlen + clen
    out, p, recs = bytearray(), 0, {}
    while p + 28 <= len(index):
        end = index.index(b'\0', p + 28)
        name = index[p + 28:end]
        rec = bytearray(index[p:end + 1])
        rec[12:24] = struct.pack('>III', *entries[name])
        recs[name] = rec
        out += rec
        p = end + 1
    added = 0
    for name, (template, _) in (added_entries or {}).items():
        rec = bytearray(recs[template.encode()][:28]) + name.encode() + b'\0'
        rec[12:24] = struct.pack('>III', *entries[name.encode()])
        out += rec
        added += 1
    tail = struct.pack('>I', struct.unpack('>I', index[p:p + 4])[0] + added)
    return bytes(out + tail)


def main(src_dir, dst_dir):
    """src_dir / dst_dir: game data folders. Writes patched GuiPackages\\Game.rui,
    Base.rui and resources.dir into dst_dir (unlinking first: they may be hard
    links to the originals)."""
    import os

    def write(rel, data):
        out = os.path.join(dst_dir, rel)
        if os.path.exists(out):
            os.unlink(out)
        open(out, 'wb').write(data)

    import zipfile
    packages, adds = {}, {}
    title = os.environ.get('TITLE_XPR')  # a prepared AmpedBoxLogo.xpr (tools/art)
    ruirr = {'Textures/AmpedBoxLogo.xpr': lambda d: open(title, 'rb').read()} if title else {}
    for name, patches in (('Game.rui', GAME_PATCHES), ('Base.rui', BASE_PATCHES), ('Shell.rui', SHELL_PATCHES),
                          ('Shell.ruirr', ruirr)):
        src = open(os.path.join(src_dir, 'GuiPackages', name), 'rb').read()
        add = {}
        if name == 'Game.rui' and NATIVE_SCENE:
            z = zipfile.ZipFile(os.path.join(src_dir, 'GuiPackages', name))
            for ext, fn in (('scene', native_scene), ('sceneData', native_scene_data), ('anims', None), ('actions', None)):
                t = f'Scenes/GlobalOptionsMenu2.{ext}'
                raw = z.read(t)
                add[f'Scenes/ReAmped3.{ext}'] = (t, fn(raw) if fn else raw)
        adds['GuiPackages/' + name] = add
        packages['GuiPackages/' + name] = rebuild(src, patches, add)
        write(os.path.join('GuiPackages', name), packages['GuiPackages/' + name])
    resources = open(os.path.join(src_dir, 'resources.dir'), 'rb').read()
    write('resources.dir', rebuild(resources, {
        name: (lambda data, pkg=pkg, a=adds[name]: reindex(data, pkg, a)) for name, pkg in packages.items()}))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
