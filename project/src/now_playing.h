// now_playing.h - "Now playing" popup for the player's own music
#pragma once

#include <cstdint>
#include <string>

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace amped3::now_playing {

// Call once the ImGui drawer exists.
void Initialize(rex::ui::WindowedAppContext& app_context, rex::ui::ImGuiDrawer* drawer);

// Once per game frame (any thread): shows the popup when a new track starts.
void OnFrame();

// A tip in the same popup (any thread); `label` goes on its tag ("TIP",
// "SAVED").
// A full-screen cover with `text` over everything until HideCover (fades out).
void ShowCover(const std::string& text);
void HideCover();

void ShowTip(const std::string& title, const std::string& subtitle, const std::string& label = "TIP",
             float seconds = 8.0f);

// The next (1) or previous (-1) track of the player's own music.
void Skip(int32_t delta);

}  // namespace amped3::now_playing
