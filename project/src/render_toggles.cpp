// render_toggles.cpp - Graphics toggles: the game's screen fog, falling snow,
// board spray and lens flares
//
// The scene renderer draws named render buckets through sub_82185D38(name);
// skipping a bucket's call skips that effect (private_work notes: there's no
// bloom, motion blur or depth of field drawn in Amped 3).

#include <rex/cvar.h>
#include <rex/hook.h>

#include <rex/logging.h>

#include <cstring>
#include <mutex>
#include <set>
#include <string>

REXCVAR_DEFINE_BOOL(screen_fog, true, "Graphics", "The game's distance fog");
REXCVAR_DEFINE_BOOL(falling_snow, true, "Graphics", "Falling snow");
REXCVAR_DEFINE_BOOL(board_spray, true, "Graphics", "Snow spray from boards");
REXCVAR_DEFINE_BOOL(lens_flares, true, "Graphics", "Sun lens flares");

REXCVAR_DEFINE_BOOL(render_bucket_log, false, "Debug", "Debug: log each render bucket name once");

REX_EXTERN(__imp__sub_82185D38);

extern "C" REX_FUNC(sub_82185D38) {
  if (const uint32_t name_address = ctx.r3.u32) {
    const char* name = reinterpret_cast<const char*>(base + name_address);
    if (REXCVAR_GET(render_bucket_log)) {
      static std::mutex mutex;
      static std::set<std::string> seen;
      std::lock_guard lock(mutex);
      if (seen.insert(name).second) REXLOG_INFO("Amped3: render bucket {}", name);
    }
    if ((!REXCVAR_GET(screen_fog) && std::strcmp(name, "ScreenFog") == 0) ||
        (!REXCVAR_GET(falling_snow) && std::strcmp(name, "FallingSnow") == 0) ||
        (!REXCVAR_GET(board_spray) && std::strcmp(name, "BoarderSpray") == 0) ||
        (!REXCVAR_GET(lens_flares) && std::strcmp(name, "LensFlares") == 0)) {
      return;
    }
  }
  __imp__sub_82185D38(ctx, base);
}
