// achievement_toast.h - "Achievement unlocked" in the game's style
#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct ImGuiIO;

namespace amped3::achievement_toast {

// The chime (unless --achievement_sound=false).
void PlayChime();

// Draws one toast, t seconds into its 4.5 s (ImGui frame, UI thread).
void Draw(ImGuiIO& io, const std::string& name, uint32_t gamerscore, float t);

// Progress towards an achievement (achievement_progress.cpp): a smaller,
// silent card with a bar, shown kProgressSeconds; queued after unlocks.
constexpr float kProgressSeconds = 3.0f;
void DrawProgress(ImGuiIO& io, const std::string& name, const std::string& detail, float fraction, float t);
// The toast queue registers itself here; PushProgress hands it a card.
void SetProgressSink(std::function<void(std::string, std::string, float)> sink);
void PushProgress(const std::string& name, const std::string& detail, float fraction);

}  // namespace amped3::achievement_toast
