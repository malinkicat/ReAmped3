// achievement_progress.h - Progress towards achievements from the game's statistics
#pragma once

#include <cstdint>
#include <string>

namespace amped3::achievement_progress {

// A statistic's raw 32-bit value (the "Your Statistics" screen's numbering);
// false when there's no profile loaded.
bool ReadStat(uint32_t index, uint32_t& raw);

// The progress of a tracked achievement (by its name in the game's list):
// 0-1 and a "137 / 200 objects" text; false if it has none.
bool Progress(const std::string& name, float& fraction, std::string& detail);

// Once per game frame.
void OnFrame();

}  // namespace amped3::achievement_progress
