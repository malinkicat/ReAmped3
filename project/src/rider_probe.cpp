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

// [[midasm_hook]] at 0x82220150, the per-rider update's entry.
void Amped3_RiderUpdate(PPCRegister& r3, PPCRegister& f1) {
  if (REXCVAR_GET(rider_timers)) {
    TrackTimers(r3.u32);
  }
  if (REXCVAR_GET(rider_dump) > 0) {
    Dump(r3.u32, f1.f64);
  }
  if (!REXCVAR_GET(rider_probe)) {
    return;
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
    return;
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
}
