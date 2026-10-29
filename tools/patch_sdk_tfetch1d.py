"""Port xenia-canary PR #1129 (two-component tfetch1D coordinates) to ReXGlue v0.10.0.

Amped 3's UI shader fetches a 2D texture with tfetch1D and an XY coordinate;
dropping Y turns every UI layer into vertical stripes.

usage: patch_sdk_tfetch1d.py <rexglue-src>
"""
import sys
from pathlib import Path

SHADER = Path(sys.argv[1]) / 'src/graphics/pipeline/shader'
COORD_DIM = ('{i}xenos::FetchOpDimension coordinate_dimension =\n'
             '{i}    instr.dimension == xenos::FetchOpDimension::k1D && instr.operands[0].component_count > 1\n'
             '{i}        ? xenos::FetchOpDimension::k2D\n'
             '{i}        : instr.dimension;\n')


def patch(name, inserts, replaces):
    path = SHADER / name
    lines = path.read_text().splitlines(keepends=True)
    if any('coordinate_dimension' in l for l in lines):
        print(f'{name}: already patched')
        return
    for n, old in replaces:  # 1-based line numbers, checked before changing
        assert old in lines[n - 1], f'{name}:{n}: expected {old!r}, got {lines[n - 1]!r}'
        lines[n - 1] = lines[n - 1].replace(old, old.replace('instr.dimension', 'coordinate_dimension'))
    for n, anchor, text in sorted(inserts, reverse=True):  # insert after line n
        assert anchor in lines[n - 1], f'{name}:{n}: expected {anchor!r}'
        lines.insert(n, text)
    path.write_text(''.join(lines))
    print(f'{name}: patched {len(replaces)} sites')


SW = 'switch (instr.dimension)'
patch('translator.cpp', [(1078, 'GetFetchOpDimensionComponentCount(op.dimension())',
    '  if (op.opcode() == FetchOpcode::kTextureFetch && op.dimension() == xenos::FetchOpDimension::k1D) {\n'
    '    uint32_t src_swizzle = op.src_swizzle();\n'
    '    uint32_t src_select_x = src_swizzle & 0x3;\n'
    '    if (((src_swizzle >> 2) & 0x3) != src_select_x || ((src_swizzle >> 4) & 0x3) != src_select_x) {\n'
    '      // 1D may provide XY for a 2D fetch constant. 545407D4\'s UI shader does\n'
    '      // this (xenia-canary PR #1129).\n'
    '      src_op.component_count = 2;\n'
    '    }\n'
    '  }\n')], [])
patch('dxbc_translator_fetch.cpp',
      [(673, 'uint32_t tfetch_index = instr.operands[1].storage_index;', COORD_DIM.format(i='  '))],
      [(714, SW), (782, SW), (800, SW), (839, SW),
       (1005, 'GetFetchOpDimensionComponentCount(instr.dimension)'), (1008, SW), (1148, SW), (1431, SW),
       (1520, 'if (instr.dimension == xenos::FetchOpDimension::k1D)')])
patch('spirv_translator_fetch.cpp',
      [(593, 'fetch_constant_word_0_index = 6 * fetch_constant_index;', COORD_DIM.format(i='    '))],
      [(695, SW), (772, SW), (789, SW), (860, SW),
       (957, 'GetFetchOpDimensionComponentCount(instr.dimension)'),
       (1065, 'instr.dimension != xenos::FetchOpDimension::k1D'), (1464, SW)])
