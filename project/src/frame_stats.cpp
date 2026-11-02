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

#include "achievement_progress.h"
#include "cpu_profile.h"
#include "frame_graph.h"
#include "game_menu.h"
#include "media_session.h"
#include "now_playing.h"
#include "quit_prompt.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

REXCVAR_DECLARE(bool, fps_log);
REXCVAR_DECLARE(bool, hitch_profile);
REXCVAR_DECLARE(bool, frame_times);
REXCVAR_DECLARE(bool, profile_pad_combo);
REXCVAR_DEFINE_INT32(achievement_test, -1, "Debug",
                     "Test only (use a scratch --user_data_root: unlocks persist): unlock this "
                     "achievement id 600 frames after start, to see the toast");
REXCVAR_DEFINE_INT32(achievement_test_frame, 600, "Debug", "Test only: the frame achievement_test unlocks at");
REXCVAR_DEFINE_INT32(pin_main_mask, 0, "CPU",
                     "Experiment: CPU affinity mask for the game's main thread (0 = leave alone); "
                     "applied once 600 frames in");
REXCVAR_DEFINE_INT32(pin_other_mask, 0, "CPU",
                     "Experiment: CPU affinity mask for every other thread of the process (0 = "
                     "leave alone); applied with pin_main_mask");
REXCVAR_DEFINE_STRING(find_guest_string, "", "Debug",
                      "Debug: 600 frames in, log every address in the game image (0x82000000-"
                      "0x83000000) holding this ASCII text");
REXCVAR_DEFINE_STRING(dump_guest_image, "", "Debug",
                      "Debug: 600 frames in, write the game image (0x82000000-0x83000000) to this file");
REXCVAR_DEFINE_BOOL(main_cycles_log, false, "Debug",
                    "Log the game main thread's CPU cycles per frame (mean of 300 frames) - "
                    "a steadier A/B measure than frame times");

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

// Stall watch (always on): while the game's frame thread hasn't finished a
// frame for 0.5 s, sample where its guest code is (lr and caller) every 5 ms,
// and log the top spots when frames resume. Guest addresses are sub_XXXXXXXX
// in any build, so a player's log names the function.
std::atomic<int64_t> g_stall_last_ns{0};
// Guest thread id of the thread that drew the last frame (at boot, a thread
// that later exits draws the first frames).
std::atomic<uint32_t> g_frame_thread_id{0};

int64_t SteadyNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

