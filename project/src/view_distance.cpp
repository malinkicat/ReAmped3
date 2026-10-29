// view_distance.cpp - Camera far clip from the game's camera data
//
// sub_821CD980 reads a camera node's AspectRatio, FieldOfView, NearClip and
// FarClip (floats at camera+36/40/44/48). --far_clip_scale (default 1 = as
// the game ships) scales FarClip after it is read; < 1 draws less in the
// distance (experiment for the Steam Deck, where the command processor's cost
// follows the number of draws). --camera_log logs every camera read.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include <bit>
#include <cstring>

REXCVAR_DEFINE_DOUBLE(far_clip_scale, 1.0, "Video",
                      "Scale the far clip of the game's cameras (1 = original; < 1 draws less in "
                      "the distance)");
REXCVAR_DEFINE_BOOL(camera_log, false, "Debug", "Log every camera the game reads from its data");

namespace {

float LoadBEFloat(const uint8_t* p) {
  uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  return std::bit_cast<float>(v);
}

void StoreBEFloat(uint8_t* p, float f) {
  uint32_t v = std::bit_cast<uint32_t>(f);
  p[0] = uint8_t(v >> 24);
  p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);
  p[3] = uint8_t(v);
}

}  // namespace

// [[midasm_hook]] at 0x821CDA28, right after `stfs f1,48(r31)` stored FarClip.
void Amped3_CameraFarClipRead(PPCRegister& r31) {
  auto* camera = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(r31.u32);
  const double scale = REXCVAR_GET(far_clip_scale);
  if (REXCVAR_GET(camera_log)) {
    REXLOG_INFO("Amped3: camera {:08X}: aspect {} fov {} near {} far {} (scale {})", r31.u32,
                LoadBEFloat(camera + 36), LoadBEFloat(camera + 40), LoadBEFloat(camera + 44),
                LoadBEFloat(camera + 48), scale);
  }
  if (scale > 0.0 && scale != 1.0) {
    StoreBEFloat(camera + 48, float(LoadBEFloat(camera + 48) * scale));
  }
}
