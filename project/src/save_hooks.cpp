// save_hooks.cpp - Quick save: Back in any menu (the pause screen, Options,
// our pages; ui_patch.cpp binds it, the legends say "Back Quick save") saves
// the game and goes straight back to the ride. While riding, Back stays the
// game's own (its tip; resetting a stuck boarder).
//
// The game's own save: sub_821686E8(0x82890288, -, 0), what the game calls
// after its options change (and from 4 other places): it opens the save
// (k:\), writes PlayerData.lua (sub_82169650), PlayerMusic.lua and the
// changed <resort>_Progress.lua (sub_8230DB90), then closes it. Found from
// the save files' names; sub_82168368 (same strings) is the load. It saves
// progress, not the rider's place on the mountain.

#include "achievement_progress.h"
#include "game_menu.h"
#include "now_playing.h"
#include "quit_prompt.h"
#include "scripted_input.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/kernel/xam/private.h>
#include <rex/logging.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

REXCVAR_DEFINE_BOOL(quick_save, true, "Game", "Back in a menu saves the game and returns to it");
REXCVAR_DEFINE_BOOL(save_log, false, "Debug", "Debug: log the game's save functions");
REXCVAR_DEFINE_BOOL(ultrawide_hud_edges, false, "UI",
                    "Ultrawide: move the riding HUD to the screen's edges (experimental: help cards "
                    "and popups get split, as the runtime can't tell their pieces from the HUD's)");
REXCVAR_DEFINE_BOOL(save_min_wait_skip, true, "Game",
                    "Leaving the mountain doesn't freeze for the rest of the console's 3 s minimum "
                    "save time (the save itself takes ~0.2 s here)");

REX_EXTERN(__imp__sub_82168368);
REX_EXTERN(sub_821686E8);
REX_EXTERN(__imp__sub_824A6D08);
REX_EXTERN(__imp__sub_8215C598);
REX_EXTERN(__imp__sub_82490928);  // the game's song change (not music_hooks.cpp's "next")

extern "C" REX_FUNC(sub_82168368) {
  if (REXCVAR_GET(save_log)) {
    REXLOG_INFO("Amped3: save: sub_82168368 r3={:08X} r4={:08X} lr={:08X}", ctx.r3.u32, ctx.r4.u32, uint32_t(ctx.lr));
  }
  __imp__sub_82168368(ctx, base);
  if (REXCVAR_GET(save_log)) REXLOG_INFO("Amped3: save: done, r3={:08X}", ctx.r3.u32);
}

bool Amped3_CinematicPlaying();  // frame_rate.cpp

void Amped3_SaveAndResume(int presses);
void Amped3_QuickLoadRequest();

