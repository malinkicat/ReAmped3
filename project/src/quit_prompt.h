// quit_prompt.h - "Quit ReAmped 3?" (Escape, or hold Back + Start)
#pragma once

#include <cstdint>
#include <functional>

namespace rex::ui {
class ImGuiDrawer;
class WindowedAppContext;
}  // namespace rex::ui

namespace amped3::quit_prompt {

// UI thread, once the ImGui drawer exists. `quit` closes the game.
void Initialize(rex::ui::WindowedAppContext& app_context, rex::ui::ImGuiDrawer* drawer,
                std::function<void()> quit);
// UI thread: shows the prompt, unless it's open or a system dialog (e.g. the
// name prompt) is.
void Open();
// Any thread: Open on the UI thread.
void OpenFromAnyThread();
// Any thread: quits now, no prompt (the menu's Quit row).
void QuitFromAnyThread();
// Any thread: game input stays paused while it's open.
bool IsOpen();
// Any thread, every game frame: XInput buttons of all pads. Back + Start held
// for 1 s opens the prompt (once per hold).
void OnPadButtons(uint16_t buttons, double now_seconds);
// The XInput buttons of all connected pads, ORed.
uint16_t PadButtons();

}  // namespace amped3::quit_prompt
