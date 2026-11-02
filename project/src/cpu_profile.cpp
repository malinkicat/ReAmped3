// cpu_profile.cpp - Debug sampling profiler for host threads
//
// Start(seconds) samples every thread of the process ~500 times a second: each
// thread is suspended, its registers and the top 64 KB of its stack are
// copied, and it is resumed; its instruction pointer and up to kFrames caller
// frames are then unwound (RtlVirtualUnwind) on the copy. Nothing that takes a
// lock runs while a thread is suspended: allocation could need the heap lock,
// and unwinding in place, the function tables' (it froze the game mid-profile).
// A watchdog still resumes any thread left suspended over 100 ms. At the end the counts go
// to cpu_profile.csv next to the executable as
//   thread,module,rva,self,total
// (busy samples only; samples blocked in a syscall are counted per thread as
// "#idle" lines).
// and tools/cpu_profile.py symbolizes them with the modules' PDBs.

#include "cpu_profile.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace amped3::cpu_profile {
namespace {

constexpr int kFrames = 20;
constexpr auto kInterval = std::chrono::microseconds(2000);

std::atomic<bool> g_running{false};
// The sampled thread's stack top, copied while it's suspended.
alignas(16) uint8_t g_stack[64 * 1024];
// The thread the sampler has suspended (or null), and since when (steady
// clock ticks), for the watchdog.
std::atomic<HANDLE> g_suspended{nullptr};
std::atomic<int64_t> g_suspended_at{0};

void Watchdog() {
  while (g_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    HANDLE h = g_suspended.load();
    const int64_t at = g_suspended_at.load();
    if (h && std::chrono::steady_clock::now().time_since_epoch().count() - at >
                 std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                     std::chrono::milliseconds(100)).count()) {
      // The sampler skips its own resume once this one has run.
      if (g_suspended.compare_exchange_strong(h, nullptr)) {
        ResumeThread(h);
        REXLOG_WARN("Amped3: cpu profile: a thread stayed suspended over 100 ms; resumed it");
      }
    }
  }
}

struct Module {
  uintptr_t base = 0, end = 0;
  std::string name;
};

std::vector<Module> LoadModules() {
  std::vector<Module> modules;
  HMODULE handles[1024];
  DWORD needed = 0;
  if (!EnumProcessModules(GetCurrentProcess(), handles, sizeof(handles), &needed)) {
    return modules;
  }
  for (DWORD i = 0; i < needed / sizeof(HMODULE); ++i) {
    MODULEINFO info{};
    char name[MAX_PATH] = {};
    if (GetModuleInformation(GetCurrentProcess(), handles[i], &info, sizeof(info)) &&
        GetModuleBaseNameA(GetCurrentProcess(), handles[i], name, sizeof(name))) {
      const auto base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
      modules.push_back({base, base + info.SizeOfImage, name});
    }
  }
  std::sort(modules.begin(), modules.end(),
            [](const Module& a, const Module& b) { return a.base < b.base; });
  return modules;
}

std::string ThreadName(HANDLE thread, DWORD id) {
  PWSTR description = nullptr;
  std::string name;
  if (SUCCEEDED(GetThreadDescription(thread, &description)) && description) {
    for (const wchar_t* c = description; *c; ++c) {
      name += char(*c < 128 ? *c : '?');
    }
    LocalFree(description);
  }
  for (char& c : name) {
    if (c == ',') c = ' ';
  }
  return name.empty() ? "tid" + std::to_string(id) : name;
}

