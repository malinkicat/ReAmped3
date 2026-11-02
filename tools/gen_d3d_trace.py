#!/usr/bin/env python3
# Generates project/src/d3d_trace.cpp: hooks on the game's D3D entry points
# (private_work/native-renderer/d3d_api.json plus known internal emitters).
import json, sys
api = [a['f'] for a in json.load(open('private_work/native-renderer/d3d_api.json'))]
extra = ['sub_825A0B60', 'sub_825A0E88', 'sub_825A1278', 'sub_825A14B8', 'sub_825A1758',
         'sub_825A1DB0', 'sub_825A1F30', 'sub_825A6CA8', 'sub_8259FD60', 'sub_825A58B8',
         'sub_825A3638', 'sub_825A2FB8', 'sub_825A3BA0', 'sub_825A43D8', 'sub_825A4330',
         'sub_825AAED8', 'sub_825A8C98', 'sub_8259CFD0', 'sub_82593280']
funcs = sorted(set(api + extra))
hooks = "\n".join(
    f"REX_EXTERN(__imp__{f});\n"
    f"extern \"C\" REX_FUNC({f}) {{\n  Amped3D3DTrace({i}, ctx, base);\n  __imp__{f}(ctx, base);\n}}"
    for i, f in enumerate(funcs))
names = ",\n".join(f'    "{f}"' for f in funcs)
template = open('tools/d3d_trace.cpp.in').read()
open('project/src/d3d_trace.cpp', 'w').write(
    template.replace('@NAMES@', names).replace('@HOOKS@', hooks))
print(len(funcs), "hooks")
