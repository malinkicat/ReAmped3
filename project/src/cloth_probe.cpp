// cloth_probe.cpp - Debug probe: the flapping clothes animation job
//
// The job "FlappingClothes::UpdateAsync" (sub_8222DB18, r3 = job: [+4] and
// [+8] point at the animation objects it drives, blending the "windy" and
// "noWind" clips). With --cloth_dump=N, the scripted "clothdump" command
// records N consecutive calls of the first job seen: 1 KB of each object
// ([+4], [+8]) to cloth_dump.bin, to find a clock that advances per call
// instead of per second (compare dumps at two frame rates).

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

REXCVAR_DEFINE_INT32(cloth_dump, 0, "Debug",
                     "Calls of the flapping clothes job to dump on the scripted \"clothdump\" "
                     "command (0 = off)");

namespace {
std::atomic<bool> g_requested{false};

uint32_t LoadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
}  // namespace

namespace amped3::cloth_probe {
void Request() {
  g_requested = true;
  REXLOG_INFO("ClothProbe: dump requested");
}
}  // namespace amped3::cloth_probe

// [[midasm_hook]] at 0x8222DB18, the flapping clothes job's entry.
void Amped3_ClothJob(PPCRegister& r3) {
  static std::FILE* file = nullptr;
  static uint32_t job = 0, calls = 0;
  static std::chrono::steady_clock::time_point start;
  if (!g_requested.load(std::memory_order_relaxed) || REXCVAR_GET(cloth_dump) <= 0) {
    return;
  }
  if (!file) {
    file = _wfopen((rex::filesystem::GetExecutableFolder() / "cloth_dump.bin").c_str(), L"wb");
    job = r3.u32;
    start = std::chrono::steady_clock::now();
    if (!file) {
      g_requested = false;
      return;
    }
  }
  if (r3.u32 != job) {
    return;
  }
  auto* memory = REX_KERNEL_MEMORY();
  const auto* j = memory->TranslateVirtual<const uint8_t*>(job);
  // Record: f32 seconds since the first call, then [+4] and [+8], 1 KB each.
  const float t =
      std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
  std::fwrite(&t, 4, 1, file);
  for (uint32_t field : {4u, 8u}) {
    static uint8_t bytes[1024];
    std::memset(bytes, 0, sizeof(bytes));
    const uint32_t object = LoadBE32(j + field);
    if (object) {
      std::memcpy(bytes, memory->TranslateVirtual<const uint8_t*>(object), sizeof(bytes));
    }
    std::fwrite(bytes, 1, sizeof(bytes), file);
  }
  if (++calls >= uint32_t(REXCVAR_GET(cloth_dump))) {
    std::fclose(file);
    file = nullptr;
    g_requested = false;
    calls = 0;
    REXLOG_INFO("ClothProbe: wrote cloth_dump.bin");
  }
}
