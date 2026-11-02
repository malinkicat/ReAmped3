// achievement_progress.cpp - Progress towards the game's achievements, from
// the game's own statistics (the "Your Statistics" screen's values), with
// popups at milestones
//
// The statistics screen's table (0x8287BA04: label, statistic (-1: a heading),
// denominator statistic (-2: none), format; 16 bytes each; --stats_log dumps
// it) names them; the values live in an
// array of 56-byte records at [[[0x8288F0A8] + 56] + 12], the value at +0
// (an int, or a float for distances).
//
// Milestones (from our research notes):
// the first progress, then 10%, 50% and 90%, each shown once ever (a ledger in
// the user data folder, outside the saves, so restoring a backup doesn't
// repeat them). What's already passed when a save is loaded is recorded
// silently. At most one progress card every 30 s, only the highest new
// milestone, never while the ReAmped 3 menu or the quit prompt is up, and
// unlocks go first (the toast queue). --achievement_progress=milestones | all
// | off.

#include "achievement_progress.h"

#include "achievement_toast.h"
#include "game_menu.h"
#include "quit_prompt.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

REXCVAR_DEFINE_STRING(achievement_progress, "milestones", "Achievements",
                      "Achievement progress popups: milestones (first, 10%, 50%, 90%), all (every "
                      "change, at most every 30 s), or off")
    .allowed({"milestones", "all", "off"});
REXCVAR_DEFINE_BOOL(stats_log, false, "Debug", "Debug: log some of the game's statistics every 10 s");
REXCVAR_DEFINE_STRING(stats_poke, "", "Debug",
                      "Debug: set a statistic, \"index=value\" (hex index, e.g. 35=21), every 20 s of "
                      "play (increasing by the value each time) - to test progress popups");
REXCVAR_DECLARE(std::string, user_data_root);

namespace amped3::achievement_progress {
namespace {

uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, 4);
  return _byteswap_ulong(value);
}

const uint8_t* GuestBase() {
  auto* kernel_state = REX_KERNEL_STATE();
  return kernel_state ? kernel_state->memory()->TranslateVirtual<const uint8_t*>(0) : nullptr;
}

// The statistics array (guest address), 0 when there's no profile yet.
uint32_t StatsArray(const uint8_t* base) {
  const uint32_t holder = Load32(base, 0x8288F0A8);
  if (!holder) return 0;
  const uint32_t container = Load32(base, holder + 56);
  return container ? Load32(base, container + 12) : 0;
}

enum class Unit { kCount, kKilometres };

// The achievements with progress: the statistic and its target (a fixed
// number, or another statistic, the screen's denominator).
struct Tracked {
  const char* name;  // as in the game's achievement list
  uint32_t stat;
  float target;          // if target_stat is 0
  uint32_t target_stat;  // the total, from the statistics
  Unit unit;
  const char* what;  // "objects", "gold medals"
};
constexpr Tracked kTracked[] = {
    {"Park Designer Merit Badge", 0x35, 200.0f, 0, Unit::kCount, "objects"},
    {"Snowmobiler Merit Badge", 0x91, 10000.0f, 0, Unit::kKilometres, "km"},
    {"Lesser Snow God", 0x23, 0.0f, 0x27, Unit::kCount, "medals"},
    {"Supreme Snow God", 0x25, 0.0f, 0x29, Unit::kCount, "gold medals"},
};
constexpr float kMilestones[] = {0.1f, 0.5f, 0.9f};

std::filesystem::path LedgerPath() {
  const std::string root = REXCVAR_GET(user_data_root);
  return (root.empty() ? rex::filesystem::GetUserFolder() / "amped3" : std::filesystem::path(root)) /
         "achievements" / "progress_shown.txt";
}

struct State {
  bool loaded = false;
  std::set<std::string> shown;  // "<name>|<milestone>"
  bool baselined[std::size(kTracked)] = {};
  float last_value[std::size(kTracked)] = {};
  std::chrono::steady_clock::time_point last_card{};
  uint32_t array = 0;  // the profile's statistics, to notice a profile change
};
State g;

