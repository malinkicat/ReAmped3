// clock_probe.cpp - Debug probe for the game's stopwatch reads
//
// With --clock_probe=true, each call site of sub_8218DB90 (stopwatch -> elapsed
// seconds in f1) reports calls per second and how much the value moves per
// call, to find which reads drive the simulation and whether they follow real
// time at different frame rates. Hooks are [[midasm_hook]]s after each call.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>

REXCVAR_DECLARE(bool, clock_probe);

namespace {

struct Site {
  uint32_t address = 0;
  uint64_t calls = 0;
  double first = 0.0, last = 0.0;
};

constexpr std::array<uint32_t, 16> kSites = {
    0x82194654,
    0x821969D8,
    0x8219717C,
    0x8227F610,
    0x822CB14C,
    0x822CDDCC,
    0x822E977C,
    0x823B55C8,
    0x823B55DC,
    0x823B5668,
    0x823EAD98,
    0x82496A18,
    0x82499CE4,
    0x8249D6EC,
    0x824A4E14,
    0x824A4EB0,
};

std::mutex g_mutex;
std::array<Site, kSites.size()> g_sites{};
std::chrono::steady_clock::time_point g_window_start{};

void Record(size_t index, double seconds) {
  if (!REXCVAR_GET(clock_probe)) {
    return;
  }
  std::lock_guard lock(g_mutex);
  auto& site = g_sites[index];
  if (site.calls == 0) {
    site.first = seconds;
  }
  site.address = kSites[index];
  site.last = seconds;
  ++site.calls;
  const auto now = std::chrono::steady_clock::now();
  if (g_window_start == std::chrono::steady_clock::time_point{}) {
    g_window_start = now;
  }
  const double window = std::chrono::duration<double>(now - g_window_start).count();
  if (window < 2.0) {
    return;
  }
  for (auto& s : g_sites) {
    if (s.calls) {
      REXLOG_INFO("ClockProbe: site={:08X} calls/s={:.1f} value={:.4f} per_call={:.5f}", s.address,
                  s.calls / window, s.last, s.calls > 1 ? (s.last - s.first) / (s.calls - 1) : 0.0);
    }
    s = {};
  }
  g_window_start = now;
}

}  // namespace

void Amped3_Clock_82194654(PPCRegister& f1) { Record(0, f1.f64); }
void Amped3_Clock_821969D8(PPCRegister& f1) { Record(1, f1.f64); }
void Amped3_Clock_8219717C(PPCRegister& f1) { Record(2, f1.f64); }
void Amped3_Clock_8227F610(PPCRegister& f1) { Record(3, f1.f64); }
void Amped3_Clock_822CB14C(PPCRegister& f1) { Record(4, f1.f64); }
void Amped3_Clock_822CDDCC(PPCRegister& f1) { Record(5, f1.f64); }
void Amped3_Clock_822E977C(PPCRegister& f1) { Record(6, f1.f64); }
void Amped3_Clock_823B55C8(PPCRegister& f1) { Record(7, f1.f64); }
void Amped3_Clock_823B55DC(PPCRegister& f1) { Record(8, f1.f64); }
void Amped3_Clock_823B5668(PPCRegister& f1) { Record(9, f1.f64); }
void Amped3_Clock_823EAD98(PPCRegister& f1) { Record(10, f1.f64); }
void Amped3_Clock_82496A18(PPCRegister& f1) { Record(11, f1.f64); }
void Amped3_Clock_82499CE4(PPCRegister& f1) { Record(12, f1.f64); }
void Amped3_Clock_8249D6EC(PPCRegister& f1) { Record(13, f1.f64); }
void Amped3_Clock_824A4E14(PPCRegister& f1) { Record(14, f1.f64); }
void Amped3_Clock_824A4EB0(PPCRegister& f1) { Record(15, f1.f64); }