namespace {

constexpr uint32_t kSaveObject = 0x82890288;  // the game's progress (sub_821686E8's this)

// Frames the rider's data has been loaded (none: the front end, or loading).
uint32_t g_loaded_frames = 0;
// In the game: from the trail map (every way onto the mountain opens it; the
// radio scene is made during the loading screen before it, too early) until
// the front end opens again (the title, Load a Rider, the main menu). (Not
// the game-time statistic: it is readable, and runs, in the front end too.)
// A new rider's first ride, before any map, has no soundtrack anyway.
std::atomic<bool> g_in_game{false};

// Backing out of the menus after a quick save: B presses on the virtual pad,
// then B once more while the game is still paused - its time in
// game (statistic 0x3e) stands still - since a page that saves options on its
// way out can swallow a press.
struct Resume {
  bool active = false;
  int retries = 0;
  bool have_time = false;
  uint32_t time = 0;
  std::chrono::steady_clock::time_point check_at;
} g_resume;

void WatchResume() {
  if (!g_resume.active || std::chrono::steady_clock::now() < g_resume.check_at) return;
  uint32_t time;
  if (!amped3::achievement_progress::ReadStat(0x3e, time)) {
    g_resume.active = false;
    return;
  }
  if (g_resume.have_time && time != g_resume.time) {
    g_resume.active = false;  // the game's clock runs: back in the game
    return;
  }
  // The player pressing something: they're driving the menus now.
  if (amped3::quit_prompt::PadButtons() & ~0x2000u) {
    g_resume.active = false;
    return;
  }
  if (g_resume.have_time) {
    if (++g_resume.retries > 1) {
      g_resume.active = false;
      return;
    }
    REXLOG_INFO("Amped3: quick save: still in a menu, B again");
    amped3::scripted_input::Tap(0x2000, 1);
  }
  g_resume.have_time = true;
  g_resume.time = time;
  g_resume.check_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
}

void QuickLoad(PPCContext& ctx, uint8_t* base) {
  if (g_loaded_frames < 300 || Amped3_CinematicPlaying() || amped3::quit_prompt::IsOpen() ||
      amped3::game_menu::IsOpen() || rex::kernel::xam::xeXamIsUIActive()) {
    REXLOG_INFO("Amped3: quick load skipped (loading, a cinematic or a prompt)");
    return;
  }
  PPCContext saved = ctx;
  ctx.r3.u64 = kSaveObject;
  ctx.r4.u64 = 1;
  ctx.r5.u64 = 0;
  sub_82168368(ctx, base);
  const bool ok = ctx.r3.u32 != 0;
  ctx = saved;
  REXLOG_INFO("Amped3: quick load {}", ok ? "done" : "failed");
  amped3::now_playing::ShowTip(ok ? "Game loaded" : "Load failed", ok ? "Back to your last save" : "Try again",
                               "LOADED", 3.0f);
}

// 1 saved, 0 failed, -1 skipped (not now).
int QuickSave(PPCContext& ctx, uint8_t* base) {
  // Only with a rider loaded for a while (never mid-load: that could save
  // half-loaded data), not in a cinematic, and not under our prompts or a
  // system dialog.
  if (g_loaded_frames < 300 || Amped3_CinematicPlaying() || amped3::quit_prompt::IsOpen() ||
      amped3::game_menu::IsOpen() || rex::kernel::xam::xeXamIsUIActive()) {
    REXLOG_INFO("Amped3: quick save skipped (loading, a cinematic or a prompt)");
    return -1;
  }
  const auto started = std::chrono::steady_clock::now();
  PPCContext saved = ctx;
  ctx.r3.u64 = kSaveObject;
  ctx.r4.u64 = 0;
  ctx.r5.u64 = 0;
  sub_821686E8(ctx, base);
  const bool ok = ctx.r3.u32 != 0;
  ctx = saved;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
  REXLOG_INFO("Amped3: quick save {} ({} ms)", ok ? "done" : "failed", ms.count());
  amped3::now_playing::ShowTip(ok ? "Game saved" : "Save failed", ok ? "Your progress is saved" : "Try again",
                               "SAVED", 3.0f);
  return ok ? 1 : 0;
}

}  // namespace

// The save runs on the game's thread at its next frame (it needs a guest
// context), then the menus are backed out of.
std::atomic<int> g_save_and_resume{0};

// The menu's Quit row: save (if a rider is loaded), then quit.
std::atomic<bool> g_save_and_quit{false};
// A quit whose save failed stays in the game; the next Quit leaves without
// saving. Each quit's 5 s watchdog only acts if it is still the latest.
std::atomic<bool> g_quit_without_save{false};
std::atomic<int> g_quit_request{0};
// quit_prompt.cpp's text: whether Quit saves first (QuickSave's conditions,
// bar the prompt itself, which closes before the save).
bool Amped3_WillSaveOnQuit() {
  return REXCVAR_GET(quick_save) && !g_quit_without_save && g_loaded_frames >= 300 && !Amped3_CinematicPlaying() &&
         !amped3::game_menu::IsOpen();
}

void Amped3_SaveAndQuit() {
  g_save_and_quit = true;
  // If the game's thread doesn't come round (stuck loading), quit anyway.
  const int request = ++g_quit_request;
  std::thread([request] {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    if (g_quit_request != request) return;  // the save failed: staying
    REXLOG_WARN("Amped3: quit: no save after 5 s, quitting");
    amped3::quit_prompt::QuitFromAnyThread();
  }).detach();
}

void Amped3_SaveAndResume(int presses) {
  if (REXCVAR_GET(quick_save)) g_save_and_resume = presses;
}

