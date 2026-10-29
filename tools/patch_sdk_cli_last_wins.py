"""ReXGlue v0.10.0 patch: a repeated --option takes the last value instead of failing.

CLI11 rejects a repeated option and aborts the whole parse, so every flag on the
command line is dropped and the game stops with "--game_data_root was not
provided" (e.g. tools\\play_4k60.cmd --framerate=120, which already passes --framerate=60).

usage: patch_sdk_cli_last_wins.py <rexglue-src>
"""
import sys
from pathlib import Path

path = Path(sys.argv[1]) / 'src/core/cvar.cpp'
src = path.read_text()
if 'MultiOptionPolicy::TakeLast' in src:
    print('cvar.cpp: already patched')
    sys.exit(0)

old = '''          [&entry](const std::string& val) { ApplyFromSource(entry, val, Source::kCommandLine); },
          entry.description);'''
new = '''          [&entry](const std::string& val) { ApplyFromSource(entry, val, Source::kCommandLine); },
          entry.description)
          ->multi_option_policy(CLI::MultiOptionPolicy::TakeLast);'''
assert src.count(old) == 1, 'anchor not found'
path.write_text(src.replace(old, new))
print('cvar.cpp: patched')
