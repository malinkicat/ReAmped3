// music_hooks.cpp - The game's song change drives the player's own music
//
// sub_82490928(direction) is the game's "change song": the D-pad during a run,
// right = 0 (next), left = 2 (previous). While the player's own music plays
// (--user_music, the runtime's stand-in for the Xbox 360's custom
// soundtracks), it skips that music instead; the game's own soundtrack, turned
// down meanwhile, is left where it is. With --user_music=system it skips in
// the other player (media_session.cpp).

#include "media_session.h"
#include "now_playing.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

REXCVAR_DEFINE_BOOL(music_hook_log, false, "Audio", "Debug: log the game's song changes");

REX_EXTERN(__imp__sub_82490928);

namespace {

// rex/audio/user_music.h, looked up by name (the installed SDK's import
// library predates it).
template <typename T>
T RuntimeFunction(const char* name) {
  HMODULE runtime = GetModuleHandleA("rexruntime.dll");
  return runtime ? reinterpret_cast<T>(GetProcAddress(runtime, name)) : nullptr;
}

}  // namespace

bool Amped3_OurPageOpen();  // options_hooks.cpp

extern "C" REX_FUNC(sub_82490928) {
  if (Amped3_OurPageOpen()) return;  // left / right are the page's
  static const auto playing = RuntimeFunction<int32_t (*)()>("ReXUserMusicPlaying");
  const uint32_t direction = ctx.r3.u32;
  const bool user_music = playing && playing();
  if (REXCVAR_GET(music_hook_log)) {
    REXLOG_INFO("Amped3: song change (direction {}){}", direction, user_music ? ": your music" : "");
  }
  if (user_music) {
    amped3::now_playing::Skip(direction == 2 ? -1 : 1);
    return;
  }
  // Taking turns with another player: "next" during the game's song ends its
  // turn (the player plays on).
  if (direction == 0 && amped3::media_session::InGameTurn()) {
    amped3::media_session::EndGameTurn();
    return;
  }
  // --user_music=mix during the game's song: "next" goes on to the player's
  // next track (the game's song, skipped too, is turned down with it).
  static const auto game_turn = RuntimeFunction<int32_t (*)()>("ReXUserMusicGameTurn");
  static const auto end_game_turn = RuntimeFunction<void (*)()>("ReXUserMusicEndGameTurn");
  if (direction == 0 && game_turn && end_game_turn && game_turn()) end_game_turn();
  __imp__sub_82490928(ctx, base);
}
