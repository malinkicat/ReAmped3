// game_menu.cpp - The ReAmped 3 menu, over the game
//
// Opened from the game's own Options (ReAmped 3 > More options): music
// (the game's soundtrack, your music folder or both; volume; next / previous
// track), frame rate, anti-aliasing, the achievements list, the controls, and
// quit. Styled after the game's own help cards: a black panel with a white
// border, yellow italic headings, hot-pink highlights and stars.
//
// Changes apply at once and are saved to settings.ini (the launcher's), so the
// launcher shows them next time. Game input is paused while the menu is open
// (rex_app.cpp's input active callback), like the quit prompt's.

#include "game_menu.h"

#include "achievement_progress.h"

#include "fonts.h"
#include "now_playing.h"
#include "quit_prompt.h"
#include "save_backup.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/kernel/xam/private.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/windowed_app_context.h>

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string_view>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

REXCVAR_DECLARE(int32_t, framerate);
REXCVAR_DEFINE_STRING(menu_debug_page, "", "Debug",
                      "Debug: open the ReAmped 3 menu on this page (achievements, backups, controls) 30 s "
                      "into play, for screenshots");

namespace amped3::game_menu {
namespace {

constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010,
                   kBack = 0x0020, kLB = 0x0100, kRB = 0x0200, kA = 0x1000, kB = 0x2000;
constexpr uint32_t kBlack = 0x0b0b12, kPink = 0xff2e9a, kPinkDark = 0xb0105f, kYellow = 0xffe14d,
                   kWhite = 0xffffff, kGrey = 0xa8a8b8;

rex::ui::WindowedAppContext* g_app_context = nullptr;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
std::atomic<bool> g_open{false};
// After closing, game input stays paused until every button is up (so the
// press that closed the menu doesn't reach the game).
std::atomic<bool> g_held_after_close{false};
// Back + LB while open: the game thread's request to close.
std::atomic<bool> g_close_requested{false};
bool g_chord_down = false;

ImU32 Color(uint32_t rgb, float alpha = 1.0f) {
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, uint8_t(255 * alpha));
}

template <typename T>
T RuntimeFunction(const char* module, const char* name) {
  HMODULE m = GetModuleHandleA(module);
  return m ? reinterpret_cast<T>(GetProcAddress(m, name)) : nullptr;
}

// settings.ini next to the game (the launcher's): replace or add key=value.
void SaveSetting(const std::string& key, const std::string& value) {
  const auto path = rex::filesystem::GetExecutableFolder() / "settings.ini";
  std::ifstream in(path, std::ios::binary);
  if (!in) return;  // started without the launcher: nothing to keep in step
  std::stringstream text;
  text << in.rdbuf();
  in.close();
  std::string out, line;
  bool found = false;
  std::istringstream lines(text.str());
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto eq = line.find('=');
    if (eq != std::string::npos && line.substr(0, eq) == key) {
      line = key + "=" + value;
      found = true;
    }
    out += line + "\r\n";
  }
  if (!found) out += key + "=" + value + "\r\n";
  std::ofstream(path, std::ios::binary | std::ios::trunc) << out;
}

std::string Flag(const char* name) { return rex::cvar::GetFlagByName(name); }

// The menus' and HUD's overall size in percent (--ui_size, ui_patch.cpp: the
// game's scenes are scaled from their anchors when the UI patch is built, so a
// change applies at the next start). Starts from the size this run has.
int StartedUiSize() {
  try {
    return std::clamp(std::stoi(rex::cvar::GetFlagByName("ui_size")), 50, 100);
  } catch (...) {
    return 100;
  }
}
int& UiSize() {
  static int size = StartedUiSize();
  return size;
}

// Under Wine on a 1280x800 primary display (as the launcher decides).
bool OnSteamDeck() {
  static const bool deck = [] {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") &&
           GetSystemMetrics(SM_CXSCREEN) == 1280 && GetSystemMetrics(SM_CYSCREEN) == 800;
  }();
  return deck;
}

// A parallelogram (the game's slanted shapes).
void Slant(ImDrawList* draw, ImVec2 a, ImVec2 b, float skew, ImU32 color) {
  draw->AddQuadFilled(ImVec2(a.x + skew, a.y), ImVec2(b.x + skew, a.y), ImVec2(b.x - skew, b.y),
                      ImVec2(a.x - skew, b.y), color);
}

