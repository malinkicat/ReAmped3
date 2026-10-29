// load_wait_probe.cpp - The loader's dead 1 s wait at loading screens
//
// sub_82167940(r3 = loader, r4 = wait flag) waits up to 1000 ms on the event
// at loader+332 for a read still in flight before cancelling it (loader+336 =
// OVERLAPPED) and closing the event. On the console a read-ahead would still
// be running and signal shortly; the runtime completes every read before
// NtReadFile returns, so nothing is in flight, the event is never signalled
// again and every load stalls for the full second. --loader_skip_dead_wait
// (on by default) clears the wait flag when the event isn't signalled: the
// cancel and result calls that follow then find the read already complete.
// --load_wait_probe=true logs the event and how long the wait took (entry
// hook 0x82167940, after-wait hook 0x8216796C).

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>
#include <rex/system/xobject.h>
#include <rex/system/xthread.h>

#include <chrono>

REXCVAR_DEFINE_BOOL(loader_skip_dead_wait, true, "General",
                    "Skip the loader's 1 s wait for a read that has already completed "
                    "(loading screens stall otherwise)");
REXCVAR_DEFINE_BOOL(load_wait_probe, false, "Debug",
                    "Log the loader's 1 s wait (sub_82167940): the handle and the time taken");

namespace {
thread_local std::chrono::steady_clock::time_point t_start;
thread_local uint32_t t_handle = 0;

uint32_t LoadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
}  // namespace

void Amped3_LoadWaitEntry(PPCRegister& r3, PPCRegister& r4) {
  if (REXCVAR_GET(loader_skip_dead_wait) && (r4.u32 & 0xFF)) {
    const uint32_t handle =
        LoadBE32(REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(r3.u32 + 332));
    auto event = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(handle);
    if (event) {
      uint32_t type = 0, state = 0;
      event->Query(&type, &state);
      if (!state) {
        r4.u64 = 0;
      }
    }
  }
  if (!REXCVAR_GET(load_wait_probe)) return;
  t_start = std::chrono::steady_clock::now();
  t_handle = LoadBE32(REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(r3.u32 + 332));
  std::string what;
  auto object = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XObject>(t_handle);
  if (object) {
    what = fmt::format("type={} '{}'", uint32_t(object->type()), object->name());
    if (object->type() == rex::system::XObject::Type::Event) {
      uint32_t type = 0, state = 0;
      static_cast<rex::system::XEvent*>(object.get())->Query(&type, &state);
      what += fmt::format(" event_type={} signaled={}", type, state);
    } else if (object->type() == rex::system::XObject::Type::Thread) {
      what += fmt::format(" tid={:X}",
                          static_cast<rex::system::XThread*>(object.get())->thread_id());
    }
  } else {
    what = "no object";
  }
  REXLOG_INFO("LoadWait: loader {:08X} wait={} handle {:08X} ({}) lr={:08X}", r3.u32,
              r4.u32 & 0xFF, t_handle, what, 0);
}

void Amped3_LoadWaitDone() {
  if (!REXCVAR_GET(load_wait_probe) || !t_handle) return;
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start)
          .count();
  REXLOG_INFO("LoadWait: handle {:08X} waited {:.1f} ms", t_handle, ms);
  t_handle = 0;
}
