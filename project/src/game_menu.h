// game_menu.h - The ReAmped 3 overlay menu (the game's Options > ReAmped 3 >
// More options)
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace amped3::game_menu {

// Call once the ImGui drawer exists.
void Initialize(rex::ui::WindowedAppContext& app_context, rex::ui::ImGuiDrawer* drawer);

// Opens the menu (UI thread).
void Open();

// Opens the menu from any thread (e.g. the game's own Options screen).
void RequestOpen();

// Settings shared with the game-styled ReAmped 3 scene (options_hooks.cpp):
// "music", "fps", "aa", "hud", "fog", "snow", "spray", "volume"; delta +1 / -1 steps through the values.
void ChangeSetting(std::string_view name, int delta);
std::string SettingValue(std::string_view name);

// Whether it's open (game input is paused meanwhile, and until the buttons
// that closed it are up).
bool IsOpen();

// Whether it's on screen.
bool IsShowing();

// The controller's buttons once per game frame: Back + LB closes the menu.
void OnPadButtons(uint16_t buttons);

}  // namespace amped3::game_menu
