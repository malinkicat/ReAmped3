// amped3 - ReXGlue Recompiled Project

#include "generated/default/amped3_init.h"

#include <rex/cvar.h>

REXCVAR_DEFINE_INT32(framerate, 60, "Video", "Target frame rate: 30 (original), 60, any other cap (e.g. 120, 141 for a 144 Hz VRR display), or 0 for unlimited");
REXCVAR_DEFINE_BOOL(music_ramp_fix, true, "Audio", "Apply fade requests that arrive during a fade (fixes loading music left playing next to the next song)");
REXCVAR_DEFINE_BOOL(show_fps, false, "Video", "Show the frame-time graph at startup (F10 toggles it)");
REXCVAR_DEFINE_BOOL(pix_gpu_capture, false, "Debug", "Load PIX's GPU capturer at startup; the scripted command \"pixcapture\" captures the next frame");
REXCVAR_DEFINE_STRING(gpu_backend, "any", "Video", "GPU backend: any (D3D12 on Windows), d3d12 or vulkan (needs a runtime built with Vulkan)");
REXCVAR_DEFINE_BOOL(profile_pad_combo, false, "Debug", "Holding both sticks in (L3 + R3) for a second records a 10 s CPU profile (cpu_profile.csv)");
REXCVAR_DEFINE_BOOL(dump_image, false, "Debug", "Write the loaded XEX image to image.bin next to the executable");
REXCVAR_DEFINE_INT32(hang_watch, 0, "Debug", "Every N seconds, log where each guest thread is (0 = off)");
REXCVAR_DEFINE_BOOL(fps_log, false, "Debug", "Log frames per second and frame times every two seconds");
REXCVAR_DEFINE_BOOL(frame_times, false, "Debug", "Write every frame time to frametimes.csv next to the executable");
REXCVAR_DEFINE_BOOL(hitch_profile, false, "Debug", "After a frame longer than 70 ms, log where guest threads spent it");
REXCVAR_DEFINE_BOOL(rider_timers, false, "Debug", "Log each reset of the riders' state timers with how long it ran (compare frame rates)");
REXCVAR_DEFINE_INT32(rider_dump, 0, "Debug", "Frames of rider state to dump to rider_dump.bin on the scripted \"riderdump\" command (0 = off)");
REXCVAR_DEFINE_BOOL(rider_probe, false, "Debug", "Log each rider's position and speed every second");
REXCVAR_DEFINE_BOOL(voice_probe, false, "Debug", "Log calls into XAudio2 source-voice methods");
REXCVAR_DEFINE_BOOL(step_probe, false, "Debug", "Log how often code using a fixed 1/60 s step runs");
REXCVAR_DEFINE_BOOL(clock_probe, false, "Debug", "Log how the game stopwatch reads advance (finding the simulation clock)");
REXCVAR_DEFINE_BOOL(scripted_input, false, "Debug", "Play back input_script.txt next to the executable as controller presses");

#include "amped3_app.h"

REX_DEFINE_APP(amped3, Amped3App::Create)

#ifdef AMPED3_PGO_GEN
extern "C" int __llvm_profile_write_file(void);
#endif

namespace amped3 {
// The scripted "pgodump" command: with a -fprofile-instr-generate build
// (tools/build_pgo.cmd), writes the profile counters (the process is usually
// killed, so they're never written at exit).
void WritePgoProfile() {
#ifdef AMPED3_PGO_GEN
  REXLOG_INFO("Amped3: PGO profile written ({})", __llvm_profile_write_file());
#else
  REXLOG_WARN("Amped3: pgodump needs an instrumented build (tools/build_pgo.cmd)");
#endif
}
}  // namespace amped3
