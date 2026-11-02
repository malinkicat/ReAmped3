// crash_contact.cpp - Riders' contact counters in time, not frames.
//
// The riders' physics counts frames of contact: rider+2416 (with a terrain
// feature, sub_82225CB0) and rider+2412 (with a moving object, sub_82227F88)
// go up by one a step and act past the tuning's NumFramesFeatureColToTriggerCrash
// (rider+1496) / NumFramesDynamicColToTriggerCrash (rider+1492); rider+2420
// counts steps without contact and clears +2416 after 4. Made for 30 steps a
// second: at 60 a contact half as long counts the same. With
// --crash_contact_time, each step adds its length in 1/30 s instead of one.
// --crash_probe logs the rider state changes (sub_82210148, state in r4) by
// caller every 5 s, to count wipeouts.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <string>

REXCVAR_DEFINE_BOOL(crash_contact_time, true, "Game",
                    "Riders' contact counters (crash checks) count time (1/30 s), not frames");
REXCVAR_DEFINE_BOOL(crash_probe, false, "Debug", "Log the riders' state changes (sub_82210148) by state and caller");

namespace {
thread_local float g_rider_step = 1.0f / 30.0f;

struct Fractions {
  float feature = 0.0f, dynamic = 0.0f, clear = 0.0f;
};
std::mutex g_mutex;
std::map<uint32_t, Fractions> g_fractions;

// Guest memory is big-endian.
uint32_t Load32(const uint8_t* base, uint32_t address) {
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + address));
}
void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  *reinterpret_cast<uint32_t*>(base + address) = __builtin_bswap32(value);
}

// The counter at rider+offset went from `before` to before + 1 in this step:
// make that the step's length in 1/30 s (whole ones; the rest carried).
void Scale(uint8_t* base, uint32_t rider, uint32_t offset, uint32_t before, float& fraction) {
  const uint32_t after = Load32(base, rider + offset);
  if (after != before + 1) {
    fraction = 0.0f;
    return;
  }
  fraction += std::fmin(std::fmax(g_rider_step * 30.0f, 0.0f), 4.0f);
  const float whole = std::floor(fraction + 0.001f);
  fraction -= whole;
  Store32(base, rider + offset, before + uint32_t(whole));
}

std::map<std::string, uint32_t> g_states;
std::chrono::steady_clock::time_point g_states_start{};
}  // namespace

// rider_probe.cpp's hook at the per-rider update's entry: this update's step.
void Amped3_SetRiderStep(double step) { g_rider_step = float(step); }

REX_EXTERN(__imp__sub_82225CB0);
extern "C" REX_FUNC(sub_82225CB0) {
  if (!REXCVAR_GET(crash_contact_time)) {
    __imp__sub_82225CB0(ctx, base);
    return;
  }
  const uint32_t rider = ctx.r3.u32;
  const uint32_t feature = Load32(base, rider + 2416), clear = Load32(base, rider + 2420);
  __imp__sub_82225CB0(ctx, base);
  std::lock_guard lock(g_mutex);
  Fractions& f = g_fractions[rider];
  Scale(base, rider, 2416, feature, f.feature);
  Scale(base, rider, 2420, clear, f.clear);
}

REX_EXTERN(__imp__sub_82227F88);
extern "C" REX_FUNC(sub_82227F88) {
  if (!REXCVAR_GET(crash_contact_time)) {
    __imp__sub_82227F88(ctx, base);
    return;
  }
  const uint32_t rider = ctx.r3.u32;
  const uint32_t dynamic = Load32(base, rider + 2412);
  __imp__sub_82227F88(ctx, base);
  std::lock_guard lock(g_mutex);
  Scale(base, rider, 2412, dynamic, g_fractions[rider].dynamic);
}

REX_EXTERN(__imp__sub_82210148);
extern "C" REX_FUNC(sub_82210148) {
  if (REXCVAR_GET(crash_probe)) {
    std::lock_guard lock(g_mutex);
    ++g_states[fmt::format("{}@{:08X}", ctx.r4.s32, uint32_t(ctx.lr))];
    const auto now = std::chrono::steady_clock::now();
    if (g_states_start == std::chrono::steady_clock::time_point{}) g_states_start = now;
    if (now - g_states_start >= std::chrono::seconds(5)) {
      std::string out;
      for (const auto& [key, count] : g_states) out += fmt::format(" {}:{}", key, count);
      REXLOG_INFO("crash_probe: rider states (state@caller:count) in 5 s:{}", out);
      g_states.clear();
      g_states_start = now;
    }
  }
  __imp__sub_82210148(ctx, base);
}