void StallWatchMain(rex::system::KernelState* kernel) {
  auto* memory = kernel->memory();
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const int64_t last = g_stall_last_ns.load();
    if (!last || SteadyNs() - last < 500'000'000) continue;
    rex::system::object_ref<rex::system::XThread> thread;
    for (auto& t : kernel->object_table()->GetObjectsByType<rex::system::XThread>()) {
      if (t->thread_id() == g_frame_thread_id.load()) thread = t;
    }
    if (!thread) {
      while (g_stall_last_ns.load() == last) std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    std::map<std::array<uint32_t, 5>, int> counts;
    int samples = 0;
    bool profiled = false;
    while (g_stall_last_ns.load() == last && SteadyNs() - last < 60'000'000'000) {
      // A long stall: also where the host threads are (the GPU command
      // processor's included) - cpu_profile.csv next to the executable.
      if (!profiled && SteadyNs() - last > 5'000'000'000) {
        profiled = true;
        REXLOG_INFO("Amped3: stall over 5 s: sampling the host threads for 3 s (cpu_profile.csv)");
        amped3::cpu_profile::Start(3);
      }
      auto* state = thread->thread_state();
      if (state && state->context()) {
        const auto& ctx = *state->context();
        // lr, then return addresses up the back chain.
        std::array<uint32_t, 5> chain{uint32_t(ctx.lr)};
        uint32_t sp = ctx.r1.u32;
        for (size_t i = 1; i < chain.size(); ++i) {
          if (sp < 0x70000000 || sp >= 0x80000000 || !Readable(memory, sp)) break;
          const uint32_t up = LoadBE32(memory, sp);
          if (up <= sp || up >= 0x80000000 || !Readable(memory, up - 8)) break;
          chain[i] = LoadBE32(memory, up - 8);
          sp = up;
        }
        ++counts[chain];
        ++samples;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const int64_t end = g_stall_last_ns.load() == last ? SteadyNs() : g_stall_last_ns.load();
    std::vector<std::pair<int, std::array<uint32_t, 5>>> top;
    for (const auto& [key, n] : counts) top.push_back({n, key});
    std::sort(top.rbegin(), top.rend());
    std::string out;
    for (size_t i = 0; i < top.size() && i < 6; ++i) {
      out += fmt::format(" [x{}", top[i].first);
      for (uint32_t a : top[i].second) {
        if (a) out += fmt::format(" {:08X}", a);
      }
      out += "]";
    }
    REXLOG_INFO("Amped3: stall {} ms, frame thread ({} samples):{}", (end - last) / 1'000'000, samples, out);
    // Still stuck after a minute: say so once more when (if) frames resume.
    while (g_stall_last_ns.load() == last) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

void NotifyStallWatch() {
  static std::once_flag started;
  std::call_once(started, [] { std::thread(StallWatchMain, REX_KERNEL_STATE()).detach(); });
  if (auto* thread = rex::system::XThread::GetCurrentThread()) g_frame_thread_id = thread->thread_id();
  g_stall_last_ns = SteadyNs();
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
void Amped3_D3DTraceFrame();

void Amped3_MinigameFpsOnFrame();  // minigame_fps_fix.cpp


void Amped3_OnFrame() {
  Amped3_PaceFrame();
  NotifyStallWatch();
  Amped3_MinigameFpsOnFrame();
  Amped3_D3DTraceFrame();
  amped3::frame_graph::RecordGameFrame();
  amped3::now_playing::OnFrame();
  amped3::media_session::OnFrame();
  amped3::achievement_progress::OnFrame();
  const uint16_t pad = amped3::quit_prompt::PadButtons();
  const double now_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  amped3::quit_prompt::OnPadButtons(pad, now_seconds);
  amped3::game_menu::OnPadButtons(pad);
  if (REXCVAR_GET(profile_pad_combo)) {
    ProfileOnPadCombo(pad, now_seconds);
  }
  if (REXCVAR_GET(achievement_test) >= 0) {
    static uint32_t frames = 0;
    if (++frames == uint32_t(REXCVAR_GET(achievement_test_frame))) {
      if (auto* kernel_state = REX_KERNEL_STATE()) {
        // 0 = the first in the list (ids are the title's own).
        uint32_t id = uint32_t(REXCVAR_GET(achievement_test));
        std::string ids;
        for (const auto& a : kernel_state->loaded_achievements()) ids += " " + std::to_string(a.id);
        if (id == 0 && !kernel_state->loaded_achievements().empty()) {
          id = kernel_state->loaded_achievements().front().id;
        }
        REXLOG_INFO("Amped3: achievement_test: unlocking {} (ids:{})", id, ids);
        kernel_state->UnlockAchievement(id);
      }
    }
  }
  if (REXCVAR_GET(pin_main_mask) || REXCVAR_GET(pin_other_mask)) {
    // Once all threads exist: the main thread (this one) on its CPUs, the rest
    // on theirs (e.g. keeping the GPU emulation threads off the main thread's
    // core and its SMT sibling).
    static uint32_t frames = 0;
    if (++frames == 600) {
      const DWORD self = GetCurrentThreadId();
      if (REXCVAR_GET(pin_main_mask)) {
        SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(uint32_t(REXCVAR_GET(pin_main_mask))));
      }
      int pinned = 0;
      if (REXCVAR_GET(pin_other_mask)) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry{sizeof(entry)};
        for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
          if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == self) continue;
          if (HANDLE thread = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE,
                                         entry.th32ThreadID)) {
            pinned += SetThreadAffinityMask(thread, DWORD_PTR(uint32_t(REXCVAR_GET(pin_other_mask)))) != 0;
            CloseHandle(thread);
          }
        }
        CloseHandle(snapshot);
      }
      REXLOG_INFO("Amped3: pinned the main thread to {:X} and {} others to {:X}", REXCVAR_GET(pin_main_mask),
                  pinned, REXCVAR_GET(pin_other_mask));
    }
  }
  if (!REXCVAR_GET(find_guest_string).empty()) {
    static uint32_t frames = 0;
    if (++frames == 600) {
      const std::string& needle = REXCVAR_GET(find_guest_string);
      auto* memory = REX_KERNEL_STATE()->memory();
      const uint8_t* base = memory->TranslateVirtual<const uint8_t*>(0x82000000);
      if (needle[0] == '@') {
        // "@82050000,8205F01C": the strings at these addresses.
        size_t pos = 1;
        while (pos < needle.size()) {
          size_t end = needle.find(',', pos);
          if (end == std::string::npos) end = needle.size();
          const uint32_t addr = uint32_t(std::stoul(needle.substr(pos, end - pos), nullptr, 16));
          if (addr >= 0x82000000 && addr < 0x83000000) {
            const char* text = reinterpret_cast<const char*>(base + (addr - 0x82000000));
            REXLOG_INFO("find_guest_string: {:08X}: {}", addr, std::string(text, strnlen(text, 96)));
          }
          pos = end + 1;
        }
        return;
      }
      int found = 0;
      for (uint32_t off = 0; off + needle.size() < 0x01000000 && found < 200; ++off) {
        if (base[off] == uint8_t(needle[0]) && !std::memcmp(base + off, needle.data(), needle.size())) {
          // Back to the start of the string.
          uint32_t start = off;
          while (start > 0 && base[start - 1] >= 0x20 && base[start - 1] < 0x7F && off - start < 64) --start;
          size_t len = 0;
          while (base[start + len] >= 0x20 && base[start + len] < 0x7F && len < 96) ++len;
          REXLOG_INFO("find_guest_string: {:08X} (string at {:08X}): {}", 0x82000000 + off, 0x82000000 + start,
                      std::string(reinterpret_cast<const char*>(base + start), len));
          ++found;
          off += uint32_t(needle.size());
        }
      }
      REXLOG_INFO("find_guest_string: {} found", found);
    }
  }
  // Debug: text_poke.txt next to the exe ("find|replace", UTF-16 in guest
  // memory, same length or shorter, padded with spaces) is applied once and
  // deleted: tests whether the game's UI re-reads string table text.
  {
    static uint32_t frames = 0;
    if (++frames % 30 == 0) {
      const auto poke = rex::filesystem::GetExecutableFolder() / "text_poke.txt";
      std::string line;
      if (std::ifstream in(poke); in && std::getline(in, line)) {
        in.close();
        std::error_code ec;
        std::filesystem::remove(poke, ec);
        const size_t bar = line.find('|');
        if (bar != std::string::npos && bar > 0) {
          auto utf16 = [](const std::string& s) {
            std::string out;
            for (char c : s) { out.push_back(0); out.push_back(c); }
            return out;
          };
          const std::string find = utf16(line.substr(0, bar));
          std::string repl = line.substr(bar + 1);
          repl.resize(bar, ' ');
          const std::string with = utf16(repl);
          auto* memory = REX_KERNEL_STATE()->memory();
          uint8_t* base = memory->TranslateVirtual<uint8_t*>(0);
          int hits = 0;
          for (uint64_t lo : {0x40000000ull, 0x80000000ull, 0xA0000000ull}) {
            MEMORY_BASIC_INFORMATION mbi;
            for (uint64_t a = lo; a < lo + 0x20000000ull; a += mbi.RegionSize) {
              if (!VirtualQuery(base + a, &mbi, sizeof(mbi))) break;
              if (mbi.State != MEM_COMMIT || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) continue;
              const uint64_t start = uint64_t(static_cast<uint8_t*>(mbi.BaseAddress) - base);
              const uint64_t end = std::min<uint64_t>(start + mbi.RegionSize, lo + 0x20000000ull);
              auto* p = base + start;
              auto* e = base + end;
              while ((p = std::search(p, e, find.begin(), find.end())) != e) {
                std::memcpy(p, with.data(), with.size());
                REXLOG_INFO("text_poke: {:08X}", uint32_t(p - base));
                ++hits;
                p += find.size();
              }
            }
          }
          REXLOG_INFO("text_poke: '{}' -> '{}': {} replaced", line.substr(0, bar), repl, hits);
        }
      }
    }
  }
  if (!REXCVAR_GET(dump_guest_image).empty()) {
    static uint32_t frames = 0;
    if (++frames == 600) {
      const uint8_t* base = REX_KERNEL_STATE()->memory()->TranslateVirtual<const uint8_t*>(0x82000000);
      std::ofstream(REXCVAR_GET(dump_guest_image), std::ios::binary)
          .write(reinterpret_cast<const char*>(base), 0x01000000);
      REXLOG_INFO("dump_guest_image: wrote {}", REXCVAR_GET(dump_guest_image));
    }
  }
  if (REXCVAR_GET(main_cycles_log)) {
    // VdSwap runs on the game's main thread.
    static ULONG64 last_cycles = 0, sum = 0;
    static uint32_t frames = 0;
    ULONG64 cycles = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cycles);
    if (last_cycles) {
      sum += cycles - last_cycles;
      if (++frames == 300) {
        REXLOG_INFO("Amped3: main thread {:.3f} Mcycles/frame", double(sum) / frames / 1e6);
        sum = 0;
        frames = 0;
      }
    }
    last_cycles = cycles;
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
