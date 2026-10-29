#!/bin/bash
# Check that tools/patch_sdk_*.py, applied in order to pristine SDK sources, give
# exactly the patched tree in $SDK_SRC_WSL (so the patches are reproducible).
#   tools/check_sdk_patches.sh
cd "$(dirname "$0")/.." && . tools/env.sh
SRC=${SDK_SRC_WSL:?set SDK_SRC_WSL in tools/local.env}
T=$(mktemp -d)
FILES="src/core/cvar.cpp src/graphics/command_processor.cpp src/graphics/graphics_system.cpp
src/graphics/shared_memory.cpp src/audio/xma_context.cpp src/audio/audio_system.cpp src/kernel/xam/xam_ui.cpp src/kernel/xboxkrnl/xboxkrnl_audio_xma.cpp
src/graphics/pipeline/shader/dxbc_translator_fetch.cpp src/graphics/pipeline/shader/spirv_translator_fetch.cpp
src/graphics/pipeline/shader/translator.cpp"
for f in $FILES; do
  mkdir -p "$T/$(dirname $f)"
  git -C $SRC -c safe.directory='*' show HEAD:$f > "$T/$f" 2>/dev/null || rm -f "$T/$f"
done
cd "$(dirname "$0")"
for p in tfetch1d headless_keyboard cli_last_wins vblank_timer vblank_wake swap_times xma_trace \
         keyboard_pad cp_perf audio_levels single_music; do
  python3 patch_sdk_$p.py "$T" >/dev/null || { echo "FAIL applying $p"; exit 1; }
done
ok=1
for f in $FILES; do
  [ -f "$T/$f" ] || continue
  diff -q "$T/$f" "$SRC/$f" >/dev/null || { echo "DIFFERS: $f"; ok=0; }
done
rm -rf "$T"
[ $ok = 1 ] && echo "SDK patches reproduce the tree"