void LoadLedger() {
  if (g.loaded) return;
  g.loaded = true;
  std::ifstream in(LedgerPath());
  for (std::string line; std::getline(in, line);) {
    if (!line.empty()) g.shown.insert(line);
  }
}

void Remember(const std::string& key) {
  if (!g.shown.insert(key).second) return;
  std::error_code ec;
  std::filesystem::create_directories(LedgerPath().parent_path(), ec);
  std::ofstream(LedgerPath(), std::ios::app) << key << "\n";
}

float StatValue(const uint8_t* base, uint32_t array, uint32_t index, Unit unit) {
  const uint32_t raw = Load32(base, array + index * 56);
  if (unit == Unit::kKilometres) {
    float metres;
    std::memcpy(&metres, &raw, 4);
    return std::isfinite(metres) ? metres : 0.0f;
  }
  return float(int32_t(raw));
}

std::string Detail(const Tracked& t, float value, float target) {
  if (t.unit == Unit::kKilometres) {
    return fmt::format("{:.1f} / {:.0f} {}", value / 1000.0f, target / 1000.0f, t.what);
  }
  return fmt::format("{} / {} {}", int(value), int(target), t.what);
}

void LogStats(const uint8_t* base, uint32_t array) {
  static uint32_t frames = 0;
  if (++frames % 600) return;
  static const struct {
    const char* name;
    uint32_t index;
  } kLogged[] = {
      {"objects planted", 0x35}, {"snowmobile distance", 0x91}, {"stoked areas", 0x56},
      {"stoked areas total", 0x57}, {"story", 0x2a}, {"story total", 0x2b},
      {"bronze medals", 0x23}, {"bronze total", 0x27}, {"gold medals", 0x25}, {"gold total", 0x29},
      {"challenge points", 0x22}, {"challenge total", 0x26}, {"tricks", 0x34}, {"time in game", 0x3e},
  };
  if (!array) {
    REXLOG_INFO("Amped3: stats: no profile yet");
    return;
  }
  // Once: the Your Statistics screen's table (label, statistic or -1 for a
  // heading, denominator statistic or -2, format; 16 bytes each) with the
  // values, to find more counters.
  static bool dumped = false;
  if (!dumped) {
    dumped = true;
    for (uint32_t e = 0x8287BA04; e < 0x8287BA04 + 16 * 300; e += 16) {
      const uint32_t label = Load32(base, e), index = Load32(base, e + 4), denominator = Load32(base, e + 8);
      if (label < 0x82000000 || label > 0x83000000) break;
      const char* text = reinterpret_cast<const char*>(base + label);
      std::string value;
      if (index < 0x400) {
        const uint32_t raw = Load32(base, array + index * 56);
        float f;
        std::memcpy(&f, &raw, 4);
        value = fmt::format("{} ({:g})", int32_t(raw), f);
      }
      REXLOG_INFO("Amped3: stats table: {:X} / {:X} fmt {} {} = {}", int32_t(index), int32_t(denominator),
                  Load32(base, e + 12), std::string(text, strnlen(text, 64)), value);
    }
  }
  std::string line;
  for (const auto& stat : kLogged) {
    const uint32_t raw = Load32(base, array + stat.index * 56);
    float f;
    std::memcpy(&f, &raw, 4);
    line += fmt::format(" | {} {} ({:g})", stat.name, int32_t(raw), f);
  }
  REXLOG_INFO("Amped3: stats:{}", line);
}

}  // namespace

bool ReadStat(uint32_t index, uint32_t& raw) {
  const uint8_t* base = GuestBase();
  if (!base) return false;
  const uint32_t array = StatsArray(base);
  if (!array) return false;
  raw = Load32(base, array + index * 56);
  return true;
}