// Quick load through the game's own path (--quick_load): loading mid-ride
// can restart the story's scene, so instead the mountain is left without
// saving (Pause > Main Menu > Yes, on the virtual pad), the save is loaded at
// the main menu (scene HUB) and Hit the Slopes rebuilds the mountain from it.
enum QuickLoadPhase { kNone, kLeaving, kAtHub, kReturning };
std::atomic<int> g_quick_load_phase{kNone};
std::chrono::steady_clock::time_point g_quick_load_since{};

void Amped3_OnScenePush(const char* name) {
  for (const char* front_end : {"StartScreen", "LoadCharacter", "HUB"}) {
    if (std::strcmp(name, front_end) == 0) g_in_game = false;
  }
  if (std::strcmp(name, "TrailMap") == 0) g_in_game = true;
  if (std::strcmp(name, "HUB") == 0 && g_quick_load_phase == kLeaving) {
    g_quick_load_since = std::chrono::steady_clock::now();
    g_quick_load_phase = kAtHub;
  }
  // Back on the mountain (its map): the cover comes off.
  if (std::strcmp(name, "TrailMap") == 0 && g_quick_load_phase == kReturning) {
    g_quick_load_phase = kNone;
    amped3::now_playing::HideCover();
  }
}

// The game's save (sub_821686E8): skipped while leaving for a quick load.
REX_EXTERN(__imp__sub_821686E8);
extern "C" REX_FUNC(sub_821686E8) {
  if (g_quick_load_phase == kLeaving) {
    REXLOG_INFO("Amped3: quick load: leaving without saving");
    ctx.r3.u64 = 1;
    return;
  }
  if (REXCVAR_GET(save_log)) REXLOG_INFO("Amped3: save: sub_821686E8 r3={:08X} r4={:08X}", ctx.r3.u32, ctx.r4.u32);
  __imp__sub_821686E8(ctx, base);
  if (REXCVAR_GET(save_log)) REXLOG_INFO("Amped3: save: sub_821686E8 -> {:08X}", ctx.r3.u32);
}

// Quick load: LB on the pause screen twice within 3 s (the first press says
// so) loads the last save - the game's own load, sub_82168368 (the rider's
// progress, stats and options back as saved) - and goes back to the ride.
std::atomic<bool> g_load_and_resume{false};
std::chrono::steady_clock::time_point g_load_armed{};

void Amped3_QuickLoadRequest() {
  if (!REXCVAR_GET(quick_save)) return;
  const auto now = std::chrono::steady_clock::now();
  if (now - g_load_armed < std::chrono::seconds(3)) {
    g_load_armed = {};
    if (g_loaded_frames < 300 || Amped3_CinematicPlaying()) return;
    // Pause > Main Menu (the wheel's lower left) > Yes.
    g_quick_load_since = now;
    g_quick_load_phase = kLeaving;
    REXLOG_INFO("Amped3: quick load: leaving the mountain");
    // The menus it goes through stay under a cover: it looks like one load.
    amped3::now_playing::ShowCover("Loading your last save...");
    amped3::scripted_input::TapStick(-0.7f, -0.7f, 0x1000);  // A
    amped3::scripted_input::Tap(0x1000, 1);
    return;
  }
  g_load_armed = now;
  amped3::now_playing::ShowTip("Quick load?", "Press LB again to load your last save", "LOAD", 3.0f);
}

void ResumeGame(int presses) {
  amped3::scripted_input::Tap(0x2000, presses);
  g_resume = {};
  g_resume.active = true;
  g_resume.check_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(830 * presses + 600);
}

// The game's song change from our menu rows (now_playing::Skip), on the
// game's thread at its next frame: 0 next, 2 previous (-1 none).
std::atomic<int> g_game_song_change{-1};
void Amped3_RequestGameSongChange(int delta) { g_game_song_change = delta < 0 ? 2 : 0; }

// On the mountain (its stats loaded), riding or in its menus.
bool Amped3_OnMountain() { return g_loaded_frames > 0; }
// Riding: on the mountain with the game's clock running (not its pause menu,
// loading or the front end).
bool g_riding = false;
bool Amped3_Riding() { return g_riding; }
bool Amped3_InGame() { return g_in_game.load(std::memory_order_relaxed); }

