// watch_probe.cpp - Debug probe: which code writes a rider's actor field
//
// With --watch_actor_offset=N, the scripted "watch" command arms a hardware
// data breakpoint (debug register DR0) on the 4 bytes at actor+N of the next
// rider updated (actor = [[rider+1500]+20], as in rider_probe.cpp), on the
// thread updating the riders. A vectored exception handler counts the writes
// by host instruction address and logs them every two seconds with the last
// value written (on the first rider whose field isn't 0); build/amped3.map names the functions
// (amped3.exe offsets).

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/system/kernel_state.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

REXCVAR_DEFINE_INT32(watch_actor_offset, 0, "Debug",
                     "Actor field offset the scripted \"watch\" command puts a hardware write "
                     "breakpoint on (0 = off)");

// --watch_rider_offset=N (N != 0): watch rider+N instead (N may be negative:
// the trick tuning sits before the rider object), for reads and writes.
REXCVAR_DEFINE_INT32(watch_rider_offset, 0, "Debug",
                     "Rider offset the scripted \"watch\" command watches for reads and writes (0 = off)");

namespace {

std::atomic<bool> g_requested{false};
std::atomic<bool> g_installed{false};
uint8_t* g_host_address = nullptr;
uintptr_t g_exe_base = 0;
std::mutex g_mutex;
std::map<uintptr_t, uint64_t> g_writes;  // exe offset -> count
std::map<uintptr_t, float> g_last_value;
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

LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
      !(info->ContextRecord->Dr6 & 1)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  info->ContextRecord->Dr6 = 0;
  const uintptr_t rip = uintptr_t(info->ContextRecord->Rip);
  const uintptr_t offset = rip >= g_exe_base ? rip - g_exe_base : rip;
  {
    std::lock_guard lock(g_mutex);
    ++g_writes[offset];
    g_last_value[offset] = LoadBEFloat(g_host_address);
    const auto now = std::chrono::steady_clock::now();
    if (g_window_start == std::chrono::steady_clock::time_point{}) {
      g_window_start = now;
    }
    if (now - g_window_start >= std::chrono::seconds(2)) {
      std::string out;
      for (const auto& [at, n] : g_writes) {
        out += fmt::format(" [amped3.exe+{:X} {}x last={:.4f}]", at, n, g_last_value[at]);
      }
      REXLOG_INFO("WatchProbe: accesses in 2 s:{}", out);
      g_writes.clear();
      g_window_start = now;
    }
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

}  // namespace

namespace amped3::watch_probe {

void Request() {
  g_requested = true;
  REXLOG_INFO("WatchProbe: armed for the next rider update");
}

// Called from the per-rider update hook (rider_probe.cpp), on the thread that
// updates the riders.
void OnRiderUpdate(uint32_t rider) {
  if (!g_requested.load(std::memory_order_relaxed) || g_installed.exchange(true)) {
    return;
  }
  const int32_t field = REXCVAR_GET(watch_actor_offset);
  const int32_t rider_field = REXCVAR_GET(watch_rider_offset);
  auto* memory = REX_KERNEL_MEMORY();
  if (rider_field) {
    g_host_address = memory->TranslateVirtual<uint8_t*>(rider + uint32_t(rider_field));
    REXLOG_INFO("WatchProbe: rider {:08X}+({}) = {}", rider, rider_field, LoadBE32(g_host_address));
  } else {
    const uint32_t holder = LoadBE32(memory->TranslateVirtual<const uint8_t*>(rider + 1500));
    if (!field || !holder) {
      g_installed = false;
      return;
    }
    const uint32_t actor = LoadBE32(memory->TranslateVirtual<const uint8_t*>(holder + 20));
    if (!actor) {
      g_installed = false;
      return;
    }
    g_host_address = memory->TranslateVirtual<uint8_t*>(actor + uint32_t(field));
    // A rider that uses the field (not 0 - the player's may stay unused).
    if (LoadBE32(g_host_address) == 0) {
      g_installed = false;
      return;
    }
  }
  g_exe_base = uintptr_t(GetModuleHandleW(nullptr));
  AddVectoredExceptionHandler(1, WatchHandler);
  // Debug registers of every thread of the process (the field may be written by
  // a thread other than the one updating the riders), each set while the
  // thread is suspended, from a helper thread.
  const DWORD caller = GetCurrentThreadId();
  std::thread([caller] {
    const DWORD helper = GetCurrentThreadId();
    int armed = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
      if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == helper) {
        continue;
      }
      HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                                 FALSE, entry.th32ThreadID);
      if (!thread) continue;
      if (SuspendThread(thread) != DWORD(-1)) {
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(thread, &context)) {
          context.Dr0 = DWORD64(g_host_address);
          // L0 enable, R/W0 = 01 (writes), LEN0 = 11 (4 bytes).
          context.Dr7 = (context.Dr7 & ~DWORD64(0xF0003)) | 1 | ((REXCVAR_GET(watch_rider_offset) ? 3 : 1) << 16) | (3 << 18);
          if (SetThreadContext(thread, &context)) ++armed;
        }
        ResumeThread(thread);
      }
      CloseHandle(thread);
    }
    CloseHandle(snapshot);
    REXLOG_INFO("WatchProbe: breakpoint set on {} threads (caller {})", armed, caller);
  }).join();
  REXLOG_INFO("WatchProbe: value now {:.4f}", LoadBEFloat(g_host_address));
  REXLOG_INFO("WatchProbe: watching rider {:08X} (host {})", rider, static_cast<void*>(g_host_address));
}

}  // namespace amped3::watch_probe
