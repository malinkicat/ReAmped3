// music_ramp_fix.cpp - Don't drop fade requests that arrive during a fade
//
// Overlapping music ("Loading Music" left playing next to the next song).
// The sound engine fades an instance through a parameter ramp at instance+108,
// set by sub_82423238(instance, request). While a ramp is active (+40 == 2) it
// ignores the new request's target, duration and completion action and only
// re-derives the step toward the old target. The music state machine starts
// "Loading Music" with a ~3.75 s fade-in and, when loading ends, asks to fade it
// out and stop it (action 1). On the console loading outlasted the fade-in; here
// it takes ~3 s, the stop lands mid-fade-in and is dropped, and the loading music
// plays on forever (paused music never finishes its ramp either).
//
// With --music_ramp_fix (default on), a request that arrives during an active
// ramp retargets it from its current value: the new duration, target, owner id
// and action are copied in before the original code re-derives the step.
//
// Request: +8 id, +12 duration, +16 start, +20 target, +24 action.
// Ramp:    +4 duration, +8 start, +12 target, +16 elapsed, +20 current,
//          +24 step, +32 id, +36 action (1 = stop when done), +40 state (2 = active).
// Hook is a [[midasm_hook]] at the entry of sub_82423238.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

REXCVAR_DECLARE(bool, music_ramp_fix);

namespace {

uint8_t* Guest(uint32_t address) {
  return REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(address);
}

uint32_t Load32(uint32_t address) {
  const uint8_t* p = Guest(address);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void Store32(uint32_t address, uint32_t value) {
  uint8_t* p = Guest(address);
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

}  // namespace

// [[midasm_hook]] at sub_82423238 (r3 = sound instance, r4 = ramp request).
void Amped3_RampRequest(PPCRegister& r3, PPCRegister& r4) {
  if (!REXCVAR_GET(music_ramp_fix)) {
    return;
  }
  const uint32_t ramp = r3.u32 + 108, request = r4.u32;
  if (Load32(ramp + 40) != 2) {
    return;  // No active ramp: the original code applies the request.
  }
  Store32(ramp + 4, Load32(request + 12));   // duration
  Store32(ramp + 12, Load32(request + 20));  // target
  Store32(ramp + 16, 0);                     // elapsed
  Store32(ramp + 32, Load32(request + 8));   // owner id
  Store32(ramp + 36, Load32(request + 24));  // action when done
  REXLOG_DEBUG("Amped3: ramp retargeted mid-fade (instance {:08X}, action {})", r3.u32,
               Load32(request + 24));
}
