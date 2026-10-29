// rider_probe.cpp - Debug probe: each rider's position and speed
//
// With --rider_probe=true, the per-rider update (sub_82220150, r3 = rider,
// f1 = time step) logs once a second, per rider: updates/s, the summed time
// steps, distance moved and speed, using the position vector at rider+2176.
// Used to compare the crew's speed at 30, 60 and unlimited FPS.
// With --rider_dump=N, the scripted "riderdump" command writes N consecutive
// frames of every rider object (3 KB) and its actor object ([rider+1500]+20,
// 4 KB) to rider_dump.bin, to find state that advances per frame instead of
// per second (tools/rider_dump.py compares dumps at two frame rates).
// Hook is a [[midasm_hook]] on the update's entry.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <rex/filesystem.h>
#include <cmath>
#include <map>
#include <mutex>
#include <string>

REXCVAR_DECLARE(bool, rider_probe);
REXCVAR_DECLARE(int32_t, rider_dump);
REXCVAR_DECLARE(bool, rider_timers);
REXCVAR_DEFINE_INT32(ai_physics_hz, 0, "Debug", "Update AI riders at this rate with the accumulated time step (0 = every frame)");

namespace {

struct Rider {
  float start[3] = {}, last[3] = {};
  double distance = 0.0, steps = 0.0;
  uint64_t updates = 0;
};

std::mutex g_mutex;
std::atomic<bool> g_dump_requested{false};
std::map<uint32_t, Rider> g_riders;
std::chrono::steady_clock::time_point g_window_start{};

float LoadBEFloat(const uint8_t* p) {
  const uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  float f;
  std::memcpy(&f, &v, sizeof(f));
  return f;
}

uint32_t LoadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// rider_dump.bin records: u32 frame, u32 rider, f32 step, then 3072 bytes of the
// rider and 4096 of its actor (zeros when absent), all as in guest memory.
void Dump(uint32_t rider, double step) {
  static std::FILE* file = nullptr;
  static uint32_t frame = 0, last_rider = 0, frames_done = 0;
  static bool done = false;
  if (done || (!file && !g_dump_requested.load(std::memory_order_relaxed))) return;
  if (!file) {
    file = _wfopen((rex::filesystem::GetExecutableFolder() / "rider_dump.bin").c_str(), L"wb");
    if (!file) { done = true; return; }
    last_rider = rider;
  }
  if (rider == last_rider && frames_done++ > 0) ++frame;  // first rider seen marks a new frame
  if (frame >= uint32_t(REXCVAR_GET(rider_dump))) {
    std::fclose(file);
    done = true;
    REXLOG_INFO("RiderProbe: wrote rider_dump.bin");
    return;
  }
  auto* memory = REX_KERNEL_MEMORY();
  const auto* r = memory->TranslateVirtual<const uint8_t*>(rider);
  static uint8_t actor_bytes[4096];
  std::memset(actor_bytes, 0, sizeof(actor_bytes));
  const uint32_t holder = LoadBE32(r + 1500);
  if (holder) {
    const uint32_t actor = LoadBE32(memory->TranslateVirtual<const uint8_t*>(holder + 20));
    if (actor) std::memcpy(actor_bytes, memory->TranslateVirtual<const uint8_t*>(actor), 4096);
  }
  const float step_f = float(step);
  std::fwrite(&frame, 4, 1, file);
  std::fwrite(&rider, 4, 1, file);
  std::fwrite(&step_f, 4, 1, file);
  std::fwrite(r, 1, 3072, file);
  std::fwrite(actor_bytes, 1, 4096, file);
}

}  // namespace

namespace amped3::rider_probe {
void RequestDump() {
  g_dump_requested = true;
  REXLOG_INFO("RiderProbe: dump requested");
}
}  // namespace amped3::rider_probe

namespace {
// Actor timers that advance by the time step (found with tools/rider_dump.py).
// With --rider_timers, each reset (value drops) is logged with the time the
// timer reached: how long each rider state lasted, to compare frame rates.
constexpr uint32_t kActorTimers[] = {260, 1068, 1612, 1632, 2180, 2212, 2216, 2220,
                                     2244, 2252, 2256, 2260, 2272, 2316, 2324};
std::map<uint32_t, std::array<float, std::size(kActorTimers)>> g_timers;

void TrackTimers(uint32_t rider) {
  auto* memory = REX_KERNEL_MEMORY();
  const auto* r = memory->TranslateVirtual<const uint8_t*>(rider);
  const uint32_t holder = LoadBE32(r + 1500);
  if (!holder) return;
  const uint32_t actor = LoadBE32(memory->TranslateVirtual<const uint8_t*>(holder + 20));
  if (!actor) return;
  const auto* a = memory->TranslateVirtual<const uint8_t*>(actor);
  std::lock_guard lock(g_mutex);
  auto [it, inserted] = g_timers.try_emplace(rider);
  for (size_t i = 0; i < std::size(kActorTimers); ++i) {
    const float v = LoadBEFloat(a + kActorTimers[i]);
    const float last = it->second[i];
    if (!inserted && v < last - 0.001f && last > 0.0f && last < 1000.0f) {
      REXLOG_INFO("RiderTimer: {:08X} actor+{} reset after {:.3f}s", rider, kActorTimers[i], last);
    }
    it->second[i] = v;
  }
}
}  // namespace

namespace amped3::watch_probe {
void OnRiderUpdate(uint32_t rider);  // watch_probe.cpp
}

