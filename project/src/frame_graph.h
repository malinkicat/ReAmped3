// frame_graph.h - On-screen frame-time graph (F10 or --show_fps=true)
//
// Any ImGui dialog makes the SDK presenter repaint on every display refresh,
// which competes with GPU emulation, so the graph is a separate click-through
// window drawn with GDI (as in Table-Tennis-Rockstar-PC). It plots two series:
//   display: DXGI presents, i.e. when frames reach the screen (what you see)
//   game:    the game's VdSwap calls (CPU side; drifts by design at 30/60,
//            because the flip itself waits for vblank later on the GPU)

#pragma once

#include <cstdint>

namespace amped3::frame_graph {

// Hooks DXGI Present for the display series (call once the presenter exists).
void InstallPresentHook();

// Called once per game frame (VdSwap).
void RecordGameFrame();

// target_fps: the --framerate value, used for the graph's scale (0 = unlimited).
void Show(void* game_window, int32_t target_fps);
void Hide();
bool IsVisible();

}  // namespace amped3::frame_graph
