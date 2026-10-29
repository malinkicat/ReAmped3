"""ReXGlue v0.10.0 patch: make the keyboard-input dialog usable with a controller.

The ImGui name prompt (e.g. Amped 3's "Enter a name for your new rider") had no
gamepad support and started empty, so a controller-only player could not get
past it. Now, on Windows, A confirms and B cancels (on release) (read through XInput, since
game input is paused while a system dialog is open), and when the game gives no
default text the box is pre-filled with --headless_keyboard_text (if set).
After a name is confirmed, game input stays paused for 5 s while the game
creates the save, so a second A press cannot open another prompt.
Apply after patch_sdk_headless_keyboard.py.

usage: patch_sdk_keyboard_pad.py <rexglue-src>
"""
import sys
from pathlib import Path

path = Path(sys.argv[1]) / 'src/kernel/xam/xam_ui.cpp'
src = path.read_text()
if 'PadButtonsForDialog' in src:
    print('xam_ui.cpp: already patched')
    sys.exit(0)

def rep(old, new):
    global src
    assert src.count(old) == 1, old
    src = src.replace(old, new)

rep('#include <imgui.h>\n', '''#include <imgui.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
''')
rep('class KeyboardInputDialog : public XamDialog {', '''#ifdef _WIN32
// Buttons held on any XInput controller. Game input is paused while a system
// dialog is open, so dialogs read the pad directly.
uint16_t PadButtonsForDialog() {
  struct XInputState {
    uint32_t packet;
    uint16_t buttons;
    uint8_t triggers[2];
    int16_t thumbs[4];
  };
  using GetStateFn = uint32_t(__stdcall*)(uint32_t, XInputState*);
  static GetStateFn get_state = [] {
    for (const char* name : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"}) {
      if (HMODULE module = LoadLibraryA(name)) {
        return reinterpret_cast<GetStateFn>(GetProcAddress(module, "XInputGetState"));
      }
    }
    return GetStateFn(nullptr);
  }();
  uint16_t buttons = 0;
  for (uint32_t user = 0; get_state && user < 4; ++user) {
    XInputState state{};
    if (get_state(user, &state) == 0) {
      buttons |= state.buttons;
    }
  }
  return buttons;
}
#else
uint16_t PadButtonsForDialog() { return 0; }
#endif

class KeyboardInputDialog : public XamDialog {''')
rep('''    text_ = default_text;
    text_buffer_.resize(max_length);''', '''    if (default_text_.empty()) {
      default_text_ = REXCVAR_GET(headless_keyboard_text);
    }
    text_ = default_text_;
    text_buffer_.resize(max_length);''')
rep('''        text_ = "";
        cancelled_ = true;
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::Spacing();
      ImGui::EndPopup();''', '''        text_ = "";
        cancelled_ = true;
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::TextDisabled("Controller: A = OK, B = Cancel");
      ImGui::Spacing();
      // Controller: A confirms, B cancels, on release: game input resumes when
      // the dialog closes, and a still-held A would reopen the prompt. Buttons
      // count only after all were released, so the press that opened the
      // dialog does not confirm it.
      const uint16_t pad = PadButtonsForDialog();
      constexpr uint16_t kPadA = 0x1000, kPadB = 0x2000;
      if (!pad_armed_) {
        pad_armed_ = (pad & (kPadA | kPadB)) == 0;
      } else if (pad & (kPadA | kPadB)) {
        pad_pressed_ |= pad & (kPadA | kPadB);
      } else if (pad_pressed_ & kPadA) {
        text_ = std::string(text_buffer_.data(), text_buffer_.size());
        cancelled_ = false;
        ImGui::CloseCurrentPopup();
        Close();
      } else if (pad_pressed_ & kPadB) {
        text_ = "";
        cancelled_ = true;
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::EndPopup();''')
rep('''  std::string default_text_;
  size_t max_length_ = 0;''', '''  std::string default_text_;
  size_t max_length_ = 0;
  bool pad_armed_ = false;
  uint16_t pad_pressed_ = 0;''')
rep('        // Zero the output buffer.\n        auto text = rex::string::to_utf16(dialog->text());\n        rex::string::copy_and_swap_truncating(buffer, text, buffer_length);\n        extended_error = X_ERROR_SUCCESS;\n        length = 0;\n        return X_ERROR_SUCCESS;', '        // Zero the output buffer.\n        auto text = rex::string::to_utf16(dialog->text());\n        rex::string::copy_and_swap_truncating(buffer, text, buffer_length);\n        extended_error = X_ERROR_SUCCESS;\n        length = 0;\n        // Keep game input paused a little longer: after a name is entered the\n        // game spends ~4.5 s creating and reading the save with its menu still\n        // live, and a second A there opened another prompt that was left\n        // behind when the game moved on.\n        ++xam_dialogs_shown_;\n        std::thread([] {\n          std::this_thread::sleep_for(std::chrono::seconds(5));\n          --xam_dialogs_shown_;\n        }).detach();\n        return X_ERROR_SUCCESS;')
if '#include <thread>' not in src:
    src = src.replace('#include <imgui.h>\n', '#include <imgui.h>\n\n#include <chrono>\n#include <thread>\n', 1)
# Return the typed text up to its NUL, not the whole zero-padded buffer.
src = src.replace('std::string(text_buffer_.data(), text_buffer_.size())',
                  'std::string(text_buffer_.data())')
path.write_text(src)
print('xam_ui.cpp: patched')
