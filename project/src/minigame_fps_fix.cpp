// minigame_fps_fix.cpp - Three spots where the game counts per frame instead
// of per second, so they change above 30 FPS. Found by Amped 3 Revamped
// (github.com/minty-hippo/amped3revamped, public domain, src/game_patches.cpp);
// these are the same fixes, with their measured constants.
//
// - Sled turn on the spot: the sled "wait" state update (sub_82259078, f1 =
//   dt) turns the sled by stick * -3 degrees a frame without reading dt (90
//   degrees a second at 30, 180 at 60). The angle is scaled by dt * 30.
// - Sled crash scoring: each ragdoll contact adds |projected velocity| to a
//   bone's sum (0x823AD818, f1 = the value) and every scoring interval the
//   biggest sum is held against the injury thresholds; at high frame rates
//   the physics reports fewer, softer contacts (a third of the total at 120).
//   Each contact is multiplied by (fps / 30)^0.7 (calibrated over ~700
//   scoring events so the share reaching each threshold matches 30 FPS).
// - "Press Back to reset your boarder": the tutor's stuck check (0x822136CC)
//   compares the rider (rider+64) with a point trailing it (rider+220) that
//   at 30 FPS follows 2.33% of the way a frame (~1.4 s behind); above 30 it
//   ends up a frame behind, so slow riding counts as stuck. Above 30 FPS the
//   check uses its own point, trailing by the same amount per second.

#include <rex/cvar.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>

REXCVAR_DEFINE_BOOL(fix_sled_turn_frame_rate, true, "Game",
                    "Sleds turn on the spot at the same speed at any frame rate (the game turns "
                    "them a fixed angle a frame: twice as fast at 60)");
REXCVAR_DEFINE_BOOL(fix_sled_crash_score_frame_rate, true, "Game",
                    "Sled crash scores the same at any frame rate (above 30 the physics reports "
                    "fewer, softer hits)");
REXCVAR_DEFINE_DOUBLE(sled_crash_score_compensation, 0.7, "Game",
                      "How much sled crash hits are scaled up above 30 FPS: (fps / 30) to this power")
    .range(0.0, 1.0);
REXCVAR_DEFINE_BOOL(fix_stuck_tip_frame_rate, true, "Game",
                    "\"Press Back to reset your boarder\" only when stuck, at any frame rate (above "
                    "30 slow riding counted as stuck)");

namespace {

uint8_t* GuestBase() {
  auto* kernel_state = REX_KERNEL_STATE();
  return kernel_state ? kernel_state->memory()->TranslateVirtual<uint8_t*>(0) : nullptr;
}

float LoadF32(const uint8_t* base, uint32_t address) {
  uint32_t raw;
  std::memcpy(&raw, base + address, 4);
  raw = _byteswap_ulong(raw);
  float value;
  std::memcpy(&value, &raw, 4);
  return value;
}

// The real time between presented frames, smoothed (the game's own dt is
// game time, and sled crash mode runs the game at 0.7 speed).
double g_frame_seconds = 1.0 / 30.0;

double g_sled_wait_dt = 0.0;

struct Trail {
  double ref[3] = {};
  bool valid = false;
  std::chrono::steady_clock::time_point last{};
};
std::unordered_map<uint32_t, Trail> g_trails;

}  // namespace

// From Amped3_OnFrame (frame_stats.cpp), once per presented frame.
void Amped3_MinigameFpsOnFrame() {
  static auto last = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(now - last).count();
  last = now;
  if (seconds > 0.0 && seconds < 0.25) g_frame_seconds += (seconds - g_frame_seconds) * 0.1;
}

// [[midasm_hook]] at 0x82259090 (the sled wait update after its prologue).
void Amped3_SledWaitDt(PPCRegister& f1) { g_sled_wait_dt = f1.f64; }

// [[midasm_hook]] at 0x82259164 (f1 = this frame's turn angle).
void Amped3_FixSledWaitTurn(PPCRegister& f1) {
  const double dt = g_sled_wait_dt;
  if (REXCVAR_GET(fix_sled_turn_frame_rate) && dt > 0.0 && dt < 0.25) f1.f64 *= dt * 30.0;
}

// [[midasm_hook]] at 0x823AD818 (f1 = a contact's hit value, being added).
void Amped3_FixSledCrashHit(PPCRegister& f1) {
  const double frame = g_frame_seconds;
  if (!REXCVAR_GET(fix_sled_crash_score_frame_rate) || !(frame > 0.0 && frame < 1.0 / 32.0)) return;
  const double strength = std::clamp(REXCVAR_GET(sled_crash_score_compensation), 0.0, 1.0);
  f1.f64 *= std::pow(1.0 / (frame * 30.0), strength);
}

// [[midasm_hook]] at 0x822136CC (f1 = dt, f6 = squared distance from the
// trailing point, r11 = the rider).
void Amped3_FixStuckTip(PPCRegister& f1, PPCRegister& f6, PPCRegister& r11) {
  const double dt = f1.f64;
  if (!REXCVAR_GET(fix_stuck_tip_frame_rate) || !(dt > 0.0 && dt < 1.0 / 32.0) || !r11.u32) return;
  const uint8_t* base = GuestBase();
  if (!base) return;
  float pos[3];
  for (int i = 0; i < 3; ++i) pos[i] = LoadF32(base, r11.u32 + 64 + i * 4);
  Trail& trail = g_trails[r11.u32];
  const auto now = std::chrono::steady_clock::now();
  if (!trail.valid || now - trail.last > std::chrono::milliseconds(500)) {
    for (int i = 0; i < 3; ++i) trail.ref[i] = LoadF32(base, r11.u32 + 220 + i * 4);
    trail.valid = true;
  }
  trail.last = now;
  // 2.33% of the way a 30 FPS frame, as a rate.
  const double follow = 1.0 - std::pow(1.0 - 0.0233, dt * 30.0);
  double d2 = 0;
  for (int i = 0; i < 3; ++i) {
    trail.ref[i] += (pos[i] - trail.ref[i]) * follow;
    const double d = pos[i] - trail.ref[i];
    d2 += d * d;
  }
  f6.f64 = d2;
}