void Star(ImDrawList* draw, ImVec2 c, float r, ImU32 fill, ImU32 outline) {
  ImVec2 p[10];
  for (int i = 0; i < 10; ++i) {
    const float angle = -1.5707963f + i * 0.6283185f;
    const float rr = (i & 1) ? r * 0.45f : r;
    p[i] = ImVec2(c.x + std::cos(angle) * rr, c.y + std::sin(angle) * rr);
  }
  draw->AddConcavePolyFilled(p, 10, fill);
  draw->AddPolyline(p, 10, outline, ImDrawFlags_Closed, std::max(1.0f, r * 0.12f));
}

enum class Row { kResume, kMusic, kVolume, kNext, kPrevious, kFrameRate, kAA, kUiSize, kFog, kSnow, kSpray, kHud, kAchievements, kBackups, kControls, kQuit };
enum class Page { kMain, kAchievements, kBackups, kControls };

class MenuDialog : public rex::ui::ImGuiDialog {
 public:
  explicit MenuDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {
    const std::string& page = REXCVAR_GET(menu_debug_page);
    if (page == "achievements") page_ = Page::kAchievements;
    if (page == "backups") page_ = Page::kBackups, RefreshBackups();
    if (page == "controls") page_ = Page::kControls;
  }

 protected:
  void OnClose() override {
    g_held_after_close = true;
    g_open = false;
  }

  void OnDraw(ImGuiIO& io) override {
    ++frames_;
    if (!Input()) {
      Close();
      return;
    }
    const float scale = io.DisplaySize.y / 1080.0f;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
    switch (page_) {
      case Page::kMain: DrawMain(draw, io, scale); break;
      case Page::kAchievements: DrawAchievements(draw, io, scale); break;
      case Page::kControls: DrawControls(draw, io, scale); break;
      case Page::kBackups: DrawBackups(draw, io, scale); break;
    }
  }

 private:
  std::vector<Row> Rows() const {
    const std::string music = Flag("user_music");
    const bool volume = true;  // the music volume applies to every source
    // Opened from the game's ReAmped 3 pages ("More options"): only what
    // they don't have.
    if (Flag("native_menu") == "true") {
      std::vector<Row> rows = {Row::kResume};
      if (volume) rows.push_back(Row::kVolume);
      rows.insert(rows.end(), {Row::kBackups, Row::kControls, Row::kQuit});
      return rows;
    }
    std::vector<Row> rows = {Row::kResume, Row::kMusic};
    if (volume) rows.push_back(Row::kVolume);
    // Tracks change with the D-pad in game (as the game's own songs do).
    rows.insert(rows.end(), {Row::kFrameRate, Row::kAA, Row::kUiSize, Row::kFog, Row::kSnow, Row::kSpray, Row::kHud, Row::kAchievements, Row::kBackups, Row::kControls, Row::kQuit});
    return rows;
  }

  // False to close.
  bool Input() {
    const uint16_t pad = quit_prompt::PadButtons();
    // Act on presses only after everything held when the menu opened is up.
    if (!armed_) {
      armed_ = (pad & ~uint16_t(kLeft | kRight | kUp | kDown)) == 0 || frames_ > 120;
      held_ = pad;
      return true;
    }
    const uint16_t pressed = pad & ~held_;
    held_ = pad;
    // Held directions repeat.
    const auto now = std::chrono::steady_clock::now();
    uint16_t repeat = 0;
    const uint16_t dirs = pad & (kUp | kDown | kLeft | kRight);
    if (dirs && dirs == repeat_dirs_ && now >= next_repeat_) {
      repeat = dirs;
      next_repeat_ = now + std::chrono::milliseconds(90);
    } else if (dirs != repeat_dirs_) {
      repeat_dirs_ = dirs;
      next_repeat_ = now + std::chrono::milliseconds(380);
    }
    const uint16_t act = pressed | repeat;
    auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    const bool up = (act & kUp) || key(ImGuiKey_UpArrow);
    const bool down = (act & kDown) || key(ImGuiKey_DownArrow);
    const bool left = (act & kLeft) || key(ImGuiKey_LeftArrow);
    const bool right = (act & kRight) || key(ImGuiKey_RightArrow);
    const bool accept = (pressed & kA) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const bool back = (pressed & kB) || (frames_ > 2 && ImGui::IsKeyPressed(ImGuiKey_Escape, false));
    // Back + LB closes it too (seen by OnPadButtons on the game thread).
    const bool f1 = frames_ > 2 && ImGui::IsKeyPressed(ImGuiKey_F1, false);
    if (g_close_requested.exchange(false) || f1 || (pressed & kStart)) return false;
    if (page_ == Page::kBackups) {
      const auto& backups = backups_;
      if (back) page_ = Page::kMain;
      if (up) backup_sel_ = std::max(0, backup_sel_ - 1);
      if (down) backup_sel_ = std::min(int(backups.size()) - 1, backup_sel_ + 1);
      if (accept && backup_sel_ >= 0 && backup_sel_ < int(backups.size())) {
        if (backups[backup_sel_].filename().string() == scheduled_) {
          save_backup::CancelScheduledRestore();
        } else {
          save_backup::ScheduleRestore(backups[backup_sel_]);
        }
        RefreshBackups();
      }
      return true;
    }
    if (page_ != Page::kMain) {
      if (back || accept) page_ = Page::kMain;
      if (up) scroll_ = std::max(0, scroll_ - 1);
      if (down) ++scroll_;
      return true;
    }
    if (back) return false;
    const auto rows = Rows();
    selected_ = std::clamp(selected_, 0, int(rows.size()) - 1);
    if (up) selected_ = (selected_ + int(rows.size()) - 1) % int(rows.size());
    if (down) selected_ = (selected_ + 1) % int(rows.size());
    const Row row = rows[selected_];
    if (left || right) Change(row, right ? 1 : -1);
    if (accept) return Activate(row);
    return true;
  }

