// options_hooks.cpp - The "ReAmped 3" item in the game's own Options screens
//
// The UI package is patched (tools/rui_patch.py) with an item whose A button
// runs $Select(ReAmped3), i.e. the scene command PushScene("ReAmped3"). There
// is no such scene: the PushScene handler opens the ReAmped 3 menu instead.

#include "achievement_progress.h"
#include "game_menu.h"
#include "now_playing.h"
#include "quit_prompt.h"
#include "scripted_input.h"
#include "save_backup.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

REXCVAR_DEFINE_BOOL(scene_log, false, "Debug", "Debug: log the game UI's PushScene commands");
REXCVAR_DEFINE_BOOL(native_menu, true, "UI", "ReAmped 3 in the game's Options opens the game-styled screen (false: the overlay menu)");

REX_EXTERN(__imp__sub_821EC580);

void Amped3_SaveAndResume(int presses);  // save_hooks.cpp
void Amped3_QuickLoadRequest();          // save_hooks.cpp
void Amped3_SaveAndQuit();               // save_hooks.cpp

namespace {
uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, 4);
  return _byteswap_ulong(value);
}

std::string Utf16Be(const std::string& text) {
  std::string out;
  for (char c : text) {
    out.push_back(0);
    out.push_back(c);
  }
  return out;
}

// Guest addresses holding `pattern` (writable guest memory).
std::vector<uint32_t> FindGuestAll(uint8_t* base, const std::string& pattern);

// Where our texts were found: they sit in the loaded string tables (one
// loaded at start for the front end, another when riding), so each text is
// searched near those first (a whole-memory search per text made the pages
// slow to open).
std::mutex g_anchor_mutex;
std::vector<uint32_t> g_text_anchors;

void AddAnchor(uint32_t address) {
  std::lock_guard lock(g_anchor_mutex);
  for (uint32_t a : g_text_anchors) {
    if ((a > address ? a - address : address - a) < (4u << 20)) return;
  }
  if (g_text_anchors.size() < 8) g_text_anchors.push_back(address);
}

std::vector<uint32_t> FindGuestIn(uint8_t* base, const std::string& pattern, uint64_t from, uint64_t to) {
  std::vector<uint32_t> out;
  const auto* pat = reinterpret_cast<const uint8_t*>(pattern.data());
  const std::boyer_moore_horspool_searcher searcher(pat, pat + pattern.size());
  MEMORY_BASIC_INFORMATION mbi;
  for (uint64_t a = from; a < to; a += mbi.RegionSize) {
    if (!VirtualQuery(base + a, &mbi, sizeof(mbi))) break;
    const uint64_t region = uint64_t(static_cast<uint8_t*>(mbi.BaseAddress) - base);
    mbi.RegionSize -= a - region;
    if (mbi.State != MEM_COMMIT || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) continue;
    uint8_t* p = base + a;
    uint8_t* e = base + std::min<uint64_t>(a + mbi.RegionSize, to);
    while ((p = std::search(p, e, searcher)) != e) {
      out.push_back(uint32_t(p - base));
      p += pattern.size();
    }
  }
  return out;
}

// For the hitch log (frame_rate.cpp): whole-memory text searches, and the
// last scene pushed.
std::atomic<uint32_t> g_whole_memory_searches{0};
char g_last_scene[64] = "";

// Every copy of the last text found far off: the texts of a page sit together in its
// string table, so the next one is looked for near those first (the anchors'
// 16 MB windows took ~30 ms a text: 264 ms to open the Options page).
std::mutex g_last_found_mutex;
std::vector<uint32_t> g_last_found;
void SetLastFound(const std::vector<uint32_t>& found) {
  if (found.empty()) return;
  std::lock_guard lock(g_last_found_mutex);
  g_last_found = found;
}

