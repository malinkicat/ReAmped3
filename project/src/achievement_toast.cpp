// achievement_toast.cpp - "Achievement unlocked" in the game's style
//
// Drawn for amped3_app.h's OnDemandToastDialog in place of the SDK's stock
// toast, in the game's own notification style (amped_style.cpp): the tip
// arrow from the left with a yellow star, the name and the gamerscore; with a
// short synthesized chime (--achievement_sound) through PlaySound, which Wine
// has too.

#include "achievement_toast.h"

#include "amped_style.h"

#include <rex/cvar.h>

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

REXCVAR_DEFINE_BOOL(achievement_sound, true, "UI", "Play a chime when an achievement unlocks");

namespace amped3::achievement_toast {
namespace {

// A rising three-note chime (E6, G6, C7), 16-bit mono WAV in memory.
const std::vector<uint8_t>& Chime() {
  static const std::vector<uint8_t> wav = [] {
    constexpr uint32_t rate = 44100;
    const float notes[] = {1318.5f, 1568.0f, 2093.0f};
    const float starts[] = {0.0f, 0.09f, 0.18f};
    const uint32_t samples = uint32_t(rate * 0.75f);
    std::vector<int16_t> pcm(samples);
    for (uint32_t i = 0; i < samples; ++i) {
      const float t = float(i) / rate;
      float v = 0.0f;
      for (int n = 0; n < 3; ++n) {
        const float u = t - starts[n];
        if (u < 0) continue;
        const float env = std::exp(-u * 6.0f) * std::min(1.0f, u * 400.0f);
        v += env * (std::sin(6.2831853f * notes[n] * u) + 0.3f * std::sin(6.2831853f * notes[n] * 2 * u));
      }
      pcm[i] = int16_t(std::clamp(v * 0.18f, -1.0f, 1.0f) * 32767);
    }
    std::vector<uint8_t> out(44 + pcm.size() * 2);
    auto put32 = [&](size_t at, uint32_t x) { std::memcpy(&out[at], &x, 4); };
    auto put16 = [&](size_t at, uint16_t x) { std::memcpy(&out[at], &x, 2); };
    std::memcpy(&out[0], "RIFF", 4);
    put32(4, uint32_t(out.size() - 8));
    std::memcpy(&out[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 1);
    put32(24, rate);
    put32(28, rate * 2);
    put16(32, 2);
    put16(34, 16);
    std::memcpy(&out[36], "data", 4);
    put32(40, uint32_t(pcm.size() * 2));
    std::memcpy(&out[44], pcm.data(), pcm.size() * 2);
    return out;
  }();
  return wav;
}

}  // namespace

void PlayChime() {
  if (!REXCVAR_GET(achievement_sound)) return;
  PlaySoundA(reinterpret_cast<LPCSTR>(Chime().data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void Draw(ImGuiIO& io, const std::string& name, uint32_t gamerscore, float t) {
  amped_style::Banner banner;
  banner.tag = "ACHIEVEMENT UNLOCKED";
  banner.title = name;
  banner.value = std::to_string(gamerscore) + "G";
  banner.icon = amped_style::Icon::kStar;
  const float shown = amped_style::Shown(t, 4.5f);
  amped_style::Draw(ImGui::GetForegroundDrawList(), io, 0.24f, banner, shown, std::min(1.0f, shown * 3));
}

namespace {
std::function<void(std::string, std::string, float)> g_progress_sink;
}  // namespace

void SetProgressSink(std::function<void(std::string, std::string, float)> sink) {
  g_progress_sink = std::move(sink);
}

void PushProgress(const std::string& name, const std::string& detail, float fraction) {
  if (g_progress_sink) g_progress_sink(name, detail, fraction);
}

void DrawProgress(ImGuiIO& io, const std::string& name, const std::string& detail, float fraction, float t) {
  amped_style::Banner banner;
  banner.tag = "ACHIEVEMENT PROGRESS";
  banner.title = name;
  banner.value = detail;
  banner.progress = fraction;
  banner.icon = amped_style::Icon::kStarOutline;
  const float shown = amped_style::Shown(t, kProgressSeconds);
  amped_style::Draw(ImGui::GetForegroundDrawList(), io, 0.24f, banner, shown, std::min(1.0f, shown * 3));
}

}  // namespace amped3::achievement_toast