  // False to close.
  bool Activate(Row row) {
    switch (row) {
      case Row::kResume: return false;
      case Row::kNext: now_playing::Skip(1); return true;
      case Row::kPrevious: now_playing::Skip(-1); return true;
      case Row::kAchievements: page_ = Page::kAchievements; scroll_ = 0; return true;
      case Row::kControls: page_ = Page::kControls; scroll_ = 0; return true;
      case Row::kBackups: page_ = Page::kBackups; backup_sel_ = 0; RefreshBackups(); return true;
      case Row::kQuit:
        // The quit prompt takes over (it pauses input too).
        g_app_context->CallInUIThreadDeferred([] { quit_prompt::Open(); });
        return false;
      default: Change(row, 1); return true;
    }
  }

 public:
  // Also used by the game-styled scene (ChangeSetting / SettingValue).
  static void Change(Row row, int delta) {
    switch (row) {
      case Row::kMusic: {
        static const char* modes[] = {"off", "on", "mix", "system"};
        static const char* saved[] = {"game", "mine", "mix", "system"};
        int i = 0;
        while (i < 4 && Flag("user_music") != modes[i]) ++i;
        i = ((i % 4) + delta + 4) % 4;
        rex::cvar::SetFlagByName("user_music", modes[i]);
        SaveSetting("music", saved[i]);
        // The other player's state is the old mode's: cleared, which also
        // tells the game whose music it is now (it only asks when told).
        static const auto set_external =
            RuntimeFunction<void (*)(int32_t, const char*)>("rexruntime.dll", "ReXSetExternalMusic");
        if (set_external) set_external(0, "");
        break;
      }
      case Row::kVolume: {
        // One music volume, whatever plays: your music, the game's songs
        // (title_music_volume) and another player (media_session.cpp).
        // Wraps round, so A alone steps through it.
        int v = std::atoi(Flag("user_music_volume").c_str()) + delta * 10;
        v = v > 100 ? 0 : v < 0 ? 100 : v;
        rex::cvar::SetFlagByName("user_music_volume", std::to_string(v));
        rex::cvar::SetFlagByName("title_music_volume", std::to_string(v));
        SaveSetting("music_volume", std::to_string(v));
        break;
      }
      case Row::kFrameRate: {
        // The launcher's lists: the Steam Deck's 30/40/60/90 (its panel's
        // rates), else the PC's; plus the current rate if it's another.
        std::vector<int> rates = OnSteamDeck() ? std::vector<int>{30, 40, 60, 90, 0}
                                               : std::vector<int>{30, 60, 90, 100, 120, 144, 0};
        const int current = REXCVAR_GET(framerate);
        if (std::find(rates.begin(), rates.end(), current) == rates.end()) {
          rates.insert(rates.end() - 1, current);
        }
        auto it = std::find(rates.begin(), rates.end(), current);
        int i = int(it - rates.begin()) + delta;
        i = std::clamp(i, 0, int(rates.size()) - 1);
        REXCVAR_SET(framerate, rates[i]);
        SaveSetting("framerate", std::to_string(rates[i]));
        break;
      }
      case Row::kAA: {
        // NVIDIA DLAA too where it can run: the single-pass frame, NVIDIA's
        // nvngx_dlss.dll next to the game or in vendor\dlss (fetched by the
        // launcher), and NGX not already failed (not an RTX GPU).
        static const auto dlss_state = RuntimeFunction<int32_t (*)()>("rexgpu-xenos.dll", "ReXDlssState");
        static const auto set_dlss = RuntimeFunction<void (*)(const char*)>("rexgpu-xenos.dll", "ReXSetDlssMode");
        std::error_code ec;
        const bool dlaa_possible =
            set_dlss && dlss_state && dlss_state() != 2 && Flag("gpu_tile_collapse") == "true" &&
            (std::filesystem::exists(rex::filesystem::GetExecutableFolder() / "nvngx_dlss.dll", ec) ||
             std::filesystem::exists(rex::filesystem::GetExecutableFolder() / "vendor" / "dlss" / "nvngx_dlss.dll", ec));
        static const char* effects[] = {"none", "fxaa", "smaa", "dlaa"};
        const int count = dlaa_possible ? 4 : 3;
        int i = 0;
        const std::string current = Flag("dlss_mode") == "dlaa" ? "dlaa" : Flag("swap_post_effect");
        while (i < count && current != effects[i]) ++i;
        i = ((i % count) + delta + count) % count;
        const char* effect = i == 3 ? "none" : effects[i];
        static const auto set_effect =
            RuntimeFunction<void (*)(const char*)>("rexgpu-xenos.dll", "ReXSetSwapPostEffect");
        if (set_effect) set_effect(effect);
        if (set_dlss) set_dlss(i == 3 ? "dlaa" : "off");
        // The GPU plugin keeps its own copies; these are what Value() reads.
        rex::cvar::SetFlagByName("swap_post_effect", effect);
        rex::cvar::SetFlagByName("dlss_mode", i == 3 ? "dlaa" : "off");
        SaveSetting("aa", effects[i]);
        break;
      }
      case Row::kUiSize: {
        // 70 to 100 % in tens, wrapping (smaller only: larger pushes the
        // title and menus off the screen); saved for the next start (the
        // launcher passes it on as --ui_size).
        static const int sizes[] = {70, 80, 90, 100};
        constexpr int n = int(std::size(sizes));
        int i = 0;
        while (i < n - 1 && sizes[i] < UiSize()) ++i;
        i = (i + delta + n) % n;
        UiSize() = sizes[i];
        REXLOG_INFO("Amped3: UI size {}% (from the next start)", UiSize());
        SaveSetting("ui_size", std::to_string(UiSize()));
        break;
      }
      case Row::kHud:
        rex::cvar::SetFlagByName("amped3_hide_hud", Flag("amped3_hide_hud") == "true" ? "false" : "true");
        break;
      case Row::kFog:
      case Row::kSnow:
      case Row::kSpray: {
        const char* name = row == Row::kFog ? "screen_fog" : row == Row::kSnow ? "falling_snow" : "board_spray";
        const std::string value = Flag(name) == "false" ? "true" : "false";
        rex::cvar::SetFlagByName(name, value);
        SaveSetting(name, value);
        break;
      }
      default: break;
    }
  }

