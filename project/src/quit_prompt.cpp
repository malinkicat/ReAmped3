// quit_prompt.cpp - "Quit ReAmped 3?" (Escape, or hold Back + Start)
//
// A console game has no quit option. Escape (keyboard) or holding Back + Start
// for a second (controller) opens a small prompt in the name prompt's style:
// A / Enter quits, B / Escape returns to the game. Game input is paused while
// it's open (rex_app.cpp's input active callback); the game itself keeps
// running - with Back + Start it sees Start and opens its own pause menu.

#include "quit_prompt.h"

#include "fonts.h"
#include "game_menu.h"
#include "scripted_input.h"

#include <rex/kernel/xam/private.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/windowed_app_context.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

void Amped3_SaveAndQuit();  // save_hooks.cpp: saves, then QuitFromAnyThread
bool Amped3_WillSaveOnQuit();  // save_hooks.cpp

namespace amped3::quit_prompt {
namespace {

constexpr uint16_t kPadStart = 0x0010, kPadBack = 0x0020, kPadA = 0x1000, kPadB = 0x2000;
constexpr uint32_t kNavy = 0x0f1724, kHeader = 0x1c2e48, kBorder = 0x263852, kField = 0x18253a,
                   kText = 0xe6eef6, kMuted = 0xa9bbd0;

rex::ui::WindowedAppContext* g_app_context = nullptr;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
std::function<void()> g_quit;
std::atomic<bool> g_open{false};
// Back + Start hold (game thread).
double g_hold_start = -1.0;
bool g_hold_fired = false;

ImU32 Color(uint32_t rgb, uint8_t alpha = 255) {
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

class QuitDialog : public rex::ui::ImGuiDialog {
 public:
  explicit QuitDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnClose() override { g_open = false; }

  void OnDraw(ImGuiIO& io) override {
    const char* id = "##quit_prompt";
    if (!opened_) {
      ImGui::OpenPopup(id);
      opened_ = true;
    }
    const float scale = io.DisplaySize.y / 1080.0f;
    const float width = 520.0f * scale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(kNavy));
    ImGui::PushStyleColor(ImGuiCol_Border, Color(kBorder));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(4, 8, 14, 150));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginPopupModal(id, nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav)) {
      const int choice = Input();
      Draw(scale, width);
      if (choice) {
        ImGui::CloseCurrentPopup();
        Close();
        if (choice > 0) {
          REXLOG_INFO("Amped3: quit from the quit prompt");
          Amped3_SaveAndQuit();
        }
      }
      ImGui::EndPopup();
    } else {
      Close();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
  }