std::vector<uint32_t> FindGuest(uint8_t* base, const std::string& pattern) {
  // Every copy near the last text found and near the anchors (the string
  // table can be in memory more than once: writing only the first copy found
  // may miss the one the scene draws).
  std::vector<uint32_t> out, last_found;
  {
    std::lock_guard lock(g_last_found_mutex);
    last_found = g_last_found;
  }
  constexpr uint64_t kNear = 256 << 10;
  for (uint32_t last : last_found) {
    for (uint32_t a : FindGuestIn(base, pattern, last > kNear ? last - kNear : 0, uint64_t(last) + kNear)) {
      if (std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
    }
  }
  // (Kept as it was: the next text may have a copy near another of them.)
  if (!out.empty()) return out;
  std::vector<uint32_t> anchors;
  {
    std::lock_guard lock(g_anchor_mutex);
    anchors = g_text_anchors;
  }
  constexpr uint64_t kWindow = 8ull << 20;
  for (uint32_t anchor : anchors) {
    const uint64_t from = anchor > kWindow ? anchor - kWindow : 0;
    for (uint32_t a : FindGuestIn(base, pattern, from, uint64_t(anchor) + kWindow)) {
      if (std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
    }
  }
  SetLastFound(out);
  return out;
}

// Not found near: all memory, on a thread of its own (about 1.5 s on a fast
// PC, which froze the game when a page opened), then `done` with what it
// found (on that thread).
void FindGuestAllInBackground(uint8_t* base, std::vector<std::string> patterns,
                              std::function<void(std::vector<uint32_t>)> done) {
  g_whole_memory_searches.fetch_add(1, std::memory_order_relaxed);
  std::thread([base, patterns = std::move(patterns), done = std::move(done)] {
    std::vector<uint32_t> out;
    for (const std::string& pattern : patterns) {
      if (out.empty()) out = FindGuestAll(base, pattern);
    }
    if (REXCVAR_GET(scene_log)) REXLOG_INFO("Amped3: text search: whole memory, in the background ({} found)", out.size());
    SetLastFound(out);
    done(std::move(out));
  }).detach();
}

// Our texts are written from the game's thread and from those searches.
std::recursive_mutex g_text_mutex;

std::vector<uint32_t> FindGuestAll(uint8_t* base, const std::string& pattern) {
  std::vector<uint32_t> out;
  const auto* pat = reinterpret_cast<const uint8_t*>(pattern.data());
  const std::boyer_moore_horspool_searcher searcher(pat, pat + pattern.size());
  for (uint64_t lo : {0x40000000ull, 0x80000000ull, 0xA0000000ull}) {
    MEMORY_BASIC_INFORMATION mbi;
    for (uint64_t a = lo; a < lo + 0x20000000ull; a += mbi.RegionSize) {
      if (!VirtualQuery(base + a, &mbi, sizeof(mbi))) break;
      if (mbi.State != MEM_COMMIT || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) continue;
      const uint64_t start = uint64_t(static_cast<uint8_t*>(mbi.BaseAddress) - base);
      uint8_t* p = base + start;
      uint8_t* e = base + std::min<uint64_t>(start + mbi.RegionSize, lo + 0x20000000ull);
      while ((p = std::search(p, e, searcher)) != e) {
        out.push_back(uint32_t(p - base));
        p += pattern.size();
      }
    }
  }
  return out;
}

// A string table text of the ReAmped 3 scene (tools/rui_patch.py and
// ui_patch.cpp add them, padded with spaces), rewritten in place: the game's UI
// draws string table text as it is when drawn.
class GuestText {
 public:
  GuestText(std::string initial, bool centre = false)
      : initial_(std::move(initial)), last_(initial_), centre_(centre) {}
  // `search`: look for the text if it moved (rows found together by
  // FindRows don't search one by one).
  void Set(uint8_t* base, std::string text, bool search = true) {
    if (centre_ && text.size() < initial_.size()) text.insert(0, (initial_.size() - text.size()) / 2, ' ');
    text.resize(initial_.size(), ' ');
    std::lock_guard lock(g_text_mutex);
    wanted_ = text;
    if (!Valid(base)) {
      addresses_.clear();
      const std::string last = Utf16Be(last_);
      if (search) addresses_ = FindGuest(base, Utf16Be(initial_));
      if (search && addresses_.empty() && last_ != initial_) addresses_ = FindGuest(base, last);
      if (search && addresses_.empty() && !searching_) {
        // Written when found (the scene draws the text as it is then).
        searching_ = true;
        std::vector<std::string> patterns{Utf16Be(initial_)};
        if (last_ != initial_) patterns.push_back(last);
        FindGuestAllInBackground(base, patterns, [this, base, patterns](std::vector<uint32_t> found) {
          std::lock_guard lock(g_text_mutex);
          searching_ = false;
          // Only places that still hold the text (the game may have freed or
          // reused the memory since the search).
          std::erase_if(found, [&](uint32_t a) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(base + a, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return true;
            return std::none_of(patterns.begin(), patterns.end(), [&](const std::string& p) {
              return std::memcmp(base + a, p.data(), p.size()) == 0;
            });
          });
          for (uint32_t a : found) AddAnchor(a);
          if (found.empty() || Valid(base)) return;
          addresses_ = std::move(found);
          Write(base);
        });
      }
    }
    Write(base);
  }
  bool Valid(uint8_t* base) const {
    const std::string last = Utf16Be(last_);
    bool valid = !addresses_.empty();
    for (uint32_t a : addresses_) valid = valid && std::memcmp(base + a, last.data(), last.size()) == 0;
    return valid;
  }
  // Found as its placeholder (the string table was loaded again).
  void Found(std::vector<uint32_t> addresses) {
    addresses_ = std::move(addresses);
    last_ = initial_;
  }

 private:
  void Write(uint8_t* base) {
    const std::string bytes = Utf16Be(wanted_);
    if (REXCVAR_GET(scene_log)) REXLOG_INFO("Amped3: text '{}' at {} place(s)", wanted_, addresses_.size());
    for (uint32_t a : addresses_) std::memcpy(base + a, bytes.data(), bytes.size());
    if (!addresses_.empty()) last_ = wanted_;
  }

  std::string initial_, last_, wanted_;
  bool centre_;
  bool searching_ = false;
  std::vector<uint32_t> addresses_;
};

// Must match the initial texts in ui_patch.cpp kStrings (same lengths).
GuestText g_music_row("Source: Game soundtrack ");
// The value field between the game's arrows is narrow (made for On / Off).
GuestText g_fps_value(" 60 ", true);
GuestText g_aa_value(" SMAA ", true);
// The Display and Effects pages.
GuestText g_ui_size_row("UI size: 100%             ");  // room for "(next start)"
GuestText g_hud_row("HUD: Shown ");
GuestText g_fog_row("Distance fog: On ");
GuestText g_snow_row("Falling snow: On ");
GuestText g_spray_row("Board spray: On ");

void RefreshTexts(uint8_t* base) {
  const std::string music = amped3::game_menu::SettingValue("music");
  const char* m = music == "Your music"               ? "Your music"
                  : music == "Yours and game's, in turn" ? "Taking turns"
                  : music == "Spotify / other player" ? "Spotify / other"
                                                      : "Game soundtrack";
  g_music_row.Set(base, std::string("Source: ") + m);
  const std::string fps = amped3::game_menu::SettingValue("fps");  // "60 FPS" / "Unlimited"
  g_fps_value.Set(base, fps == "Unlimited" ? "Max" : fps.substr(0, fps.find(' ')));
  const std::string aa = amped3::game_menu::SettingValue("aa");
  g_aa_value.Set(base, aa == "FXAA Extreme" ? "FXAA+" : aa);
  g_ui_size_row.Set(base, "UI size: " + amped3::game_menu::SettingValue("uisize"));
  g_hud_row.Set(base, "HUD: " + amped3::game_menu::SettingValue("hud"));
  g_fog_row.Set(base, "Distance fog: " + amped3::game_menu::SettingValue("fog"));
  g_snow_row.Set(base, "Falling snow: " + amped3::game_menu::SettingValue("snow"));
  g_spray_row.Set(base, "Board spray: " + amped3::game_menu::SettingValue("spray"));
}

// The Achievements page's rows (ui_patch.cpp AchievementRowPlaceholder: "#aNN#"
// padded to 30): each achievement's name, then "Got" or its progress.
std::vector<GuestText> MakeRows(char kind, int count) {
  std::vector<GuestText> rows;
  for (int n = 1; n <= count; ++n) {
    std::string text = std::string("#") + kind + std::to_string(n / 10) + std::to_string(n % 10) + "#";
    text.resize(30, ' ');
    rows.emplace_back(text);
  }
  return rows;
}
std::vector<GuestText> g_achievement_rows = MakeRows('a', 15);

// Rows that moved, found with one search for "#<kind>" (near the anchors, or
// in all memory once) instead of one each: opening the Achievements page
// searched 15 times, and all memory for each row not loaded.
// The row number of the "#aNN#" at `a` (2 bytes a character), or -1.
int RowAt(const uint8_t* base, uint32_t a) {
  const uint8_t* p = base + a + 4;
  if (p[0] || p[2] || p[4] || p[1] < '0' || p[1] > '9' || p[3] < '0' || p[3] > '9' || p[5] != '#') return -1;
  return (p[1] - '0') * 10 + (p[3] - '0') - 1;
}
void RefreshRows(uint8_t* base, char kind);

void FindRows(uint8_t* base, char kind, std::vector<GuestText>& rows) {
  std::lock_guard lock(g_text_mutex);
  bool all_valid = true;
  for (const GuestText& row : rows) all_valid = all_valid && row.Valid(base);
  if (all_valid) return;
  const std::string prefix = Utf16Be(std::string("#") + kind);
  std::vector<uint32_t> hits;
  {
    std::vector<uint32_t> anchors;
    {
      std::lock_guard lock(g_anchor_mutex);
      anchors = g_text_anchors;
    }
    constexpr uint64_t kWindow = 8ull << 20;
    for (uint32_t anchor : anchors) {
      const uint64_t from = anchor > kWindow ? anchor - kWindow : 0;
      for (uint32_t a : FindGuestIn(base, prefix, from, uint64_t(anchor) + kWindow)) {
        if (std::find(hits.begin(), hits.end(), a) == hits.end()) hits.push_back(a);
      }
    }
  }
  auto row_at = [base](uint32_t a) { return RowAt(base, a); };
  bool any = false;
  for (uint32_t a : hits) any = any || row_at(a) >= 0;
  if (!any) {
    // All memory in the background, then the page's rows again (found near
    // the new anchor then).
    static std::atomic<bool> searching[2];
    std::atomic<bool>& busy = searching[kind == 'b'];
    if (!busy.exchange(true)) {
      FindGuestAllInBackground(base, {prefix}, [base, kind, &busy](std::vector<uint32_t> found) {
        bool rows_found = false;
        for (uint32_t a : found) {
          if (RowAt(base, a) >= 0) AddAnchor(a), rows_found = true;
        }
        std::lock_guard lock(g_text_mutex);
        busy = false;
        if (rows_found) RefreshRows(base, kind);
      });
    }
    return;
  }
  std::vector<std::vector<uint32_t>> found(rows.size());
  for (uint32_t a : hits) {
    const int n = row_at(a);
    if (n >= 0 && size_t(n) < rows.size()) found[n].push_back(a);
  }
  for (size_t i = 0; i < rows.size(); ++i) {
    if (!rows[i].Valid(base) && !found[i].empty()) rows[i].Found(std::move(found[i]));
  }
}
// The Music page's volume row and the Save backups page's rows ("#b01#"...).
GuestText g_volume_row("Music volume: 100%      ");
std::vector<GuestText> g_backup_rows = MakeRows('b', 10);
constexpr const char* kBackupWords[] = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};

void RefreshMore(uint8_t* base) {
  g_volume_row.Set(base, "Music volume: " + amped3::game_menu::SettingValue("volume"));
}

// Backups are folders named YYYY-MM-DD_HHMMSS; the one to restore at the
// next start is marked.
void RefreshBackups(uint8_t* base) {
  const auto backups = amped3::save_backup::List();
  const std::string scheduled = amped3::save_backup::ScheduledRestore();
  FindRows(base, 'b', g_backup_rows);
  for (size_t i = 0; i < g_backup_rows.size(); ++i) {
    std::string text;
    if (i < backups.size()) {
      const std::string name = backups[i].filename().string();
      text = name.size() == 17 ? name.substr(0, 10) + " " + name.substr(11, 2) + ":" + name.substr(13, 2) : name;
      if (name == scheduled) text += " restore";  // at the next start
    } else if (i == 0) {
      text = "No backups yet";
    }
    g_backup_rows[i].Set(base, text, false);
  }
}

void RefreshAchievements(uint8_t* base) {
  auto* kernel_state = REX_KERNEL_STATE();
  if (!kernel_state) return;
  const auto list = kernel_state->loaded_achievements();
  FindRows(base, 'a', g_achievement_rows);
  for (size_t i = 0; i < g_achievement_rows.size(); ++i) {
    std::string text;
    if (i < list.size()) {
      std::string name = list[i].label;
      if (const size_t at = name.find("\xE2\x80\xA6"); at != std::string::npos) name.replace(at, 3, "...");
      for (const char* drop : {" Merit Badge", "!"}) {
        if (const size_t at = name.find(drop); at != std::string::npos) name.erase(at, std::strlen(drop));
      }
      std::string status = "Locked";
      float fraction;
      std::string detail;
      if (kernel_state->IsAchievementUnlocked(list[i].id)) {
        status = "Got";
      } else if (amped3::achievement_progress::Progress(list[i].label, fraction, detail)) {
        status = std::to_string(int(fraction * 100)) + "%";
      }
      const size_t room = 30 - (status.empty() ? 0 : status.size() + 2);
      if (name.size() > room) name = name.substr(0, room - 1) + ".";
      text = name;
      if (!status.empty()) text += std::string(30 - name.size() - status.size(), ' ').substr(0, 2) + status;
    }
    g_achievement_rows[i].Set(base, text, false);
  }
}

void RefreshRows(uint8_t* base, char kind) {
  if (kind == 'a') RefreshAchievements(base);
  else RefreshBackups(base);
}

// "ReAmped3_<setting>_next" / "_prev", or "ReAmped3_more" (the scene's rows;
// the game's command parser takes letters and _ only).
// Our pages open (above the game's Options page): pushed scenes ReAmped3*,
// popped by the game's PopScene (sub_821EC808).
std::atomic<int> g_page_depth{0};
}  // namespace
// music_hooks.cpp: the D-pad is our pages' (left / right change values), not
// the song change, while one is open.
bool Amped3_OurPageOpen() { return g_page_depth.load() > 0; }
namespace {

void RunCommand(uint8_t* base, const std::string& command) {
  if (command == "load_pause") {
    // LB on the pause screen: quick load (asks for a second press).
    uint32_t raw;
    if (amped3::achievement_progress::ReadStat(0x3e, raw)) Amped3_QuickLoadRequest();
    return;
  }
  if (command == "resume" || command == "resume_pause") {
    // Back in a menu: quick save, then straight back to the game - B for each
    // page down to the pause screen, whose B unpauses (our pages, the Options
    // page and the pause screen; just the pause screen from there). Only with
    // a rider loaded (not the front end).
    uint32_t raw;
    if (amped3::achievement_progress::ReadStat(0x3e, raw)) {
      // B presses back to the ride: each open page, then the pause screen.
      Amped3_SaveAndResume(command == "resume_pause" ? 1 : g_page_depth.exchange(0) + 1);
    }
    return;
  }
  if (command == "more") {
    amped3::game_menu::RequestOpen();
    return;
  }
  if (command.rfind("ach_", 0) == 0) {
    // A on an achievement: its description (and progress) in a banner.
    static const char* kWords[] = {"one", "two", "three", "four", "five", "six", "seven", "eight",
                                   "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen"};
    const auto it = std::find(std::begin(kWords), std::end(kWords), command.substr(4));
    auto* kernel_state = REX_KERNEL_STATE();
    if (it == std::end(kWords) || !kernel_state) return;
    const auto list = kernel_state->loaded_achievements();
    const size_t i = size_t(it - std::begin(kWords));
    if (i >= list.size()) return;
    const bool got = kernel_state->IsAchievementUnlocked(list[i].id);
    std::string text = got || list[i].unachieved_description.empty() ? list[i].description
                                                                     : list[i].unachieved_description;
    float fraction;
    std::string detail;
    if (!got && amped3::achievement_progress::Progress(list[i].label, fraction, detail)) text += "  (" + detail + ")";
    amped3::now_playing::ShowTip(list[i].label, text, got ? "UNLOCKED" : "ACHIEVEMENT", 6.0f);
    return;
  }
  if (command == "skip_next" || command == "skip_prev") {
    amped3::now_playing::Skip(command == "skip_next" ? 1 : -1);
    return;
  }
  if (command == "quit") {
    Amped3_SaveAndQuit();
    return;
  }
  if (command.rfind("backup_", 0) == 0) {
    // Restore this one at the next start (again: cancel).
    const auto it = std::find(std::begin(kBackupWords), std::end(kBackupWords), command.substr(7));
    const auto backups = amped3::save_backup::List();
    const size_t i = size_t(it - std::begin(kBackupWords));
    if (it != std::end(kBackupWords) && i < backups.size()) {
      if (amped3::save_backup::ScheduledRestore() == backups[i].filename().string()) {
        amped3::save_backup::CancelScheduledRestore();
      } else {
        amped3::save_backup::ScheduleRestore(backups[i]);
      }
    }
    RefreshBackups(base);
    return;
  }
  const size_t sep = command.rfind('_');
  if (sep == std::string::npos) return;
  const std::string step = command.substr(sep + 1);
  if (step != "next" && step != "prev") return;
  amped3::game_menu::ChangeSetting(command.substr(0, sep), step == "next" ? 1 : -1);
  RefreshTexts(base);
  RefreshMore(base);
}
}  // namespace