  static std::string Value(Row row) {
    switch (row) {
      case Row::kMusic: {
        const std::string m = Flag("user_music");
        return m == "on"       ? "Your music"
               : m == "mix"    ? "Yours and game's, in turn"
               : m == "system" ? "Spotify / other player"
                               : "Game soundtrack";
      }
      case Row::kVolume: return Flag("user_music_volume") + "%";
      case Row::kFrameRate: {
        const int f = REXCVAR_GET(framerate);
        return f ? std::to_string(f) + " FPS" : "Unlimited";
      }
      case Row::kAA: {
        if (Flag("dlss_mode") == "dlaa") return "DLAA";
        const std::string e = Flag("swap_post_effect");
        return e == "smaa" ? "SMAA" : e == "fxaa" ? "FXAA" : e == "fxaa_extreme" ? "FXAA Extreme" : "Off";
      }
      case Row::kUiSize:
        return std::to_string(UiSize()) + "%" + (UiSize() != StartedUiSize() ? " (next start)" : "");
      case Row::kHud: return Flag("amped3_hide_hud") == "true" ? "Hidden" : "Shown";
      case Row::kFog: return Flag("screen_fog") == "false" ? "Off" : "On";
      case Row::kSnow: return Flag("falling_snow") == "false" ? "Off" : "On";
      case Row::kSpray: return Flag("board_spray") == "false" ? "Off" : "On";
      default: return "";
    }
  }

