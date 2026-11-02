// ai_rand_probe.cpp - Test only (--ai_rand_probe): how often the riders' AI
// functions that draw from the game's own random generator (an inlined
// 1664525 LCG, its state at 0x82881648) run, per second. If they run once a
// frame, the other riders pick (and try) tricks twice as often at 60 FPS as
// at 30: a candidate for their wipeouts above 30 FPS. Logs calls/s each
// second while riding; no effect on the game.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

REXCVAR_DEFINE_BOOL(ai_rand_probe, false, "Debug",
                    "Log how often the AI functions using the game's random generator run (calls/s)");

namespace {
constexpr int kCount = 5;
const char* const kNames[kCount] = {"82398440", "823894E8", "82389D88", "82388E58", "82382270"};
std::atomic<uint32_t> g_calls[kCount];

// The chance function (sub_8225C620: a random number in [0,1) under a
// probability) and the riders' mode setter (sub_82388610), counted by caller
// (the return address).
std::mutex g_by_caller_mutex;
std::map<uint32_t, uint32_t> g_chance_by_caller, g_mode_by_caller;
void NoteCaller(std::map<uint32_t, uint32_t>& by_caller, uint32_t lr) {
  if (!REXCVAR_GET(ai_rand_probe)) return;
  std::lock_guard lock(g_by_caller_mutex);
  ++by_caller[lr];
}
std::string TopCallers(std::map<uint32_t, uint32_t>& by_caller) {
  std::vector<std::pair<uint32_t, uint32_t>> v(by_caller.begin(), by_caller.end());
  std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
  std::string out;
  for (size_t k = 0; k < v.size() && k < 6; ++k) out += fmt::format(" {:08X}:{}", v[k].first, v[k].second);
  by_caller.clear();
  return out;
}

void Note(int i) {
  if (!REXCVAR_GET(ai_rand_probe)) return;
  if (i >= 0) g_calls[i].fetch_add(1, std::memory_order_relaxed);
  using Clock = std::chrono::steady_clock;
  static std::atomic<int64_t> last{0};
  const int64_t now = Clock::now().time_since_epoch().count();
  int64_t before = last.load();
  if (now - before < int64_t(std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(1)).count())) return;
  if (!last.compare_exchange_strong(before, now)) return;
  uint32_t c[kCount];
  for (int k = 0; k < kCount; ++k) c[k] = g_calls[k].exchange(0);
  REXLOG_INFO("ai_rand_probe: calls/s {} {} | {} {} | {} {} | {} {} | {} {}", kNames[0], c[0], kNames[1], c[1],
              kNames[2], c[2], kNames[3], c[3], kNames[4], c[4]);
  std::lock_guard lock(g_by_caller_mutex);
  REXLOG_INFO("ai_rand_probe: chance/s by caller{}", TopCallers(g_chance_by_caller));
  REXLOG_INFO("ai_rand_probe: mode set/s by caller{}", TopCallers(g_mode_by_caller));
}
}  // namespace

REX_EXTERN(__imp__sub_82398440);
extern "C" REX_FUNC(sub_82398440) {
  Note(0);
  __imp__sub_82398440(ctx, base);
}
REX_EXTERN(__imp__sub_823894E8);
extern "C" REX_FUNC(sub_823894E8) {
  Note(1);
  __imp__sub_823894E8(ctx, base);
}
REX_EXTERN(__imp__sub_82389D88);
extern "C" REX_FUNC(sub_82389D88) {
  Note(2);
  __imp__sub_82389D88(ctx, base);
}
REX_EXTERN(__imp__sub_82388E58);
extern "C" REX_FUNC(sub_82388E58) {
  Note(3);
  __imp__sub_82388E58(ctx, base);
}
REX_EXTERN(__imp__sub_82382270);
extern "C" REX_FUNC(sub_82382270) {
  Note(4);
  __imp__sub_82382270(ctx, base);
}
REX_EXTERN(__imp__sub_8225C620);
extern "C" REX_FUNC(sub_8225C620) {
  NoteCaller(g_chance_by_caller, uint32_t(ctx.lr));
  __imp__sub_8225C620(ctx, base);
}
REX_EXTERN(__imp__sub_82388610);
extern "C" REX_FUNC(sub_82388610) {
  NoteCaller(g_mode_by_caller, uint32_t(ctx.lr));
  Note(-1);  // keeps the once-a-second log going (not counted)
  __imp__sub_82388610(ctx, base);
}
