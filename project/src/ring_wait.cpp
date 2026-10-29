// ring_wait.cpp - Sleep instead of spin while the game waits for ring space
//
// The game's D3D waits for space in the GPU command ring in sub_825A3550: it
// spins on the command processor's read pointer (loc_825A35E8 -> 825A35F8),
// calling the GPU-hang check sub_825AA830 each time. The ring is full then,
// so the command processor has plenty queued and waking a little late costs
// nothing - but the spin keeps a CPU core at full clock. On a Steam Deck the
// CPU and GPU share one ~15 W budget and that spin takes power the GPU needs
// (riding: APU at its limit, CPU ~3.2 GHz, GPU ~550 of 1600 MHz).
// --ring_wait_sleep_us: -1 = auto (sleep 200 us per check under Proton/Wine,
// spin on Windows), 0 = spin, N = sleep N us per check.

#include <rex/cvar.h>
#include <rex/logging.h>

#include <atomic>
#include <chrono>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

REXCVAR_DEFINE_INT32(ring_wait_sleep_us, -1, "CPU",
                     "While the game waits for GPU ring space: -1 auto (sleep 200 us under "
                     "Proton/Wine, spin on Windows), 0 spin, N sleep N microseconds per check");
REXCVAR_DECLARE(bool, fps_log);

namespace {

bool RunningUnderWine() {
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

int SleepMicroseconds() {
  static const int us = [] {
    int value = REXCVAR_GET(ring_wait_sleep_us);
    if (value < 0) {
      value = RunningUnderWine() ? 200 : 0;
    }
    REXLOG_INFO("Amped3: ring space wait: {}", value ? fmt::format("sleep {} us per check", value)
                                                      : std::string("spin"));
    return value;
  }();
  return us;
}

std::atomic<uint64_t> g_sleeps{0};

}  // namespace

// [[midasm_hook]] at 0x825A35E8: one unsuccessful ring-space check.
void Amped3_RingSpaceSpin() {
  const int us = SleepMicroseconds();
  if (us <= 0) {
    return;
  }
  thread_local HANDLE timer = CreateWaitableTimerExW(
      nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  LARGE_INTEGER due;
  due.QuadPart = -int64_t(us) * 10;
  if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
    WaitForSingleObject(timer, INFINITE);
  } else {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
  }
  if (REXCVAR_GET(fps_log)) {
    const uint64_t n = ++g_sleeps;
    static std::atomic<int64_t> last_log{0};
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    int64_t last = last_log.load();
    if (now - last >= 2 && last_log.compare_exchange_strong(last, now)) {
      REXLOG_INFO("Amped3: ring space wait: {} sleeps so far", n);
    }
  }
}
