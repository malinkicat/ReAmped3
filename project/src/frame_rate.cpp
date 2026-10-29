// frame_rate.cpp - Frame-rate unlock without changing the guest vblank rate
//
// The game's D3D runtime decides when a frame may flip in sub_825AB430, called
// with r3 = (present interval << 16) | tear threshold. Amped 3 asks for an
// interval of 2 (every second 60 Hz vblank = 30 FPS). --framerate rewrites it:
//   30  game default (interval 2)
//   60  interval 1: flip on every vblank
//   any other rate (e.g. 141, just under a VRR display's maximum, or 50) or 0
//       (unlimited): interval 0, flip immediately, and pace frames on the host
//       to the target in Amped3_PaceFrame (called just before VdSwap).
// The guest stays at 60 Hz, so anything the game times by vblanks is unchanged.
//
// In-engine cinematics (the story scenes) advance by the vblanks since the last
// frame, at least one: right at 30 and 60, but above 60 FPS every frame still
// counts a whole vblank, so they ran at cap / 60 speed (out of sync with their
// audio and pop-ups). 0x8252B644 (the cinematic's elapsed-time-to-fraction
// conversion, ~14 times a frame) only runs while one plays; while it does, the
// pacer holds 60 FPS.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include <atomic>
#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

REXCVAR_DECLARE(int32_t, framerate);
REXCVAR_DECLARE(bool, fps_log);

namespace {

using Clock = std::chrono::steady_clock;
Clock::time_point g_next_frame{};
std::atomic<uint32_t> g_cinematic_calls{0};

uint32_t IntervalFor(int32_t fps) {
  if (fps == 30) return 2;
  if (fps == 60) return 1;
  return 0;
}

}  // namespace

// [[midasm_hook]] at 0x825AB430 (entry of the flip-scheduling function).
void Amped3_PresentInterval(PPCRegister& r3) {
  static uint32_t last_request = ~0u;
  if (REXCVAR_GET(fps_log) && r3.u32 != last_request) {
    REXLOG_INFO("Amped3: flip request interval={} threshold={:X}", r3.u32 >> 16, r3.u32 & 0xFFFF);
    last_request = r3.u32;
  }
  const int32_t fps = REXCVAR_GET(framerate);
  if (fps == 30) {
    return;  // Leave the game's own interval alone.
  }
  r3.u64 = (uint64_t(IntervalFor(fps)) << 16) | (r3.u32 & 0xFFFF);
}

void Amped3_Step_8252B644();  // step_probe.cpp

// [[midasm_hook]] at 0x8252B644: a cinematic is playing.
void Amped3_CinematicStep() {
  g_cinematic_calls.fetch_add(1, std::memory_order_relaxed);
  Amped3_Step_8252B644();
}

// Called from Amped3_OnFrame once per frame, before VdSwap. Above 60 FPS with a
// limit (interval 0), paces to the target. At 30 and 60 the flips wait for
// vblank, except in the title screen and menus, which swap without waiting
// (~600 FPS); frames that come in under half a vblank apart are held back to one
// vblank, so vblank-locked frames are never touched.
void Amped3_PaceFrame() {
  const int32_t fps = REXCVAR_GET(framerate);
  // A cinematic this frame or within the last half second (between its shots).
  static int cinematic_frames_left = 0;
  if (g_cinematic_calls.exchange(0, std::memory_order_relaxed)) {
    if (!cinematic_frames_left && REXCVAR_GET(fps_log)) {
      REXLOG_INFO("Amped3: cinematic: pacing at 60 FPS");
    }
    cinematic_frames_left = 30;
  } else if (cinematic_frames_left && !--cinematic_frames_left && REXCVAR_GET(fps_log)) {
    REXLOG_INFO("Amped3: cinematic ended");
  }
  // Above 60 (interval 0 in effect), a cinematic is paced at 60 below.
  const bool cinematic_at_60 = cinematic_frames_left && (fps == 0 || fps > 60);
#ifdef _WIN32
  static const bool timer_resolution = [] { return timeBeginPeriod(1) == TIMERR_NOERROR; }();
  (void)timer_resolution;
#endif
  if (fps == 30 || fps == 60) {
    static Clock::time_point last{};
    constexpr auto kVblank = std::chrono::nanoseconds(1'000'000'000LL / 60);
    auto now = Clock::now();
    if (last != Clock::time_point{} && now - last < kVblank / 2) {
      const auto deadline = last + kVblank;
      std::this_thread::sleep_until(deadline - std::chrono::microseconds(1500));
      while (Clock::now() < deadline) {
        std::this_thread::yield();
      }
      now = Clock::now();
    }
    last = now;
    return;
  }
  if (fps == 0 && !cinematic_at_60) {
    return;  // Unlimited.
  }
  const auto period = std::chrono::nanoseconds(1'000'000'000LL / (cinematic_at_60 ? 60 : fps));
  const auto now = Clock::now();
  if (g_next_frame == Clock::time_point{} || now >= g_next_frame + period / 2) {
    // First frame, or this one was late by over half a period (a hitch): never
    // run short catch-up frames after it (they bunch frames together); the next
    // deadline is a full period away.
    g_next_frame = now + period;
    return;
  }
  if (now >= g_next_frame) {
    // Slightly late: keep the cadence (restarting it on every small delay made
    // a 144 cap run at ~141).
    g_next_frame += period;
    return;
  }
  // Wait on a high-resolution timer to ~0.3 ms before the deadline, then spin.
  static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
                                               CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                               TIMER_ALL_ACCESS);
  const auto coarse = g_next_frame - std::chrono::microseconds(300) - now;
  if (coarse.count() > 0) {
    LARGE_INTEGER due;
    due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(coarse).count() / 100;
    if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
      WaitForSingleObject(timer, INFINITE);
    } else {
      std::this_thread::sleep_until(g_next_frame - std::chrono::microseconds(1500));
    }
  }
  while (Clock::now() < g_next_frame) {
    YieldProcessor();
  }
  g_next_frame += period;
}