  static const char* Label(Row row) {
    switch (row) {
      case Row::kResume: return Flag("native_menu") == "true" ? "Back" : "Resume";  // back to the game's page
      case Row::kMusic: return "Music";
      case Row::kVolume: return "Music volume";
      case Row::kFog: return "Distance fog";
      case Row::kSnow: return "Falling snow";
      case Row::kSpray: return "Board spray";
      case Row::kNext: return "Next track";
      case Row::kPrevious: return "Previous track";
      case Row::kFrameRate: return "Frame rate";
      case Row::kAA: return "Anti-aliasing";
      case Row::kUiSize: return "UI size";
      case Row::kHud: return "HUD (for screenshots)";
      case Row::kAchievements: return "Achievements";
      case Row::kBackups: return "Save backups";
      case Row::kControls: return "Controls";
      case Row::kQuit: return "Quit ReAmped 3";
    }
    return "";
  }

  // The panel and its heading; returns the content's top-left corner.
  ImVec2 Panel(ImDrawList* draw, ImGuiIO& io, float scale, float w, float h, const char* heading) {
    const ImVec2 a((io.DisplaySize.x - w) * 0.5f, (io.DisplaySize.y - h) * 0.5f);
    const ImVec2 b(a.x + w, a.y + h);
    const float r = 18.0f * scale;
    draw->AddRectFilled(ImVec2(a.x + 10 * scale, a.y + 12 * scale), ImVec2(b.x + 10 * scale, b.y + 12 * scale),
                        Color(kPinkDark, 0.85f), r);
    draw->AddRectFilled(a, b, Color(kBlack, 0.97f), r);
    draw->AddRect(a, b, Color(kWhite), r, 0, 5.0f * scale);
    // Heading on a slanted pink bar breaking out of the top edge.
    ImFont* display = fonts::Find("ReAmped Display");
    const float hp = 46.0f * scale;
    const float hw = display->CalcTextSizeA(hp, FLT_MAX, 0, heading).x + 60 * scale;
    const ImVec2 ha(a.x + 34 * scale, a.y - 30 * scale), hb(ha.x + hw, ha.y + 62 * scale);
    Slant(draw, ImVec2(ha.x + 6 * scale, ha.y + 6 * scale), ImVec2(hb.x + 6 * scale, hb.y + 6 * scale), 12 * scale,
          Color(0x000000, 0.8f));
    Slant(draw, ha, hb, 12 * scale, Color(kPink));
    draw->AddText(display, hp, ImVec2(ha.x + 28 * scale, ha.y + 6 * scale), Color(kWhite), heading);
    Star(draw, ImVec2(hb.x + 26 * scale, ha.y + 10 * scale), 20 * scale, Color(kYellow), Color(kPink));
    Star(draw, ImVec2(b.x - 30 * scale, b.y - 30 * scale), 13 * scale, Color(kYellow), Color(kPink));
    return ImVec2(a.x + 44 * scale, a.y + 58 * scale);
  }

  void DrawMain(ImDrawList* draw, ImGuiIO& io, float scale) {
    const auto rows = Rows();
    const float row_h = 58.0f * scale, w = 760.0f * scale;
    const float h = 58.0f * scale + rows.size() * row_h + 110.0f * scale;
    const ImVec2 o = Panel(draw, io, scale, w, h, "REAMPED 3");
    ImFont* ui = fonts::Find("ReAmped UI");
    ImFont* bold = fonts::Find("ReAmped UI Bold");
    const float right = o.x + w - 88 * scale;
    for (size_t i = 0; i < rows.size(); ++i) {
      const float y = o.y + i * row_h;
      const bool on = int(i) == selected_;
      if (on) {
        Slant(draw, ImVec2(o.x - 14 * scale, y + 4 * scale), ImVec2(right + 26 * scale, y + row_h - 4 * scale),
              9 * scale, Color(kPink));
      }
      draw->AddText(bold, 32.0f * scale, ImVec2(o.x + 6 * scale, y + 10 * scale), Color(kWhite), Label(rows[i]));
      const std::string v = Value(rows[i]);
      if (!v.empty()) {
        const float vw = bold->CalcTextSizeA(30.0f * scale, FLT_MAX, 0, v.c_str()).x;
        const ImU32 vc = on ? Color(kWhite) : Color(kYellow);
        draw->AddText(bold, 30.0f * scale, ImVec2(right - vw - 24 * scale, y + 11 * scale), vc, v.c_str());
        // < > around a changeable value.
        draw->AddText(bold, 30.0f * scale, ImVec2(right - vw - 50 * scale, y + 11 * scale), vc, "<");
        draw->AddText(bold, 30.0f * scale, ImVec2(right - 6 * scale, y + 11 * scale), vc, ">");
      }
    }
    // Footer: what's playing (your music), and the buttons.
    float fy = o.y + rows.size() * row_h + 14 * scale;
    static const auto now_playing = RuntimeFunction<uint32_t (*)(char*, uint32_t)>("rexruntime.dll",
                                                                                   "ReXUserMusicNowPlaying");
    char track[256] = "";
    if (Flag("user_music") != "off" && now_playing) now_playing(track, sizeof(track));
    std::string footer = Flag("user_music") == "off" ? ""
                         : track[0]                    ? std::string("Now playing: ") + track
                         : Flag("user_music") == "system"
                             ? "Nothing playing: start Spotify or another player"
                             : "No music yet: put mp3 / ogg / flac / wav files in the music folder";
    if (!footer.empty()) {
      draw->PushClipRect(ImVec2(o.x, fy), ImVec2(o.x + w - 80 * scale, fy + 40 * scale), true);
      draw->AddText(ui, 22.0f * scale, ImVec2(o.x + 6 * scale, fy), Color(kGrey), footer.c_str());
      draw->PopClipRect();
    }
    fy += 34 * scale;
    draw->AddText(ui, 22.0f * scale, ImVec2(o.x + 6 * scale, fy), Color(kGrey),
                  "A  select     < >  change     B  back / close");
  }

