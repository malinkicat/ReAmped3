// now_playing.cpp - "Now playing" popup for the player's own music
//
// With --user_music, each new track shows its artist and title for a few
// seconds in the bottom left, in the quit prompt's style. The dialog is
// attached to the ImGui drawer only while it's on screen (an attached dialog
// makes the presenter repaint at the display's rate; see amped3_app.h).

#include "now_playing.h"

#include "amped_style.h"
#include "fonts.h"
#include "radio_text.h"

#include <cfloat>
#include "media_session.h"

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/windowed_app_context.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

REXCVAR_DECLARE(bool, game_radio_popup);  // radio_text.cpp

namespace amped3::now_playing {
namespace {

constexpr float kShowSeconds = 5.0f;

rex::ui::WindowedAppContext* g_app_context = nullptr;
rex::ui::ImGuiDrawer* g_drawer = nullptr;
uint32_t g_last_track = 0;

// The runtime's user music (rex/audio/user_music.h), looked up by name: the
// installed SDK's import library predates it.
using NowPlayingFn = uint32_t (*)(char*, uint32_t);
using SkipFn = void (*)(int32_t);
template <typename T>
T RuntimeFunction(const char* name) {
  HMODULE runtime = GetModuleHandleA("rexruntime.dll");
  return runtime ? reinterpret_cast<T>(GetProcAddress(runtime, name)) : nullptr;
}

ImU32 Color(uint32_t rgb, float alpha = 1.0f) {
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, uint8_t(255 * alpha));
}

class NowPlayingDialog : public rex::ui::ImGuiDialog {
 public:
  NowPlayingDialog(rex::ui::ImGuiDrawer* drawer, std::string artist, std::string title,
                   std::string label = "NOW PLAYING", float seconds = kShowSeconds)
      : rex::ui::ImGuiDialog(drawer),
        artist_(std::move(artist)),
        title_(std::move(title)),
        label_(std::move(label)),
        seconds_(seconds) {}

  static NowPlayingDialog* current;

  void Replace(std::string artist, std::string title, std::string label = "NOW PLAYING",
               float seconds = kShowSeconds) {
    artist_ = std::move(artist);
    title_ = std::move(title);
    label_ = std::move(label);
    seconds_ = seconds;
    start_ = std::chrono::steady_clock::now();
  }

 protected:
  void OnClose() override {
    if (current == this) current = nullptr;
  }

  void OnDraw(ImGuiIO& io) override {
    const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_).count();
    if (t > seconds_) {
      Close();
      return;
    }
    // The game's tip arrow (amped_style.cpp), low on the left, under its own
    // tips' place.
    amped_style::Banner banner;
    banner.tag = label_;
    banner.title = title_;
    banner.subtitle = artist_;
    banner.icon = label_ == "NOW PLAYING" || label_ == "SPOTIFY" || label_ == "MUSIC PLAYER" ? amped_style::Icon::kNote
                  : label_ == "SAVED"     ? amped_style::Icon::kSaved
                                          : amped_style::Icon::kNone;
    const float shown = amped_style::Shown(t, seconds_);
    amped_style::Draw(ImGui::GetForegroundDrawList(), io, 0.80f, banner, shown,
                      std::min(1.0f, shown * 3));
  }

 private:
  std::string artist_, title_, label_;
  float seconds_;
  std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

NowPlayingDialog* NowPlayingDialog::current = nullptr;

void Show(std::string text) {
  // "Artist - Title" (as UserMusicNowPlaying gives it), else just a title.
  std::string artist, title = text;
  if (auto dash = text.find(" - "); dash != std::string::npos) {
    artist = text.substr(0, dash);
    title = text.substr(dash + 3);
  }
  // The tag says where it comes from: SPOTIFY (or MUSIC PLAYER), else ours.
  std::string label = "NOW PLAYING";
  if (media_session::Active()) {
    label = media_session::PlayerName();
    for (char& c : label) c = char(std::toupper(static_cast<unsigned char>(c)));
  }
  if (NowPlayingDialog::current) {
    NowPlayingDialog::current->Replace(std::move(artist), std::move(title), label);
  } else {
    // Deletes itself when closed.
    NowPlayingDialog::current = new NowPlayingDialog(g_drawer, std::move(artist), std::move(title), label);
  }
}

// A full-screen cover with a line of text (quick load: the menus it goes
// through stay out of sight).
class CoverDialog : public rex::ui::ImGuiDialog {
 public:
  CoverDialog(rex::ui::ImGuiDrawer* drawer, std::string text) : rex::ui::ImGuiDialog(drawer), text_(std::move(text)) {}
  static CoverDialog* current;
  void Hide() { hiding_ = std::chrono::steady_clock::now(), hide_ = true; }