// PushScene(name): r3 = the command call; [r3+16] = its arguments (count at
// +484, array at +480, 48 bytes each, the string's character pointer at +8).
extern "C" REX_FUNC(sub_821EC580) {
  const uint32_t call = Load32(base, ctx.r3.u32 + 16);
  if (call && Load32(base, call + 484) > 0) {
    const uint32_t chars = Load32(base, Load32(base, call + 480) + 8);
    const char* name = chars ? reinterpret_cast<const char*>(base + chars) : "";
    if (REXCVAR_GET(scene_log)) REXLOG_INFO("Amped3: PushScene {}", std::string(name, strnlen(name, 64)));
    std::snprintf(g_last_scene, sizeof(g_last_scene), "%.63s", name);
    const auto started = std::chrono::steady_clock::now();
    struct Timer {
      std::chrono::steady_clock::time_point started;
      const char* name;
      ~Timer() {
        if (REXCVAR_GET(scene_log)) {
          REXLOG_INFO("Amped3: PushScene {} handled in {} ms", std::string(name, strnlen(name, 64)),
                      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        }
      }
    } timer{started, name};
    // Our pages, and the game's own scenes opened from them (Extras is its
    // Global Options, Audio's top row its audio page).
    if ((std::strncmp(name, "ReAmped3", 8) == 0 && name[8] != '_' && std::strcmp(name, "ReAmped3Menu") != 0) ||
        (g_page_depth.load() > 0 &&
         (std::strcmp(name, "GlobalOptionsMenu2") == 0 || std::strcmp(name, "AudioAndMusicOptions2") == 0))) {
      ++g_page_depth;
    }
    if (std::strcmp(name, "ReAmped3") == 0) {
      if (!REXCVAR_GET(native_menu)) {
        amped3::game_menu::RequestOpen();
        ctx.r3.u64 = 0;
        return;
      }
      RefreshTexts(base);  // then the game opens the scene
    } else if (std::strcmp(name, "ReAmped3Menu") == 0) {
      // The front end's Options item (its packages don't hold our scenes).
      amped3::game_menu::RequestOpen();
      ctx.r3.u64 = 0;
      return;
    } else if (std::strcmp(name, "ReAmped3Disp") == 0 || std::strcmp(name, "ReAmped3Fx") == 0) {
      RefreshTexts(base);
    } else if (std::strcmp(name, "ReAmped3Ach") == 0) {
      RefreshAchievements(base);
    } else if (std::strcmp(name, "ReAmped3Mus") == 0) {
      RefreshTexts(base);
      RefreshMore(base);
    } else if (std::strcmp(name, "ReAmped3Bak") == 0) {
      RefreshBackups(base);
    } else if (std::strncmp(name, "ReAmped3_", 9) == 0) {
      RunCommand(base, name + 9);
      ctx.r3.u64 = 0;
      return;
    }
  }
  __imp__sub_821EC580(ctx, base);
}

// The scene manager's push (sub_821B0AF0: r3 = manager, r4 = the name's
// string object, whose first word points at the characters): every scene the
// game opens, also from its own code (help cards), not only Lua commands.
REX_EXTERN(__imp__sub_821B0AF0);
void Amped3_OnScenePush(const char* name);  // save_hooks.cpp (quick load)
extern "C" REX_FUNC(sub_821B0AF0) {
  const uint32_t chars = ctx.r4.u32 ? Load32(base, ctx.r4.u32) : 0;
  char name[64] = {};
  if (chars) std::snprintf(name, sizeof(name), "%.63s", reinterpret_cast<const char*>(base + chars));
  if (REXCVAR_GET(scene_log)) REXLOG_INFO("Amped3: scene push {}", name);
  Amped3_OnScenePush(name);
  __imp__sub_821B0AF0(ctx, base);
}

// PopScene (the scene command; our pages' B and the Options page's back).
REX_EXTERN(__imp__sub_821EC808);
extern "C" REX_FUNC(sub_821EC808) {
  int depth = g_page_depth.load();
  if (REXCVAR_GET(scene_log)) REXLOG_INFO("Amped3: PopScene (our pages {})", depth);
  while (depth > 0 && !g_page_depth.compare_exchange_weak(depth, depth - 1)) {
  }
  __imp__sub_821EC808(ctx, base);
}

// After start and once riding (each loads the string table), find our texts
// in the background, so a ReAmped 3 page opens without a whole-memory search.
// frame_rate.cpp's hitch log: what our hooks did lately.
std::string Amped3_HitchContext() {
  return std::string("last scene ") + (g_last_scene[0] ? g_last_scene : "-") + ", whole-memory text searches " +
         std::to_string(g_whole_memory_searches.exchange(0));
}

void Amped3_PrewarmUiTexts(uint8_t* base) {
  std::thread([base] {
    const auto found = FindGuestAll(base, Utf16Be("Source: Game soundtrack "));
    for (uint32_t a : found) AddAnchor(a);
    SetLastFound(found);
  }).detach();
}

// Debug (scripted "findtext TEXT"): every address of TEXT in guest memory,
// as ASCII and as UTF-16 big-endian.
void Amped3_FindText(const std::string& text) {
  uint8_t* base = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(0);
  std::string out;
  for (uint32_t a : FindGuestAll(base, text)) out += fmt::format(" A:{:08X}", a);
  for (uint32_t a : FindGuestAll(base, Utf16Be(text))) out += fmt::format(" W:{:08X}", a);
  REXLOG_INFO("Amped3: findtext '{}':{}", text, out);
}

// Debug (scripted "dumpmem HEXADDR BYTES"): writes guest memory to dumpmem.bin
// next to the executable.
void Amped3_DumpMem(uint32_t address, uint32_t size) {
  uint8_t* base = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(0);
  const auto path = rex::filesystem::GetExecutableFolder() / "dumpmem.bin";
  if (std::FILE* f = _wfopen(path.c_str(), L"wb")) {
    std::fwrite(base + address, 1, size, f);
    std::fclose(f);
  }
  REXLOG_INFO("Amped3: dumpmem {:08X} {} bytes", address, size);
}

// radio_text.cpp: every address of a byte pattern in guest memory.
std::vector<uint32_t> Amped3_FindGuestAll(const std::string& pattern) {
  return FindGuestAll(REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(0), pattern);
}