  void DrawAchievements(ImDrawList* draw, ImGuiIO& io, float scale) {
    auto* kernel_state = REX_KERNEL_STATE();
    std::vector<rex::system::AchievementInfo> list;
    if (kernel_state) list = kernel_state->loaded_achievements();
    int unlocked = 0, score = 0, total = 0;
    for (const auto& a : list) {
      total += a.gamerscore;
      if (kernel_state->IsAchievementUnlocked(a.id)) ++unlocked, score += a.gamerscore;
    }
    const float row_h = 74.0f * scale, w = 980.0f * scale;
    const int visible = 9;
    const float h = 58.0f * scale + visible * row_h + 80.0f * scale;
    const ImVec2 o = Panel(draw, io, scale, w, h, "ACHIEVEMENTS");
    ImFont* ui = fonts::Find("ReAmped UI");
    ImFont* bold = fonts::Find("ReAmped UI Bold");
    char summary[96];
    std::snprintf(summary, sizeof(summary), "%d of %zu unlocked     %d / %d G", unlocked, list.size(), score, total);
    draw->AddText(bold, 26.0f * scale, ImVec2(o.x + 6 * scale, o.y - 6 * scale), Color(kYellow), summary);
    scroll_ = std::clamp(scroll_, 0, std::max(0, int(list.size()) - visible));
    for (int i = 0; i < visible && scroll_ + i < int(list.size()); ++i) {
      const auto& a = list[scroll_ + i];
      const bool got = kernel_state->IsAchievementUnlocked(a.id);
      const float y = o.y + 34 * scale + i * row_h;
      Star(draw, ImVec2(o.x + 18 * scale, y + 26 * scale), 16 * scale, got ? Color(kYellow) : Color(0x333340),
           got ? Color(kPink) : Color(0x555566));
      draw->AddText(bold, 28.0f * scale, ImVec2(o.x + 48 * scale, y + 6 * scale), got ? Color(kWhite) : Color(kGrey),
                    a.label.c_str());
      const std::string desc = got || a.unachieved_description.empty() ? a.description : a.unachieved_description;
      draw->PushClipRect(ImVec2(o.x, y), ImVec2(o.x + w - (got ? 170 : 345) * scale, y + row_h), true);
      draw->AddText(ui, 21.0f * scale, ImVec2(o.x + 48 * scale, y + 40 * scale), Color(kGrey), desc.c_str());
      draw->PopClipRect();
      // Progress (achievement_progress.cpp): a thin bar and the count on the
      // right, where an unlocked one shows its date.
      float fraction;
      std::string progress;
      if (!got && achievement_progress::Progress(a.label, fraction, progress)) {
        const float bx0 = o.x + w - 330 * scale, bx1 = o.x + w - 110 * scale, by = y + 58 * scale;
        draw->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + 6 * scale), Color(0x333340), 3 * scale);
        const float fill = (bx1 - bx0) * fraction;  // none at all for 0 (not a rounded dot)
        if (fill >= 1.0f) {
          draw->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + fill, by + 6 * scale), Color(kPink),
                              std::min(3 * scale, fill / 2));
        }
        // The count between the score above and the bar.
        const float pw = ui->CalcTextSizeA(18.0f * scale, FLT_MAX, 0, progress.c_str()).x;
        draw->AddText(ui, 18.0f * scale, ImVec2(bx1 - pw, y + 36 * scale), Color(kGrey), progress.c_str());
      }
      if (got) {
        // FILETIME (100 ns since 1601) -> the local date it was earned.
        const uint64_t ft = kernel_state->GetAchievementUnlockTime(a.id);
        if (ft) {
          FILETIME utc{DWORD(ft), DWORD(ft >> 32)}, local;
          SYSTEMTIME st;
          if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &st)) {
            char when[32];
            std::snprintf(when, sizeof(when), "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
            const float ww = ui->CalcTextSizeA(20.0f * scale, FLT_MAX, 0, when).x;
            draw->AddText(ui, 20.0f * scale, ImVec2(o.x + w - 110 * scale - ww, y + 42 * scale), Color(kGrey), when);
          }
        }
      }
      char g[16];
      std::snprintf(g, sizeof(g), "%u G", a.gamerscore);
      const float gw = bold->CalcTextSizeA(26.0f * scale, FLT_MAX, 0, g).x;
      draw->AddText(bold, 26.0f * scale, ImVec2(o.x + w - 110 * scale - gw, y + 8 * scale),
                    got ? Color(kYellow) : Color(kGrey), g);
    }
    if (list.empty()) {
      draw->AddText(ui, 24.0f * scale, ImVec2(o.x + 6 * scale, o.y + 40 * scale), Color(kGrey),
                    "No achievement data found in this copy of the game.");
    }
    draw->AddText(ui, 22.0f * scale, ImVec2(o.x + 6 * scale, o.y + 40 * scale + visible * row_h), Color(kGrey),
                  "^ v  scroll     B  back");
  }

  // The backups and the scheduled one, read when the page opens or changes
  // (each read is folder I/O behind the backup thread's lock).
  void RefreshBackups() {
    backups_ = save_backup::List();
    scheduled_ = save_backup::ScheduledRestore();
  }

  void DrawBackups(ImDrawList* draw, ImGuiIO& io, float scale) {
    const auto& backups = backups_;
    const std::string& scheduled = scheduled_;
    const float row_h = 50.0f * scale, w = 900.0f * scale;
    const int visible = 10;
    const float h = 58.0f * scale + visible * row_h + 120.0f * scale;
    const ImVec2 o = Panel(draw, io, scale, w, h, "SAVE BACKUPS");
    ImFont* ui = fonts::Find("ReAmped UI");
    ImFont* bold = fonts::Find("ReAmped UI Bold");
    draw->AddText(ui, 22.0f * scale, ImVec2(o.x + 6 * scale, o.y - 6 * scale), Color(kGrey),
                  "Made automatically when your saves change (newest 10 kept).");
    if (backups.empty()) {
      draw->AddText(ui, 26.0f * scale, ImVec2(o.x + 6 * scale, o.y + 40 * scale), Color(kWhite),
                    "No backups yet.");
    }
    // Scrolls to keep the selection in view (there can be 11: the scheduled
    // one is never pruned).
    const int first = std::clamp(backup_sel_ - visible + 1, 0, std::max(0, int(backups.size()) - visible));
    for (int row = 0; row < visible && first + row < int(backups.size()); ++row) {
      const int i = first + row;
      const float y = o.y + 34 * scale + row * row_h;
      const bool on = i == backup_sel_;
      if (on) {
        Slant(draw, ImVec2(o.x - 14 * scale, y + 2 * scale), ImVec2(o.x + w - 62 * scale, y + row_h - 4 * scale),
              9 * scale, Color(kPink));
      }
      // 2026-10-30_221500 -> 2026-10-30  22:15:00
      std::string name = backups[i].filename().string(), label = name;
      if (name.size() == 17) {
        label = name.substr(0, 10) + "   " + name.substr(11, 2) + ":" + name.substr(13, 2) + ":" + name.substr(15, 2);
      }
      draw->AddText(bold, 28.0f * scale, ImVec2(o.x + 6 * scale, y + 8 * scale), Color(kWhite), label.c_str());
      if (name == scheduled) {
        draw->AddText(bold, 24.0f * scale, ImVec2(o.x + 420 * scale, y + 11 * scale),
                      on ? Color(kWhite) : Color(kYellow), "restores at next start");
      }
    }
    draw->AddText(ui, 22.0f * scale, ImVec2(o.x + 6 * scale, o.y + 40 * scale + visible * row_h), Color(kGrey),
                  "A  restore this one at the next start (again: cancel)     B  back");
  }

  void DrawControls(ImDrawList* draw, ImGuiIO& io, float scale) {
    static const char* lines[][2] = {
        {"Start  >  Options  >  ReAmped 3", "These settings"},
        {"Back + Start (hold)  /  Esc", "Quit ReAmped 3"},
        {"D-pad left / right", "Previous / next song (yours too)"},
        {"F8 / F9", "Previous / next track (your music)"},
        {"F10", "Frame-time graph"},
    };
    const float row_h = 54.0f * scale, w = 900.0f * scale;
    const int count = int(sizeof(lines) / sizeof(lines[0]));
    const float h = 58.0f * scale + count * row_h + 80.0f * scale;
    const ImVec2 o = Panel(draw, io, scale, w, h, "CONTROLS");
    ImFont* ui = fonts::Find("ReAmped UI");
    ImFont* bold = fonts::Find("ReAmped UI Bold");
    for (int i = 0; i < count; ++i) {
      const float y = o.y + i * row_h;
      draw->AddText(bold, 28.0f * scale, ImVec2(o.x + 6 * scale, y + 8 * scale), Color(kYellow), lines[i][0]);
      draw->AddText(ui, 26.0f * scale, ImVec2(o.x + 440 * scale, y + 10 * scale), Color(kWhite), lines[i][1]);
    }
    draw->AddText(ui, 22.0f * scale, ImVec2(o.x + 6 * scale, o.y + count * row_h + 20 * scale), Color(kGrey), "B  back");
  }

  Page page_ = Page::kMain;
  int selected_ = 0, scroll_ = 0, frames_ = 0, backup_sel_ = 0;
  std::vector<std::filesystem::path> backups_;
  std::string scheduled_;
  bool armed_ = false;
  uint16_t held_ = 0, repeat_dirs_ = 0;
  std::chrono::steady_clock::time_point next_repeat_{};
};

}  // namespace

