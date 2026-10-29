"""Debug-only ReXGlue v0.10.0 patch: --xma_mute=ADDR silences one XMA context;
--audio_levels=true writes audio_levels.csv in
the working directory: time_s,source,rms for every chunk an XMA context decodes
(source = the context's guest address) and every mixed frame the game submits
(source = mix). Used to see which music streams the game actually mixes in.

usage: patch_sdk_audio_levels.py <rexglue-src>
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])

def patch(rel, pairs):
    path = root / rel
    src = path.read_text()
    if 'audio_levels' in src:
        print(f'{rel}: already patched')
        return
    for old, new in pairs:
        assert src.count(old) == 1, (rel, old)
        src = src.replace(old, new)
    path.write_text(src)
    print(f'{rel}: patched')

patch('src/audio/xma_context.cpp', [
('#include <algorithm>\n#include <cstring>\n', '''#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
'''),
('#include <rex/audio/xma/context.h>\n', '''#include <rex/audio/xma/context.h>
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(audio_levels, false, "Audio",
                    "Debug: log the RMS of decoded XMA chunks and mixed frames to audio_levels.csv");
REXCVAR_DEFINE_STRING(xma_mute, "", "Audio",
                      "Debug: silence the XMA context at this guest address (e.g. FFCA6040)");

namespace rex::audio {
// Debug (--audio_levels): one line per decoded chunk or mixed frame.
void AudioLevelLog(const char* source, double rms) {
  static std::mutex mutex;
  static std::FILE* file = nullptr;
  static const auto start = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex);
  if (!file && !(file = std::fopen("audio_levels.csv", "w"))) {
    return;
  }
  std::fprintf(file, "%.4f,%s,%.6f\\n",
               std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
               source, rms);
}
}  // namespace rex::audio
'''),
('''  output_rb->Write(raw_frame_.data() + (kOutputBytesPerBlock * raw_frame_read_offset),
                   subframes_to_write * kOutputBytesPerBlock);
''', '''  static const std::array<uint8_t, kBytesPerFrameChannel * 2> silence{};
  char context_name[16];
  std::snprintf(context_name, sizeof(context_name), "%08X", guest_ptr());
  const bool muted = !REXCVAR_GET(xma_mute).empty() && REXCVAR_GET(xma_mute) == context_name;
  output_rb->Write((muted ? silence.data() : raw_frame_.data()) +
                       (kOutputBytesPerBlock * raw_frame_read_offset),
                   subframes_to_write * kOutputBytesPerBlock);
  if (REXCVAR_GET(audio_levels)) {
    const uint8_t* pcm = raw_frame_.data() + (kOutputBytesPerBlock * raw_frame_read_offset);
    const size_t samples = subframes_to_write * kOutputBytesPerBlock / 2;
    double sum = 0.0;
    for (size_t i = 0; i < samples; ++i) {
      const double s = int16_t((pcm[2 * i] << 8) | pcm[2 * i + 1]) / 32768.0;
      sum += s * s;
    }
    char source[16];
    std::snprintf(source, sizeof(source), "%08X", guest_ptr());
    AudioLevelLog(source, samples ? std::sqrt(sum / samples) : 0.0);
  }
'''),
])

patch('src/audio/audio_system.cpp', [
('#include <rex/cvar.h>\n', '''#include <rex/cvar.h>

#include <cmath>
#include <cstring>

REXCVAR_DECLARE(bool, audio_levels);
namespace rex::audio {
void AudioLevelLog(const char* source, double rms);  // xma_context.cpp
}  // namespace rex::audio
'''),
('''  auto global_lock = global_critical_region_.Acquire();
  assert_true(index < kMaximumClientCount);
  assert_true(clients_[index].driver != NULL);
  (clients_[index].driver)->SubmitFrame(samples_ptr);''', '''  if (REXCVAR_GET(audio_levels)) {
    // 256 samples x 6 channels of big-endian float.
    const auto* p = memory()->TranslateVirtual<const uint8_t*>(samples_ptr);
    double sum = 0.0;
    for (int i = 0; i < 256 * 6; ++i) {
      uint32_t bits = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) |
                      (uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
      float f;
      std::memcpy(&f, &bits, 4);
      sum += double(f) * f;
    }
    AudioLevelLog("mix", std::sqrt(sum / (256 * 6)));
  }
  auto global_lock = global_critical_region_.Acquire();
  assert_true(index < kMaximumClientCount);
  assert_true(clients_[index].driver != NULL);
  (clients_[index].driver)->SubmitFrame(samples_ptr);'''),
])
