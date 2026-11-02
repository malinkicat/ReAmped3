// boot_screen.h - The start-up logo and warning screens as one loading screen
#pragma once

#include <string>

namespace amped3::boot_screen {

// Before the GPU plugin loads: draws the loading screen (once per version)
// into the sharp UI pack's folder, or a folder of its own without one.
// Returns the texture folder to use (the pack's, ours, or the pack's
// unchanged when --boot_screen is off or it can't be made).
std::string Prepare(const std::string& pack_folder);

// True when the folder is our own (no sharp UI pack): replacement goes off
// after the boot screen.
bool OwnFolder();

}  // namespace amped3::boot_screen
