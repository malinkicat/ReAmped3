// step_probe.cpp - Debug probe for code that uses a fixed 1/60 s step
//
// With --step_probe=true, logs every two seconds how often each instruction
// that loads a 1/60 or 1/30 constant runs. A site whose rate grows with the frame rate
// advances game state by 1/60 s per frame, i.e. too fast above 60 FPS.
// Hooks are [[midasm_hook]]s on each load.

#include <rex/cvar.h>
#include <rex/logging.h>

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

REXCVAR_DECLARE(bool, step_probe);

namespace {

constexpr std::array<uint32_t, 27> kSites = {0x821C8DCC, 0x8221B258, 0x8224A7A4, 0x822EB180, 0x8235BBC8, 0x823A7184, 0x823AFCE4, 0x82462928, 0x82526378, 0x8252B644, 0x825551DC, 0x8257CAA8, 0x822553E4, 0x8257F688, 0x82225F68, 0x82226818, 0x82241768, 0x8224A78C, 0x822A7F28, 0x822A8110, 0x8238389C, 0x82455944, 0x824B4348, 0x824F5E50, 0x8253B16C, 0x8253B2B4, 0x82746974};

std::array<std::atomic<uint64_t>, kSites.size()> g_calls{};
std::mutex g_mutex;
std::chrono::steady_clock::time_point g_window_start{};

void Record(size_t index) {
  if (!REXCVAR_GET(step_probe)) {
    return;
  }
  ++g_calls[index];
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard lock(g_mutex);
  if (g_window_start == std::chrono::steady_clock::time_point{}) {
    g_window_start = now;
  }
  const double window = std::chrono::duration<double>(now - g_window_start).count();
  if (window < 2.0) {
    return;
  }
  std::string out;
  for (size_t i = 0; i < kSites.size(); ++i) {
    if (const uint64_t n = g_calls[i].exchange(0)) {
      out += fmt::format(" {:08X}={:.1f}", kSites[i], n / window);
    }
  }
  REXLOG_INFO("StepProbe: calls/s{}", out);
  g_window_start = now;
}

}  // namespace

void Amped3_Step_821C8DCC() { Record(0); }
void Amped3_Step_8221B258() { Record(1); }
void Amped3_Step_8224A7A4() { Record(2); }
void Amped3_Step_822EB180() { Record(3); }
void Amped3_Step_8235BBC8() { Record(4); }
void Amped3_Step_823A7184() { Record(5); }
void Amped3_Step_823AFCE4() { Record(6); }
void Amped3_Step_82462928() { Record(7); }
void Amped3_Step_82526378() { Record(8); }
void Amped3_Step_8252B644() { Record(9); }
void Amped3_Step_825551DC() { Record(10); }
void Amped3_Step_8257CAA8() { Record(11); }
void Amped3_Step_822553E4() { Record(12); }
void Amped3_Step_8257F688() { Record(13); }
void Amped3_Step_82225F68() { Record(14); }
void Amped3_Step_82226818() { Record(15); }
void Amped3_Step_82241768() { Record(16); }
void Amped3_Step_8224A78C() { Record(17); }
void Amped3_Step_822A7F28() { Record(18); }
void Amped3_Step_822A8110() { Record(19); }
void Amped3_Step_8238389C() { Record(20); }
void Amped3_Step_82455944() { Record(21); }
void Amped3_Step_824B4348() { Record(22); }
void Amped3_Step_824F5E50() { Record(23); }
void Amped3_Step_8253B16C() { Record(24); }
void Amped3_Step_8253B2B4() { Record(25); }
void Amped3_Step_82746974() { Record(26); }
