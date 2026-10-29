// frame_stats.cpp - Per-frame timing from the game's single VdSwap call site
//
// With --fps_log=true, logs frames per second plus average and worst frame time
// every two seconds. (The SDK's perf counters are compiled out of Release.)
// With --hitch_profile=true, samples every guest thread's lr and caller every
// 5 ms and, after a frame longer than 70 ms, logs where the samples landed.
// With --frame_times=true, writes every frame time to frametimes.csv.

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

#include "cpu_profile.h"
#include "frame_graph.h"
#include "quit_prompt.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

REXCVAR_DECLARE(bool, fps_log);
REXCVAR_DECLARE(bool, hitch_profile);
REXCVAR_DECLARE(bool, frame_times);
REXCVAR_DECLARE(bool, profile_pad_combo);

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point g_window_start{};
Clock::time_point g_last_frame{};
uint32_t g_frames = 0;
double g_worst_ms = 0.0;
std::FILE* g_csv = nullptr;

struct Sample {
  Clock::time_point time;
  uint32_t tid, lr, caller;
};

std::mutex g_samples_mutex;
std::deque<Sample> g_samples;
std::once_flag g_sampler_started;

uint32_t LoadBE32(rex::memory::Memory* memory, uint32_t address) {
  const auto* p = memory->TranslateVirtual<const uint8_t*>(address);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool Readable(rex::memory::Memory* memory, uint32_t address) {
  auto* heap = const_cast<rex::memory::BaseHeap*>(memory->LookupHeap(address));
  uint32_t protect = 0;
  return heap && heap->QueryProtect(address, &protect) &&
         (protect & rex::memory::kMemoryProtectRead);
}

// First return address up the back chain: the caller of the current function.
uint32_t Caller(rex::memory::Memory* memory, uint32_t sp) {
  if (sp < 0x70000000 || sp >= 0x80000000 || !Readable(memory, sp)) {
    return 0;
  }
  const uint32_t caller_sp = LoadBE32(memory, sp);
  if (caller_sp <= sp || caller_sp >= 0x80000000 || !Readable(memory, caller_sp - 8)) {
    return 0;
  }
  return LoadBE32(memory, caller_sp - 8);
}

void SamplerMain(rex::system::KernelState* kernel) {
  auto* memory = kernel->memory();
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto now = Clock::now();
    auto threads = kernel->object_table()->GetObjectsByType<rex::system::XThread>();
    std::lock_guard lock(g_samples_mutex);
    for (auto& thread : threads) {
      auto* state = thread->thread_state();
      if (!state || !state->context()) {
        continue;
      }
      const auto& ctx = *state->context();
      g_samples.push_back({now, thread->thread_id(), uint32_t(ctx.lr), Caller(memory, ctx.r1.u32)});
    }
    while (!g_samples.empty() && now - g_samples.front().time > std::chrono::seconds(2)) {
      g_samples.pop_front();
    }
  }
}

void ReportHitch(Clock::time_point start, Clock::time_point end, double ms) {
  std::map<std::tuple<uint32_t, uint32_t, uint32_t>, int> counts;
  {
    std::lock_guard lock(g_samples_mutex);
    for (const auto& s : g_samples) {
      if (s.time >= start && s.time <= end) {
        ++counts[{s.tid, s.lr, s.caller}];
      }
    }
  }
  std::vector<std::pair<int, std::tuple<uint32_t, uint32_t, uint32_t>>> top;
  for (const auto& [key, n] : counts) {
    top.push_back({n, key});
  }
  std::sort(top.rbegin(), top.rend());
  std::string out;
  for (size_t i = 0; i < top.size() && i < 40; ++i) {
    const auto& [tid, lr, caller] = top[i].second;
    out += fmt::format(" [tid={:X} lr={:08X} caller={:08X} x{}]", tid, lr, caller, top[i].first);
  }
  REXLOG_INFO("Amped3: {} {:.0f}ms:{}", ms < 0 ? "window" : "hitch", ms < 0 ? -ms : ms, out);
}

}  // namespace

void Amped3_PaceFrame();  // frame_rate.cpp

namespace {

// --profile_pad_combo: L3 + R3 held for a second starts a 10 s CPU profile
// (for machines without the scripted pad, e.g. a Steam Deck).
void ProfileOnPadCombo(uint16_t buttons, double now) {
  static double held_since = -1.0;
  static bool fired = false;
  constexpr uint16_t kThumbs = 0x0040 | 0x0080;
  if ((buttons & kThumbs) != kThumbs) {
    held_since = -1.0;
    fired = false;
    return;
  }
  if (held_since < 0.0) {
    held_since = now;
  }
  if (!fired && now - held_since >= 1.0) {
    fired = true;
    amped3::cpu_profile::Start(10);
  }
}

}  // namespace

// [[midasm_hook]] at 0x825AB7F4, the game's `bl VdSwap` (once per presented frame).
void Amped3_OnFrame() {
  Amped3_PaceFrame();
  amped3::frame_graph::RecordGameFrame();
  const uint16_t pad = amped3::quit_prompt::PadButtons();
  const double now_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  amped3::quit_prompt::OnPadButtons(pad, now_seconds);
  if (REXCVAR_GET(profile_pad_combo)) {
    ProfileOnPadCombo(pad, now_seconds);
  }
  if (!REXCVAR_GET(fps_log) && !REXCVAR_GET(hitch_profile) && !REXCVAR_GET(frame_times)) {
    return;
  }
  if (REXCVAR_GET(hitch_profile)) {
    std::call_once(g_sampler_started, [] {
      std::thread(SamplerMain, REX_KERNEL_STATE()).detach();
    });
  }
  const auto now = Clock::now();
  if (g_frames == 0 && g_window_start == Clock::time_point{}) {
    g_window_start = g_last_frame = now;
    return;
  }
  const double frame_ms = std::chrono::duration<double, std::milli>(now - g_last_frame).count();
  if (REXCVAR_GET(hitch_profile) && frame_ms > 70.0) {
    ReportHitch(g_last_frame, now, frame_ms);
  }
  if (REXCVAR_GET(frame_times)) {
    static const Clock::time_point csv_start = now;
    if (!g_csv) {
      g_csv = _wfopen((rex::filesystem::GetExecutableFolder() / "frametimes.csv").c_str(), L"w");
      if (g_csv) std::fputs("time_s,frame_ms\n", g_csv);
    }
    if (g_csv) {
      std::fprintf(g_csv, "%.4f,%.3f\n", std::chrono::duration<double>(now - csv_start).count(), frame_ms);
    }
  }
  g_worst_ms = std::max(g_worst_ms, frame_ms);
  g_last_frame = now;
  ++g_frames;
  const double window_s = std::chrono::duration<double>(now - g_window_start).count();
  if (window_s >= 2.0 && REXCVAR_GET(hitch_profile)) {
    ReportHitch(g_window_start, now, -window_s * 1000.0);  // negative = whole-window profile
  }
  if (window_s >= 2.0) {
    if (REXCVAR_GET(fps_log) || REXCVAR_GET(hitch_profile)) {
      REXLOG_INFO("Amped3: fps={:.1f} avg={:.2f}ms worst={:.2f}ms", g_frames / window_s,
                  window_s * 1000.0 / g_frames, g_worst_ms);
    }
    if (g_csv) std::fflush(g_csv);
    g_window_start = now;
    g_frames = 0;
    g_worst_ms = 0.0;
  }
}
