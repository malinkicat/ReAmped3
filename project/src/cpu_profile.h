// cpu_profile.h - Debug sampling profiler for host threads

#pragma once

namespace amped3::cpu_profile {

// Samples all threads for `seconds`, then writes cpu_profile.csv next to the
// executable. Started by the scripted-input command "profile N".
void Start(int seconds);

}  // namespace amped3::cpu_profile