void Initialize(rex::ui::WindowedAppContext& app_context, rex::ui::ImGuiDrawer* drawer) {
  g_app_context = &app_context;
  g_drawer = drawer;
}

void Open() {
  if (!g_drawer || g_open || g_held_after_close || quit_prompt::IsOpen()) return;
  if (rex::kernel::xam::xeXamIsUIActive()) return;  // a system dialog has the screen
  if (!fonts::Find("ReAmped Display") || !fonts::Find("ReAmped UI") || !fonts::Find("ReAmped UI Bold")) {
    REXLOG_WARN("Amped3: menu: the fonts folder is missing");
    return;
  }
  g_open = true;
  new MenuDialog(g_drawer);  // Deletes itself when closed.
}

namespace {
std::optional<Row> SettingRow(std::string_view name) {
  if (name == "music") return Row::kMusic;
  if (name == "fps") return Row::kFrameRate;
  if (name == "aa") return Row::kAA;
  if (name == "uisize") return Row::kUiSize;
  if (name == "hud") return Row::kHud;
  if (name == "fog") return Row::kFog;
  if (name == "snow") return Row::kSnow;
  if (name == "spray") return Row::kSpray;
  if (name == "volume") return Row::kVolume;
  return std::nullopt;
}
}  // namespace

void ChangeSetting(std::string_view name, int delta) {
  if (const auto row = SettingRow(name)) MenuDialog::Change(*row, delta);
}

