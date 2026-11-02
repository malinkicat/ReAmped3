// cloth_probe.cpp - Debug probe: the flapping clothes animation
//
// "FlappingClothes::UpdateAsync" (sub_8222DB18) is a one-shot job on the
// game's timed job queue: it runs once per rider and adds the "windy" and
// "noWind" clips to the rider's animation mixer (job [+4], clip list at its
// [+8]). With --cloth_dump=N, the scripted "clothdump" command samples 256 KB
// around the clip list N times, 10 ms apart, to cloth_dump.bin. Used to show the cloth bones move at
// the same speed at 30, 60 and 120 FPS.

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

REXCVAR_DEFINE_INT32(cloth_dump, 0, "Debug",
                     "Samples (10 ms apart) of the flapping clothes objects to dump on the "
                     "scripted \"clothdump\" command (0 = off)");

namespace {
std::atomic<bool> g_requested{false};
std::atomic<uint32_t> g_jobs{0}, g_job{0};

uint32_t LoadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
}  // namespace

namespace amped3::cloth_probe {
void Request() {
  const uint32_t job = g_job.load();
  const int samples = REXCVAR_GET(cloth_dump);
  if (!job || samples <= 0 || g_requested.exchange(true)) return;
  REXLOG_INFO("ClothProbe: sampling {:08X} ({} samples)", job, samples);
  std::thread([job, samples] {
    std::FILE* file = _wfopen((rex::filesystem::GetExecutableFolder() / "cloth_dump.bin").c_str(), L"wb");
    if (!file) return;
    auto* memory = REX_KERNEL_MEMORY();
    const auto start = std::chrono::steady_clock::now();
    // Record: f32 seconds, then 256 KB of memory from 128 KB below the clip
    // list of the mixer the clips are added to (job [+4], its [+8]).
    const auto* j = memory->TranslateVirtual<const uint8_t*>(job);
    const uint32_t list = LoadBE32(memory->TranslateVirtual<const uint8_t*>(LoadBE32(j + 4)) + 8);
    REXLOG_INFO("ClothProbe: clip list {:08X}", list);
    const auto* window = memory->TranslateVirtual<const uint8_t*>(list - 0x20000);
    for (int i = 0; i < samples; ++i) {
      const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
      std::fwrite(&t, 4, 1, file);
      std::fwrite(window, 1, 0x40000, file);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::fclose(file);
    g_requested = false;
    REXLOG_INFO("ClothProbe: wrote cloth_dump.bin");
  }).detach();
}
}  // namespace amped3::cloth_probe

// [[midasm_hook]] at 0x8222DB18, the flapping clothes job's entry. It runs
// each frame until the clips are in, then adds them to the rider's animation
// and is done.
// The clothdump request then samples the job object and its [+4], [+8]
// objects every 5 ms from a host thread (cloth_dump samples), to compare
// their clocks across a frame rate switch.
void Amped3_ClothJob(PPCRegister& r3) {
  if (REXCVAR_GET(cloth_dump) <= 0) return;
  g_jobs.fetch_add(1, std::memory_order_relaxed);
  if (g_job.exchange(r3.u32) != r3.u32) REXLOG_INFO("ClothProbe: job runs (object {:08X})", r3.u32);
}
