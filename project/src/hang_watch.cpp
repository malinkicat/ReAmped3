// hang_watch.cpp - Debug watchdog that logs where every guest thread is

#include "hang_watch.h"

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xevent.h>
#include <rex/system/xobject.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace amped3::hang_watch {
namespace {

std::thread g_thread;
std::mutex g_mutex;
std::condition_variable g_cv;
bool g_stop = false;

constexpr uint32_t kCodeLo = 0x82150000, kCodeHi = 0x8284820C;

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

// Return addresses from the stack back-chain: each frame's first word links to
// the caller's frame, which keeps its saved LR 8 bytes below.
std::string BackChain(rex::memory::Memory* memory, uint32_t sp) {
  std::string out;
  for (int depth = 0; depth < 8 && sp >= 0x70000000 && sp < 0x80000000; ++depth) {
    if (!Readable(memory, sp)) {
      break;
    }
    const uint32_t caller_sp = LoadBE32(memory, sp);
    if (caller_sp <= sp || caller_sp >= 0x80000000 || !Readable(memory, caller_sp - 8)) {
      break;
    }
    const uint32_t ret = LoadBE32(memory, caller_sp - 8);
    if (ret >= kCodeLo && ret < kCodeHi) {
      out += fmt::format(" {:08X}", ret);
    }
    sp = caller_sp;
  }
  return out;
}

// Describes r3 when it is a kernel handle (the object a blocked thread waits on).
std::string DescribeHandle(rex::system::KernelState* kernel, uint32_t handle) {
  if (handle < 0xF8000000) {
    return {};
  }
  auto object = kernel->object_table()->LookupObject<rex::system::XObject>(handle);
  if (!object) {
    return " [no object]";
  }
  using Type = rex::system::XObject::Type;
  std::string out = fmt::format(" [type={} '{}'", static_cast<uint32_t>(object->type()), object->name());
  if (object->type() == Type::Thread) {
    out += fmt::format(" tid={:X}", static_cast<rex::system::XThread*>(object.get())->thread_id());
  } else if (object->type() == Type::Event) {
    uint32_t type = 0, state = 0;
    static_cast<rex::system::XEvent*>(object.get())->Query(&type, &state);
    out += fmt::format(" event_type={} signaled={}", type, state);
  }
  return out + "]";
}

void DumpThreads(rex::Runtime* runtime) {
  auto* kernel = runtime->kernel_state();
  auto* memory = runtime->memory();
  auto threads = kernel->object_table()->GetObjectsByType<rex::system::XThread>();
  REXLOG_INFO("HangWatch: {} guest threads", threads.size());
  for (auto& thread : threads) {
    auto* state = thread->thread_state();
    if (!state || !state->context()) {
      continue;
    }
    const auto& ctx = *state->context();
    REXLOG_INFO("HangWatch: tid={:X} '{}' running={} lr={:08X} ctr={:08X} r1={:08X} r3={:08X}{} chain:{}",
                thread->thread_id(), thread->name(), thread->is_running(), uint32_t(ctx.lr),
                ctx.ctr.u32, ctx.r1.u32, ctx.r3.u32, DescribeHandle(kernel, ctx.r3.u32),
                BackChain(memory, ctx.r1.u32));
    // Render thread (sub_82184AE0) blocked on CommitRender: report its quit event,
    // [[r30+4]+92], to tell a lost wake-up from a quit that was never requested.
    // r30 is clobbered inside the wait; the render object is also the global
    // that the shutdown path (sub_821854A8) signals through.
    const uint32_t r30 = LoadBE32(memory, 0x8288F108);
    if (BackChain(memory, ctx.r1.u32).starts_with(" 82184B78") && Readable(memory, r30 + 4)) {
      const uint32_t owner = LoadBE32(memory, r30 + 4);
      if (Readable(memory, owner + 92)) {
        REXLOG_INFO("HangWatch:   render obj={:08X} running={} commit{} quit{}", r30,
                    LoadBE32(memory, r30 + 12), DescribeHandle(kernel, LoadBE32(memory, r30 + 16)),
                    DescribeHandle(kernel, LoadBE32(memory, owner + 92)));
      }
    }
  }
}

}  // namespace

void Start(rex::Runtime* runtime, int interval_seconds) {
  g_thread = std::thread([runtime, interval_seconds] {
    std::unique_lock lock(g_mutex);
    while (!g_cv.wait_for(lock, std::chrono::seconds(interval_seconds), [] { return g_stop; })) {
      DumpThreads(runtime);
    }
  });
}

void Stop() {
  {
    std::lock_guard lock(g_mutex);
    g_stop = true;
  }
  g_cv.notify_all();
  if (g_thread.joinable()) {
    g_thread.join();
  }
}

}  // namespace amped3::hang_watch
