// render_shutdown_fix.cpp - Fix a lost wake-up when the game stops its render thread
//
// The render thread loops: wait(CommitRender); render; exit if quit is signaled.
// To stop it, sub_821854A8 clears the running flag, signals CommitRender and
// joins (sub_8228D2F8), which signals quit before waiting. If the render thread
// consumes CommitRender and checks quit before quit is set, it waits on
// CommitRender forever and the join deadlocks (seen on the legal screen in
// most runs). Called just before the join's wait, this signals CommitRender
// once more so the render thread wakes, sees quit and exits.

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>
#include <rex/system/xmemory.h>

namespace {

constexpr uint32_t kRenderObjectGlobal = 0x8288F108;

uint32_t LoadBE32(uint32_t address) {
  const auto* p = REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(address);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

}  // namespace

// [[midasm_hook]] at 0x8228D4A0 (sub_8228D2F8's wait for the joined thread); r30 is the
// thread object being joined.
void Amped3_BeforeThreadJoin(PPCRegister& r30) {
  const uint32_t object = r30.u32;
  if (object == 0 || object != LoadBE32(kRenderObjectGlobal) || LoadBE32(object + 12) != 0) {
    return;  // Not the render thread being stopped.
  }
  const uint32_t commit_handle = LoadBE32(object + 16);
  auto commit = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(commit_handle);
  if (commit) {
    commit->Set(0, false);
    REXLOG_DEBUG("Amped3: re-signaled CommitRender {:08X} before joining the render thread",
                 commit_handle);
  }
}