void Run(int seconds) {
  const DWORD self = GetCurrentThreadId();
  const auto modules = LoadModules();
  auto locate = [&](uintptr_t address) -> std::pair<int, uint32_t> {
    auto it = std::upper_bound(modules.begin(), modules.end(), address,
                               [](uintptr_t a, const Module& m) { return a < m.base; });
    if (it == modules.begin()) return {-1, 0};
    --it;
    if (address >= it->end) return {-1, 0};
    return {int(it - modules.begin()), uint32_t(address - it->base)};
  };

  // (thread name, module index, rva) -> {self, total}
  std::map<std::tuple<std::string, int, uint32_t>, std::array<uint64_t, 2>> counts;
  std::map<DWORD, std::pair<HANDLE, std::string>> threads;
  std::map<std::string, uint64_t> idle;
  std::map<std::pair<std::string, std::string>, uint64_t> chains;
  uint64_t samples = 0;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  auto next_refresh = std::chrono::steady_clock::now();
  std::array<uintptr_t, kFrames> frames{};

  while (g_running && std::chrono::steady_clock::now() < end) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_refresh) {
      next_refresh = now + std::chrono::seconds(1);
      HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      THREADENTRY32 entry{sizeof(entry)};
      for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == self ||
            threads.count(entry.th32ThreadID)) {
          continue;
        }
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                  THREAD_QUERY_LIMITED_INFORMATION,
                              FALSE, entry.th32ThreadID);
        if (!h) continue;
        std::string name = ThreadName(h, entry.th32ThreadID);
        // Only named (runtime and guest) threads: unnamed ones are the driver's
        // and the system's, and unwinding one suspended while it holds the
        // module list lock (loading a module) deadlocks the sampler.
        if (name.rfind("tid", 0) == 0) {
          CloseHandle(h);
          threads[entry.th32ThreadID] = {nullptr, name};
          continue;
        }
        threads[entry.th32ThreadID] = {h, name};
      }
      CloseHandle(snapshot);
    }
    for (auto& [id, thread] : threads) {
      if (!thread.first) continue;
      int depth = 0;
      const auto sample_start = std::chrono::steady_clock::now();
      if (SuspendThread(thread.first) == DWORD(-1)) continue;
      g_suspended_at = std::chrono::steady_clock::now().time_since_epoch().count();
      g_suspended = thread.first;
      // While suspended only its registers and the top of its stack are
      // copied (no locks: the unwinder's function table lookups take them).
      CONTEXT context{};
      context.ContextFlags = CONTEXT_FULL;
      size_t copied = 0;
      if (GetThreadContext(thread.first, &context)) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<void*>(context.Rsp), &region, sizeof(region))) {
          const uintptr_t end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
          copied = std::min<size_t>(sizeof(g_stack), end - context.Rsp);
          std::memcpy(g_stack, reinterpret_cast<const void*>(context.Rsp), copied);
        }
      }
      // Unless the watchdog already did.
      HANDLE expected = thread.first;
      if (g_suspended.compare_exchange_strong(expected, nullptr)) ResumeThread(thread.first);
      if (const auto took = std::chrono::steady_clock::now() - sample_start; took > std::chrono::milliseconds(50)) {
        REXLOG_WARN("Amped3: cpu profile: sampling '{}' took {} ms (copied {} bytes)", thread.second,
                    std::chrono::duration_cast<std::chrono::milliseconds>(took).count(), copied);
      }
      // Unwind on the copy: stack addresses in [Rsp, Rsp + copied) are moved
      // into it, and the walk stops when it leaves it.
      if (copied) {
        const DWORD64 real = context.Rsp, copy = reinterpret_cast<DWORD64>(g_stack);
        auto in_real = [&](DWORD64 a) { return a >= real && a < real + copied; };
        context.Rsp = copy;
        if (in_real(context.Rbp)) context.Rbp += copy - real;
        for (; depth < kFrames && context.Rip; ++depth) {
          frames[depth] = context.Rip;
          DWORD64 image_base = 0;
          auto* function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
          if (!function) break;
          void* handler_data = nullptr;
          DWORD64 establisher = 0;
          RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context,
                           &handler_data, &establisher, nullptr);
          if (context.Rsp < copy || context.Rsp >= copy + copied) {
            // Off the copy: this frame and its caller (read from the copy)
            // are still right; nothing above them is.
            ++depth;
            if (depth < kFrames && context.Rip) frames[depth++] = context.Rip;
            break;
          }
          if (in_real(context.Rbp)) context.Rbp += copy - real;
        }
      }
      // A thread stopped right after a syscall instruction is blocked in the
      // kernel (waiting); count it as idle instead of attributing a stack, but
      // keep where it waits ("thread (waiting)" chains, deeper: the waits are
      // under several runtime frames).
      bool waiting = false;
      if (depth > 0) {
        const auto* code = reinterpret_cast<const uint8_t*>(frames[0]);
        waiting = code[-2] == 0x0F && code[-1] == 0x05;
      }
      if (waiting) {
        ++idle[thread.second];
        std::string chain;
        for (int i = 0; i < depth; ++i) {
          const auto [module, rva] = locate(frames[i]);
          chain += fmt::format("{}{}+{:X}", i ? ";" : "", module < 0 ? "?" : modules[module].name, rva);
        }
        ++chains[{thread.second + " (waiting)", chain}];
        continue;
      }
      if (depth > 1) {
        // Leaf plus up to four callers, to see who reaches a hot leaf (e.g. a lock).
        std::string chain;
        for (int i = 0; i < std::min(depth, 5); ++i) {
          const auto [module, rva] = locate(frames[i]);
          chain += fmt::format("{}{}+{:X}", i ? ";" : "", module < 0 ? "?" : modules[module].name, rva);
        }
        ++chains[{thread.second, chain}];
      }
      for (int i = 0; i < depth; ++i) {
        const auto [module, rva] = locate(frames[i]);
        auto& c = counts[{thread.second, module, module < 0 ? 0 : rva}];
        if (i == 0) ++c[0];
        bool repeated = false;  // Count recursion once per sample.
        for (int j = 0; j < i; ++j) repeated |= frames[j] == frames[i];
        if (!repeated) ++c[1];
      }
    }
    ++samples;
    std::this_thread::sleep_until(now + kInterval);
  }
  for (auto& [id, thread] : threads) {
    if (thread.first) CloseHandle(thread.first);
  }

  const auto path = rex::filesystem::GetExecutableFolder() / "cpu_profile.csv";
  if (std::FILE* f = _wfopen(path.c_str(), L"w")) {
    std::fprintf(f, "thread,module,rva,self,total\n");
    for (const auto& [key, c] : counts) {
      const auto& [name, module, rva] = key;
      std::fprintf(f, "%s,%s,%X,%llu,%llu\n", name.c_str(),
                   module < 0 ? "?" : modules[module].name.c_str(), rva,
                   (unsigned long long)c[0], (unsigned long long)c[1]);
    }
    for (const auto& [key, n] : chains) {
      std::fprintf(f, "#chain,%s,%s,%llu\n", key.first.c_str(), key.second.c_str(),
                   (unsigned long long)n);
    }
    for (const auto& [name, n] : idle) {
      std::fprintf(f, "#idle,%s,%llu\n", name.c_str(), (unsigned long long)n);
    }
    std::fprintf(f, "#samples,%llu\n", (unsigned long long)samples);
    std::fclose(f);
  }
  REXLOG_INFO("Amped3: cpu profile: {} samples written to {}", samples, path.string());
  g_running = false;
}

}  // namespace

void Start(int seconds) {
  if (g_running.exchange(true)) {
    return;
  }
  REXLOG_INFO("Amped3: cpu profile: sampling for {} s", seconds);
  std::thread(Run, seconds).detach();
  std::thread(Watchdog).detach();
}

}  // namespace amped3::cpu_profile