// Once per frame on the game's thread (it calls the frame's swap).
void Amped3_PrewarmUiTexts(uint8_t* base);  // options_hooks.cpp

extern "C" REX_FUNC(sub_824A6D08) {
  static uint32_t frames = 0;
  if (++frames == 600) Amped3_PrewarmUiTexts(base);
  uint32_t raw;
  g_loaded_frames = amped3::achievement_progress::ReadStat(0x3e, raw) ? g_loaded_frames + 1 : 0;
  // Riding (the game's clock running; it stands still in its menus): the
  // ultrawide HUD is clipped to the 16:9 frame (--ultrawide_hud_clip,
  // runtime) or goes to the screen's edges (--ultrawide_hud_pin); menus stay
  // centred, unclipped.
  {
    static uint32_t last_time = 0;
    static int still_frames = 0;
    static bool was_riding = false;
    const bool running = g_loaded_frames > 0 && raw != last_time;
    last_time = raw;
    still_frames = running ? 0 : still_frames + 1;
    const bool riding = g_loaded_frames > 0 && still_frames < 3;  // the clock may skip a frame at high FPS
    g_riding = riding;
    if (riding != was_riding) {
      was_riding = riding;
      rex::cvar::SetFlagByName("ultrawide_hud_clip", riding ? "true" : "false");
      if (REXCVAR_GET(ultrawide_hud_edges)) rex::cvar::SetFlagByName("ultrawide_hud_pin", riding ? "true" : "false");
    }
  }
  if (g_loaded_frames == 240) Amped3_PrewarmUiTexts(base);
  // --user_music=mix: one of the player's tracks ended; the game's next song
  // plays (the runtime turns its music back up, and back down when the game
  // starts the song after it).
  static const auto take_game_turn = [] {
    HMODULE runtime = GetModuleHandleA("rexruntime.dll");
    return runtime ? reinterpret_cast<int32_t (*)()>(GetProcAddress(runtime, "ReXUserMusicTakeGameTurn")) : nullptr;
  }();
  if (const int direction = g_game_song_change.exchange(-1); direction >= 0 && g_loaded_frames > 0) {
    REXLOG_INFO("Amped3: the game's song change ({}) for our menu / radio popup", direction);
    PPCContext saved = ctx;
    ctx.r3.u64 = uint32_t(direction);
    __imp__sub_82490928(ctx, base);
    ctx = saved;
  }
  if (take_game_turn && take_game_turn() && g_loaded_frames > 0) {
    PPCContext saved = ctx;
    ctx.r3.u64 = 0;  // next
    __imp__sub_82490928(ctx, base);
    ctx = saved;
  }
  if (const int presses = g_save_and_resume.exchange(0)) {
    QuickSave(ctx, base);
    ResumeGame(presses);
  }
  if (g_save_and_quit.exchange(false)) {
    const int saved = REXCVAR_GET(quick_save) && !g_quit_without_save ? QuickSave(ctx, base) : -1;
    if (saved == 0) {
      // Not quitting, so nothing is lost; the next Quit leaves without saving.
      ++g_quit_request;
      g_quit_without_save = true;
      REXLOG_WARN("Amped3: quit: the save failed, staying");
      amped3::now_playing::ShowTip("Save failed", "Still here, nothing lost. Quit again to leave without saving.",
                                   "NOT QUITTING", 6.0f);
    } else {
      amped3::quit_prompt::QuitFromAnyThread();
    }
  }
  if (g_quick_load_phase != kNone) {
    const auto waited = std::chrono::steady_clock::now() - g_quick_load_since;
    if (g_quick_load_phase == kAtHub && waited > std::chrono::milliseconds(1500)) {
      g_quick_load_phase = kNone;
      PPCContext saved = ctx;
      ctx.r3.u64 = kSaveObject;
      ctx.r4.u64 = 1;
      ctx.r5.u64 = 0;
      sub_82168368(ctx, base);
      const bool ok = ctx.r3.u32 != 0;
      ctx = saved;
      REXLOG_INFO("Amped3: quick load: save {} at the main menu; Hit the Slopes", ok ? "loaded" : "NOT loaded");
      if (ok) {
        amped3::scripted_input::Tap(0x1000, 1);  // Hit the Slopes (the first item)
        g_quick_load_since = std::chrono::steady_clock::now();
        g_quick_load_phase = kReturning;
      } else {
        amped3::now_playing::HideCover();
      }
    } else if (g_quick_load_phase == kLeaving && waited > std::chrono::seconds(20)) {
      g_quick_load_phase = kNone;  // never left: saving works again
      amped3::now_playing::HideCover();
      REXLOG_WARN("Amped3: quick load: the mountain wasn't left; cancelled");
    } else if (g_quick_load_phase == kReturning && waited > std::chrono::seconds(30)) {
      g_quick_load_phase = kNone;
      amped3::now_playing::HideCover();
    }
  }
  if (g_load_and_resume.exchange(false)) {
    QuickLoad(ctx, base);
    ResumeGame(1);
  }
  WatchResume();
  __imp__sub_824A6D08(ctx, base);
}