bool Progress(const std::string& name, float& fraction, std::string& detail) {
  const uint8_t* base = GuestBase();
  if (!base) return false;
  const uint32_t array = StatsArray(base);
  if (!array) return false;
  for (const Tracked& t : kTracked) {
    if (name != t.name) continue;
    const float value = StatValue(base, array, t.stat, t.unit);
    const float target = t.target_stat ? StatValue(base, array, t.target_stat, Unit::kCount) : t.target;
    if (target <= 0.0f) return false;
    fraction = std::clamp(value / target, 0.0f, 1.0f);
    detail = Detail(t, value, target);
    return true;
  }
  return false;
}

void OnFrame() {
  static uint32_t frames = 0;
  ++frames;
  const uint8_t* base = GuestBase();
  if (!base) return;
  const uint32_t array = StatsArray(base);
  if (REXCVAR_GET(stats_log)) LogStats(base, array);
  if (!REXCVAR_GET(stats_poke).empty() && array && frames % 1200 == 0) {
    const std::string& poke = REXCVAR_GET(stats_poke);
    const size_t eq = poke.find('=');
    if (eq != std::string::npos) {
      const uint32_t index = uint32_t(std::stoul(poke.substr(0, eq), nullptr, 16));
      const int32_t add = std::stoi(poke.substr(eq + 1));
      auto* p = const_cast<uint8_t*>(base) + array + index * 56;
      uint32_t raw;
      std::memcpy(&raw, p, 4);
      raw = _byteswap_ulong(uint32_t(int32_t(_byteswap_ulong(raw)) + add));
      std::memcpy(p, &raw, 4);
      REXLOG_INFO("Amped3: stats: statistic {:X} += {}", index, add);
    }
  }
  if (frames % 60 || REXCVAR_GET(achievement_progress) == "off" || !array) return;  // about once a second
  const bool all = REXCVAR_GET(achievement_progress) == "all";
  auto* kernel_state = REX_KERNEL_STATE();
  LoadLedger();
  if (array != g.array) {
    // Another profile (or the first): what it has already passed is not news.
    g.array = array;
    std::fill(std::begin(g.baselined), std::end(g.baselined), false);
  }
  const auto now = std::chrono::steady_clock::now();
  const bool can_show = !game_menu::IsShowing() && !quit_prompt::IsOpen() &&
                        now - g.last_card >= std::chrono::seconds(30);
  const auto achievements = kernel_state->loaded_achievements();
  for (size_t i = 0; i < std::size(kTracked); ++i) {
    const Tracked& t = kTracked[i];
    const auto it = std::find_if(achievements.begin(), achievements.end(),
                                 [&](const auto& a) { return a.label == t.name; });
    if (it == achievements.end() || kernel_state->IsAchievementUnlocked(it->id)) continue;
    const float value = StatValue(base, array, t.stat, t.unit);
    const float target = t.target_stat ? StatValue(base, array, t.target_stat, Unit::kCount) : t.target;
    if (target <= 0.0f || value < 0.0f) continue;
    const float fraction = std::clamp(value / target, 0.0f, 1.0f);
    // The milestones passed: "first" (any progress), then 10 / 50 / 90 %.
    std::vector<std::string> passed;
    if (value > 0.0f) passed.push_back(std::string(t.name) + "|first");
    for (float m : kMilestones) {
      if (fraction >= m) passed.push_back(fmt::format("{}|{}", t.name, int(m * 100)));
    }
    if (!g.baselined[i]) {
      g.baselined[i] = true;
      g.last_value[i] = value;
      for (const auto& key : passed) Remember(key);
      continue;
    }
    bool news = false;
    for (const auto& key : passed) news |= !g.shown.count(key);
    if (all) news |= value > g.last_value[i];
    if (!news || !can_show || fraction >= 1.0f) continue;
    for (const auto& key : passed) Remember(key);
    g.last_value[i] = value;
    g.last_card = now;
    achievement_toast::PushProgress(t.name, Detail(t, value, target), fraction);
    break;  // one card at a time
  }
}

}  // namespace amped3::achievement_progress
