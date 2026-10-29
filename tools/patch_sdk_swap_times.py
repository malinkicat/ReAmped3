"""Debug-only ReXGlue v0.10.0 patch: --swap_times=true writes the host time of every
guest swap the GPU command processor executes (just before the host present) to
swaptimes.csv in the working directory. The game-side VdSwap time does not show
display pacing, because the flip waits for vblank later in the command stream.

usage: patch_sdk_swap_times.py <rexglue-src>
"""
import sys
from pathlib import Path

path = Path(sys.argv[1]) / 'src/graphics/command_processor.cpp'
src = path.read_text()
if 'swap_times' in src:
    print('command_processor.cpp: already patched')
    sys.exit(0)

cvar_anchor = 'REXCVAR_DEFINE_BOOL(vsync, true, "GPU", "Enable vertical sync");\n'
swap_anchor = '''  IssueSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height);

  ++counter_;'''
swap_new = '''  if (REXCVAR_GET(swap_times)) {
    static std::FILE* csv = nullptr;
    static uint64_t first = 0, last = 0, n = 0;
    const uint64_t now = rex::chrono::Clock::QueryHostTickCount();
    const double freq = double(rex::chrono::Clock::QueryHostTickFrequency());
    if (!csv && (csv = std::fopen("swaptimes.csv", "w"))) {
      std::fputs("time_s,frame_ms\\n", csv);
      first = last = now;
    }
    if (csv) {
      std::fprintf(csv, "%.5f,%.3f\\n", (now - first) / freq, (now - last) * 1000.0 / freq);
      last = now;
      if (++n % 120 == 0) std::fflush(csv);
    }
  }
''' + swap_anchor
assert src.count(cvar_anchor) == 1 and src.count(swap_anchor) == 1, 'anchors not found'
src = src.replace(cvar_anchor, cvar_anchor + 'REXCVAR_DEFINE_BOOL(swap_times, false, "GPU",\n'
                  '                    "Debug: write the time of every guest swap to swaptimes.csv");\n')
src = src.replace(swap_anchor, swap_new)
src = src.replace('#include <cstring>\n', '#include <cstdio>\n#include <cstring>\n', 1)
path.write_text(src)
print('command_processor.cpp: patched')