// The game's save step when leaving the mountain (sub_8215C598, this = r3):
// starting the save it sets a deadline 3 s ahead (this+32, microseconds), the
// console's minimum time for a save message, and a later call spins on the
// clock until then without drawing a frame: a ~2.7 s freeze here, where the
// save is ~0.2 s. The spin is the deadline's only reader, so clearing it
// before and after each call skips the wait and nothing else (the object's
// setup, sub_8215C3D8, sets a deadline too).
extern "C" REX_FUNC(sub_8215C598) {
  const uint32_t self = ctx.r3.u32;
  const bool skip = REXCVAR_GET(save_min_wait_skip) && self;
  if (skip) std::memset(base + self + 32, 0, 8);
  __imp__sub_8215C598(ctx, base);
  if (skip) std::memset(base + self + 32, 0, 8);
}

// Debug (--save_log): the writers sub_82168368 calls, with the text at r4
// (or at the pointer there) if it's printable.
inline std::string SaveText(uint8_t* base, uint32_t address) {
  auto printable = [&](uint32_t a) {
    if (a < 0x40000000 || a >= 0xA0000000) return std::string();
    std::string text;
    for (uint32_t i = 0; i < 96 && base[a + i]; ++i) {
      if (base[a + i] < 0x20 || base[a + i] > 0x7E) return std::string();
      text += char(base[a + i]);
    }
    return text;
  };
  std::string text = printable(address);
  if (text.size() < 3 && address >= 0x40000000 && address < 0xA0000000) {
    const uint32_t p = uint32_t(base[address]) << 24 | uint32_t(base[address + 1]) << 16 |
                       uint32_t(base[address + 2]) << 8 | base[address + 3];
    text = "->" + printable(p);
  }
  return text;
}
// Debug (--save_log): the writers sub_82168368 calls.
#define AMPED3_SAVE_LOGGED(address)                                                                    \
  REX_EXTERN(__imp__sub_##address);                                                                    \
  extern "C" REX_FUNC(sub_##address) {                                                                 \
    if (REXCVAR_GET(save_log)) REXLOG_INFO("Amped3: save: sub_" #address " r3={:08X} r4={:08X} '{}'", ctx.r3.u32, ctx.r4.u32, \
                                           SaveText(base, ctx.r4.u32)); \
    __imp__sub_##address(ctx, base);                                                                   \
    if (REXCVAR_GET(save_log)) REXLOG_INFO("Amped3: save: sub_" #address " -> {:08X}", ctx.r3.u32);   \
  }
AMPED3_SAVE_LOGGED(8230CB48)
AMPED3_SAVE_LOGGED(821689B0)
AMPED3_SAVE_LOGGED(82169AF0)
AMPED3_SAVE_LOGGED(8216AC08)
AMPED3_SAVE_LOGGED(82167758)
AMPED3_SAVE_LOGGED(82167940)
AMPED3_SAVE_LOGGED(82169650)
AMPED3_SAVE_LOGGED(821672E0)
