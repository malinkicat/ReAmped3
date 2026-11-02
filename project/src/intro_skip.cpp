// intro_skip.cpp - Start-up without the 2K and Indie Built logo movies
//
// sub_8215A718 (start-up) plays the movies one after the other: for each, the
// path is formatted (sub_822782C0), wrapped in a string (sub_82154820) and
// played to the end by sub_824548E8, then the string is freed (sub_821D4140).
// With --skip_intro (on by default) a hook at the first of them (0x8215A8B4,
// "2K Bumper for Amped 3 HD.bik"; then "4 second intro_FINAL.bik") jumps past
// both to 0x8215A92C, where the game goes on as after the second movie.

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(skip_intro, true, "Game", "Start without the 2K and Indie Built logo movies");

bool Amped3_SkipIntroMovies() {
  if (!REXCVAR_GET(skip_intro)) return false;
  REXLOG_INFO("Amped3: start-up logo movies skipped");
  return true;
}
