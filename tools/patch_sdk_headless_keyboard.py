"""Test-only ReXGlue v0.10.0 patch: --headless_keyboard_text answers headless keyboard prompts
that have no default text (Amped 3's "Enter a name for your new rider").

usage: patch_sdk_headless_keyboard.py <rexglue-src>
"""
import sys
from pathlib import Path

path = Path(sys.argv[1]) / 'src/kernel/xam/xam_ui.cpp'
src = path.read_text()
if 'headless_keyboard_text' in src:
    print('xam_ui.cpp: already patched')
    sys.exit(0)

cvar_anchor = '                    "Don\'t display any UI, using defaults for prompts as needed");\n'
run_old = '''    auto run = [default_text, buffer, buffer_length, buffer_size]() -> X_RESULT {
      // Redirect default_text back into the buffer.
      if (!default_text) {
        std::memset(buffer, 0, buffer_size);
      } else {'''
run_new = '''    auto run = [default_text, buffer, buffer_length, buffer_size]() -> X_RESULT {
      // Redirect default_text back into the buffer.
      const std::string fallback = REXCVAR_GET(headless_keyboard_text);
      if ((!default_text || default_text.value().empty()) && !fallback.empty()) {
        rex::string::copy_and_swap_truncating(buffer, std::u16string(fallback.begin(), fallback.end()),
                                              buffer_length);
      } else if (!default_text) {
        std::memset(buffer, 0, buffer_size);
      } else {'''
assert src.count(cvar_anchor) == 1 and src.count(run_old) == 1, 'anchors not found'
src = src.replace(cvar_anchor, cvar_anchor + 'REXCVAR_DEFINE_STRING(headless_keyboard_text, "", "Kernel",\n'
                  '                      "With headless, text returned for keyboard prompts that have no default");\n')
src = src.replace(run_old, run_new)
path.write_text(src)
print('xam_ui.cpp: patched')
