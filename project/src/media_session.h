// media_session.h - Another music player (e.g. Spotify) in place of the
// game's soundtrack (--user_music=system)
#pragma once

#include <string>

#include <cstdint>

namespace amped3::media_session {

// Once per game frame: starts watching the system's media players the first
// time --user_music is "system".
void OnFrame();
void OnQuit();  // the player stops with the game

// Next (1) or previous (-1) track in that player.
void Skip(int32_t delta);

// The player is the music source ("system", or mix mode with an empty music
// folder: it takes turns with the game's songs).
bool Active();
// Mix mode: the game's song plays now; "next" ends its turn.
bool InGameTurn();
void EndGameTurn();

// The followed player for the radio popup: "Spotify", else "Music player".
std::string PlayerName();

}  // namespace amped3::media_session
