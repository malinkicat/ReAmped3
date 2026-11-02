// ui_patch.h - ReAmped 3 in the game's own Options screen (patched UI packages)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace amped3::ui_patch {

// Before the runtime mounts the game data: writes (if needed) the patched UI
// packages and points --game_data_overlay at them.
void Prepare();

// The sharp UI texture pack's folder when it's next to the game and
// --sharp_ui is on, else empty (for the GPU plugin's texture_folder).
std::string SharpUiFolder();

// The game's own title logo texture (Shell.ruirr Textures/AmpedBoxLogo.xpr),
// empty if it can't be read.
std::vector<uint8_t> OriginalTitleLogo();

}  // namespace amped3::ui_patch