namespace {
// --ai_physics_hz: AI riders (those using the AI re-plan timer at actor+2212;
// the player's stays 0) are updated at that rate, with the accumulated time
// as the step, instead of every frame - to test whether their behaviour above
// 30 FPS comes from the size of the time step.
struct AiStep {
  double accumulated = 0.0;
  bool is_ai = false;
};
std::map<uint32_t, AiStep> g_ai_steps;

bool SkipAiUpdate(uint32_t rider, PPCRegister& f1) {
  const int32_t hz = REXCVAR_GET(ai_physics_hz);
  if (hz <= 0) {
    return false;
  }
  AiStep& state = g_ai_steps[rider];
  if (!state.is_ai) {
    auto* memory = REX_KERNEL_MEMORY();
    const uint32_t holder = LoadBE32(memory->TranslateVirtual<const uint8_t*>(rider + 1500));
    if (holder) {
      const uint32_t actor = LoadBE32(memory->TranslateVirtual<const uint8_t*>(holder + 20));
      state.is_ai =
          actor && LoadBE32(memory->TranslateVirtual<const uint8_t*>(actor + 2212)) != 0;
    }
    if (!state.is_ai) {
      return false;
    }
  }
  state.accumulated += f1.f64;
  if (state.accumulated < 0.999 / hz) {
    return true;
  }
  f1.f64 = state.accumulated;
  state.accumulated = 0.0;
  return false;
}
}  // namespace

// [[midasm_hook]] at 0x82220150, the per-rider update's entry. Returns true to
// skip the update (--ai_physics_hz).
bool Amped3_RiderUpdate(PPCRegister& r3, PPCRegister& f1) {
  amped3::watch_probe::OnRiderUpdate(r3.u32);
  if (SkipAiUpdate(r3.u32, f1)) {
    return true;
  }
  if (REXCVAR_GET(rider_timers)) {
    TrackTimers(r3.u32);
  }
  if (REXCVAR_GET(rider_dump) > 0) {
    Dump(r3.u32, f1.f64);
  }
  if (!REXCVAR_GET(rider_probe)) {
    return false;
  }
  const auto* p = REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(r3.u32 + 2176);
  const float pos[3] = {LoadBEFloat(p), LoadBEFloat(p + 4), LoadBEFloat(p + 8)};
  std::lock_guard lock(g_mutex);
  auto [it, inserted] = g_riders.try_emplace(r3.u32);
  auto& r = it->second;
  if (r.updates++ == 0) {
    std::memcpy(r.start, pos, sizeof(pos));
  } else {
    const float dx = pos[0] - r.last[0], dy = pos[1] - r.last[1], dz = pos[2] - r.last[2];
    r.distance += std::sqrt(dx * dx + dy * dy + dz * dz);
  }
  std::memcpy(r.last, pos, sizeof(pos));
  r.steps += f1.f64;
  const auto now = std::chrono::steady_clock::now();
  if (g_window_start == std::chrono::steady_clock::time_point{}) {
    g_window_start = now;
  }
  const double window = std::chrono::duration<double>(now - g_window_start).count();
  if (window < 1.0) {
    return false;
  }
  std::string out;
  for (const auto& [rider, v] : g_riders) {
    out += fmt::format(" [{:08X} {:.0f}/s steps={:.3f}s dist={:.1f} speed={:.1f}/s pos=({:.0f},{:.0f},{:.0f})]",
                       rider, v.updates / window, v.steps, v.distance,
                       v.steps > 0 ? v.distance / v.steps : 0.0, v.last[0], v.last[1], v.last[2]);
  }
  REXLOG_INFO("RiderProbe:{}", out);
  g_riders.clear();
  g_window_start = now;
  return false;
}

// --rider_heading_probe: sub_82398FE0 (an AI rider's re-plan check) at
// 0x82399108, where f0 is the dot product of the rider's current horizontal
// direction with the one saved at the last re-plan (r31 = the AI state).
// Logs calls per second and the spread of the dot product per AI state, every
// two seconds - jitter in the direction shows as dot products below ~1 that
// come and go from frame to frame.
REXCVAR_DEFINE_BOOL(rider_heading_probe, false, "Debug",
                    "Log calls/s and dot-product spread of the AI riders' re-plan check");

namespace {
struct HeadingStats {
  uint64_t calls = 0;
  double min_dot = 1.0, sum_drop = 0.0;
  float last_dot = 1.0f;
  double sum_jump = 0.0;
};
std::map<uint32_t, HeadingStats> g_heading;
std::chrono::steady_clock::time_point g_heading_start{};
}  // namespace

void Amped3_RiderHeading(PPCRegister& r31, PPCRegister& f0) {
  if (!REXCVAR_GET(rider_heading_probe)) {
    return;
  }
  std::lock_guard lock(g_mutex);
  HeadingStats& s = g_heading[r31.u32];
  const float dot = float(f0.f64);
  ++s.calls;
  s.min_dot = std::min(s.min_dot, double(dot));
  s.sum_drop += 1.0 - dot;
  s.sum_jump += std::fabs(dot - s.last_dot);
  s.last_dot = dot;
  const auto now = std::chrono::steady_clock::now();
  if (g_heading_start == std::chrono::steady_clock::time_point{}) g_heading_start = now;
  const double window = std::chrono::duration<double>(now - g_heading_start).count();
  if (window < 2.0) return;
  std::string out;
  for (const auto& [state, v] : g_heading) {
    out += fmt::format(" [{:08X} {:.0f}/s min={:.4f} mean_drop={:.5f} mean_jump={:.5f}]", state,
                       v.calls / window, v.min_dot, v.sum_drop / v.calls, v.sum_jump / v.calls);
  }
  REXLOG_INFO("RiderHeading:{}", out);
  g_heading.clear();
  g_heading_start = now;
}