 private:
  // 1 = quit, -1 = back to the game, 0 = nothing yet. Pad buttons count only
  // after all were released once, and act on release (so a held A or B
  // doesn't reach the game when it resumes).
  int Input() {
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
      return 1;
    }
    // The Escape that opened the prompt must not close it again.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && frames_ > 1) {
      return -1;
    }
    ++frames_;
    const uint16_t pad = PadButtons() & (kPadA | kPadB | kPadStart | kPadBack);
    if (!armed_) {
      armed_ = pad == 0;
      return 0;
    }
    if (pad) {
      pressed_ |= pad;
      return 0;
    }
    if (pressed_ & kPadA) return 1;
    if (pressed_ & kPadB) return -1;
    pressed_ = 0;
    return 0;
  }

  void Draw(float scale, float width) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float pad = 26.0f * scale;
    const float header_h = 96.0f * scale;
    draw->AddRectFilledMultiColor(o, ImVec2(o.x + width, o.y + header_h), Color(kHeader),
                                  Color(kHeader), Color(kNavy), Color(kNavy));
    for (int i = 0; i < 40; ++i) {
      const float fx = float((i * 7919) % 1000) / 1000.0f;
      const float fy = float((i * 104729) % 1000) / 1000.0f;
      draw->AddCircleFilled(ImVec2(o.x + fx * width, o.y + fy * header_h),
                            (i % 3 == 0 ? 1.4f : 1.0f) * scale, Color(0xffffff, 45));
    }
    float y = o.y + pad;
    ImFont* display = fonts::Find("ReAmped Display");
    ImFont* ui = fonts::Find("ReAmped UI");
    ImFont* ui_bold = fonts::Find("ReAmped UI Bold");
    ImGui::PushFont(display, 44.0f * scale);
    draw->AddText(ImVec2(o.x + pad, y), Color(0xffffff), "Quit ReAmped 3?");
    ImGui::PopFont();
    y += 64.0f * scale;
    ImGui::PushFont(ui, 20.0f * scale);
    draw->AddText(ImVec2(o.x + pad, y), Color(kMuted),
                  Amped3_WillSaveOnQuit() ? "Your progress is saved first." : "Progress since your last save is lost.");
    ImGui::PopFont();
    y += 46.0f * scale;
    // Buttons: both plain, no highlight - they're pressed with A / B (or a
    // click), never selected, so nothing may look focused.
    const float bh = 48.0f * scale, gap = 12.0f * scale;
    const float bw = (width - 2.0f * pad - gap) * 0.5f;
    struct Button {
      const char* glyph;
      uint32_t glyph_color;
      const char* label;
      bool action;
    };
    const Button buttons[] = {{"B", 0xd43b3b, "Back to game", false}, {"A", 0x3fae3f, "Quit", true}};
    ImGui::PushFont(ui_bold, 22.0f * scale);
    for (int i = 0; i < 2; ++i) {
      const ImVec2 b0(o.x + pad + float(i) * (bw + gap), y), b1(b0.x + bw, y + bh);
      draw->AddRectFilled(b0, b1, Color(kField), 8.0f * scale);
      draw->AddRect(b0, b1, Color(kBorder), 8.0f * scale);
      const float r = 11.0f * scale;
      const ImVec2 label_size = ImGui::CalcTextSize(buttons[i].label);
      const float content_w = 2.0f * r + 10.0f * scale + label_size.x;
      const float cx = b0.x + (bw - content_w) * 0.5f;
      const ImVec2 c(cx + r, b0.y + bh * 0.5f);
      draw->AddCircleFilled(c, r, Color(buttons[i].glyph_color));
      const ImVec2 gs = ImGui::CalcTextSize(buttons[i].glyph);
      draw->AddText(ImVec2(c.x - gs.x * 0.5f, c.y - gs.y * 0.5f), Color(0xffffff),
                    buttons[i].glyph);
      draw->AddText(ImVec2(cx + 2.0f * r + 10.0f * scale, b0.y + (bh - label_size.y) * 0.5f),
                    Color(kText), buttons[i].label);
      if (ImGui::IsMouseHoveringRect(b0, b1) && ImGui::IsMouseClicked(0)) {
        mouse_choice_ = buttons[i].action ? 1 : -1;
      }
    }
    ImGui::PopFont();
    y += bh + pad;
    ImGui::Dummy(ImVec2(width, y - o.y));
    if (mouse_choice_) {
      const int choice = mouse_choice_;
      mouse_choice_ = 0;
      ImGui::CloseCurrentPopup();
      Close();
      if (choice > 0) Amped3_SaveAndQuit();
    }
  }

  bool opened_ = false;
  bool armed_ = false;
  uint16_t pressed_ = 0;
  int frames_ = 0;
  int mouse_choice_ = 0;
};

}  // namespace

void Initialize(rex::ui::WindowedAppContext& app_context, rex::ui::ImGuiDrawer* drawer,
                std::function<void()> quit) {
  g_app_context = &app_context;
  g_drawer = drawer;
  g_quit = std::move(quit);
}

void Open() {
  // Not over the ReAmped 3 menu (its Escape means back; its Quit item closes
  // it before opening this).
  if (!g_drawer || g_open || game_menu::IsShowing()) {
    return;
  }
  if (rex::kernel::xam::xeXamIsUIActive()) {
    REXLOG_INFO("Amped3: quit prompt: not while a system dialog is open");
    return;
  }
  g_open = true;
  new QuitDialog(g_drawer);  // Deletes itself when closed.
}

void OpenFromAnyThread() {
  if (g_app_context) {
    g_app_context->CallInUIThread([] { Open(); });
  }
}

void QuitFromAnyThread() {
  if (g_app_context) {
    g_app_context->CallInUIThread([] {
      REXLOG_INFO("Amped3: quit from the menu");
      if (g_quit) g_quit();
    });
  }
}

bool IsOpen() { return g_open; }

void OnPadButtons(uint16_t buttons, double now_seconds) {
  if ((buttons & (kPadBack | kPadStart)) != (kPadBack | kPadStart)) {
    g_hold_start = -1.0;
    g_hold_fired = false;
    return;
  }
  if (g_hold_start < 0.0) {
    g_hold_start = now_seconds;
  }
  if (!g_hold_fired && now_seconds - g_hold_start >= 1.0 && g_app_context) {
    g_hold_fired = true;
    REXLOG_INFO("Amped3: quit prompt: Back + Start held");
    OpenFromAnyThread();
  }
}

uint16_t PadButtons() {
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
  // With --scripted_input (tests), its buttons too.
  uint16_t buttons = scripted_input::HeldButtons();
  for (uint32_t user = 0; get_state && user < 4; ++user) {
    XInputState state{};
    if (get_state(user, &state) == 0) {
      buttons |= state.buttons;
    }
  }
  return buttons;
}

}  // namespace amped3::quit_prompt