std::string SettingValue(std::string_view name) {
  const auto row = SettingRow(name);
  return row ? MenuDialog::Value(*row) : std::string();
}

void RequestOpen() {
  if (g_app_context) g_app_context->CallInUIThread([] { Open(); });
}

bool IsOpen() { return g_open || g_held_after_close; }

bool IsShowing() { return g_open; }

void OnPadButtons(uint16_t buttons) {
  // Once ever (a marker in the user data folder): where the menu is.
  static uint32_t frames = 0;
  if (frames == 1800 && !REXCVAR_GET(menu_debug_page).empty()) RequestOpen();
  if (++frames == 600) {
    const std::string root_cvar = rex::cvar::GetFlagByName("user_data_root");
    const auto root = root_cvar.empty() ? rex::filesystem::GetUserFolder() / "amped3"
                                        : std::filesystem::path(root_cvar);
    std::error_code ec;
    if (!std::filesystem::exists(root / "menu_tip_shown", ec)) {
      std::filesystem::create_directories(root, ec);
      std::ofstream(root / "menu_tip_shown") << "1\n";
      now_playing::ShowTip("Start  >  Options  >  ReAmped 3", "Music, frame rate, graphics, achievements...");
    }
  }
  if (g_held_after_close && !g_open && !buttons) g_held_after_close = false;
  const bool chord = (buttons & (kBack | kLB)) == (kBack | kLB);
  if (chord && !g_chord_down && g_app_context) {
    if (g_open) {
      g_close_requested = true;
    }
    // Opening it is the game's own: Start > Options > ReAmped 3 (no shortcut
    // into an overlay over the game).
  }
  g_chord_down = chord;
}

}  // namespace amped3::game_menu
