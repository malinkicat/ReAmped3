// radio_text.h - Your music's and the other player's tracks in the game's own
// radio popup (radio_text.cpp)
#pragma once

#include <string>

namespace amped3::radio_text {

// Shows the track in the game's radio popup (when it next updates: riding).
void ShowTrack(const std::string& artist, const std::string& title, const std::string& source);
// The radio popup can show it (on the mountain, and --game_radio_popup).
bool Available();

}  // namespace amped3::radio_text
