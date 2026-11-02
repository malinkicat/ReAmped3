// title_logo.h - The title screen's logo as REAMPED3, from the player's own
// AmpedBoxLogo.xpr (Shell.ruirr)
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace amped3::title_logo {

// Bumped when the drawing changes: every copy made from it (the patched
// texture, the sharp pack's, the launcher's header, the boot screen) is remade.
constexpr int kLogoVersion = 2;

// Redraws the XPR2 texture in place; false if it isn't the expected DXT3
// texture (left as it was).
bool Patch(std::vector<uint8_t>& xpr);

// The sharp UI pack's version of the patched logo: the pack's enlargement of
// the original (replace_dir/<original key>.png) patched at that size, saved
// as replace_dir/<patched key>.png. False if the pack hasn't the original.
bool WriteSharp(const std::vector<uint8_t>& original_xpr, const std::filesystem::path& replace_dir);

// The patched logo as a picture of its own (the launcher's header): from the
// pack's enlargement when replace_dir has it, else from the texture itself.
// reamped false: the original logo itself (sharp when the pack has it).
bool WriteLogoPng(const std::vector<uint8_t>& original_xpr, const std::filesystem::path& replace_dir,
                  const std::filesystem::path& out_png, bool reamped = true);

}  // namespace amped3::title_logo