 protected:
  void OnClose() override {
    if (current == this) current = nullptr;
  }
  void OnDraw(ImGuiIO& io) override {
    float alpha = 1.0f;
    if (hide_) {
      alpha = 1.0f - std::chrono::duration<float>(std::chrono::steady_clock::now() - hiding_).count() / 0.4f;
      if (alpha <= 0) {
        Close();
        return;
      }
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, Color(0x0B1220, alpha));
    ImFont* font = fonts::Find("ReAmped Display");
    if (!font) font = ImGui::GetFont();
    const float px = io.DisplaySize.y * 0.05f;
    const ImVec2 size = font->CalcTextSizeA(px, FLT_MAX, 0, text_.c_str());
    draw->AddText(font, px, ImVec2((io.DisplaySize.x - size.x) / 2, (io.DisplaySize.y - size.y) / 2),
                  Color(0xFFFFFF, alpha), text_.c_str());
  }

 private:
  std::string text_;
  bool hide_ = false;
  std::chrono::steady_clock::time_point hiding_;
};
CoverDialog* CoverDialog::current = nullptr;

}  // namespace

void ShowCover(const std::string& text) {
  if (!g_app_context || !g_drawer) return;
  g_app_context->CallInUIThread([text] {
    if (!CoverDialog::current) CoverDialog::current = new CoverDialog(g_drawer, text);
  });
}

void HideCover() {
  if (!g_app_context || !g_drawer) return;
  g_app_context->CallInUIThread([] {
    if (CoverDialog::current) CoverDialog::current->Hide();
  });
}

void ShowTip(const std::string& title, const std::string& subtitle, const std::string& label, float seconds) {
  if (!g_app_context || !g_drawer) return;
  g_app_context->CallInUIThread([title, subtitle, label, seconds] {
    if (NowPlayingDialog::current) {
      NowPlayingDialog::current->Replace(subtitle, title, label, seconds);
    } else {
      NowPlayingDialog::current = new NowPlayingDialog(g_drawer, subtitle, title, label, seconds);
    }
  });
}

void Initialize(rex::ui::WindowedAppContext& app_context, rex::ui::ImGuiDrawer* drawer) {
  g_app_context = &app_context;
  g_drawer = drawer;
}

void OnFrame() {
  if (!g_app_context || !g_drawer) return;
  static const NowPlayingFn now_playing = RuntimeFunction<NowPlayingFn>("ReXUserMusicNowPlaying");
  if (!now_playing) return;
  char buffer[512];
  const uint32_t track = now_playing(buffer, sizeof(buffer));
  static std::string pending;  // a track not shown yet (off the mountain)
  if (buffer[0] && track != g_last_track) {
    g_last_track = track;
    pending = buffer;
  }
  if (pending.empty()) return;
  // Only in the game's own radio popup, while riding: off the mountain (the
  // first load, loading screens, menus) the track waits for the next ride
  // (a newer one replaces it). Our banner only with --game_radio_popup=false.
  if (!REXCVAR_GET(game_radio_popup)) {
    g_app_context->CallInUIThread([text = std::move(pending)] { Show(text); });
    pending.clear();
    return;
  }
  if (radio_text::Available()) {
    std::string text = std::move(pending);
    pending.clear();
    std::string artist, title = text;
    if (auto dash = text.find(" - "); dash != std::string::npos) {
      artist = text.substr(0, dash);
      title = text.substr(dash + 3);
    }
    const std::string mode = rex::cvar::GetFlagByName("user_music");
    radio_text::ShowTrack(artist, title, mode == "system" ? media_session::PlayerName() : "Your music");
  }
}

}  // namespace amped3::now_playing
void Amped3_RequestGameSongChange(int delta);  // save_hooks.cpp
namespace amped3::now_playing {

void Skip(int32_t delta) {
  // Another player (--user_music=system), else the runtime's own; the game's
  // soundtrack (or its turn in mix mode) changes the game's song.
  const std::string mode = rex::cvar::GetFlagByName("user_music");
  if (media_session::Active()) {
    // Taking turns: during the game's song, next goes back to the player.
    if (media_session::InGameTurn()) {
      if (delta > 0) media_session::EndGameTurn();
      else Amped3_RequestGameSongChange(delta);
      return;
    }
    media_session::Skip(delta);
    return;
  }
  static const auto playing = RuntimeFunction<int32_t (*)()>("ReXUserMusicPlaying");
  if (mode == "off" || !(playing && playing())) {
    Amped3_RequestGameSongChange(delta);
    return;
  }
  static const SkipFn skip = RuntimeFunction<SkipFn>("ReXUserMusicSkip");
  if (skip) skip(delta);
}

}  // namespace amped3::now_playing
