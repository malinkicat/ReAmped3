// The launcher's screen: drawn with Dear ImGui's draw lists on SDL's renderer,
// driven by a controller, the keyboard or the mouse. One main screen (game
// folder, resolution, frame rate, toggles, image, Quit / RIDE), a folder
// picker and the vendor package screen (license, then download). Look and layout: the "Night session" theme of the earlier WPF
// launcher, whose starting layout came from Table-Tennis-Rockstar-PC
// (github.com/davidptrssvea/Table-Tennis-Rockstar-PC, MIT; see
// THIRD_PARTY_NOTICES.md).
#include <SDL3/SDL.h>

#include <algorithm>
#include <map>
#include <atomic>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "logic.h"
#include "system.h"
#include "ui_pack.h"
#include "../../src/title_logo.h"
#include "vendor.h"
#include "stb_image.h"  // the implementation is in ui_pack.cpp

using namespace launcher;

namespace {

constexpr ImU32 Rgb(unsigned rgb, unsigned a = 255) {
  return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, a);
}
constexpr ImU32 kNavy = Rgb(0x0F1724);
constexpr ImU32 kHeaderTop = Rgb(0x1C2E48);
constexpr ImU32 kField = Rgb(0x18253A);
constexpr ImU32 kBorder = Rgb(0x263852);
constexpr ImU32 kCyan = Rgb(0x5FE1F0);
constexpr ImU32 kMuted = Rgb(0x9DB2CB);
constexpr ImU32 kText = Rgb(0xE6EEF6);
constexpr ImU32 kPillText = Rgb(0xC6D4E4);
constexpr ImU32 kPlain = Rgb(0x1C2A40);
constexpr ImU32 kGo = Rgb(0xFF7A29);
constexpr ImU32 kGoHover = Rgb(0xFF8F4D);
constexpr ImU32 kError = Rgb(0xFF6B6B);
constexpr ImU32 kWhite = Rgb(0xFFFFFF);

// The layout is in design units (the WPF window: 560 wide), scaled to the
// window.
constexpr float kDesignW = 560.0f;
constexpr float kDesignH = 706.0f;
constexpr float kLeft = 26.0f;
constexpr float kRight = kDesignW - 26.0f;
// Steam Deck (and other wide, small screens): two columns, so everything is
// drawn larger. The right column holds what is below the frame rate in the
// single-column layout, moved up beside it.
constexpr float kDeckDesignW = 880.0f;
constexpr float kDeckDesignH = 548.0f;
constexpr float kDeckHeader = 52.0f;    // extra header height for the big logo
constexpr float kDeckColumn2 = 424.0f;  // right column x offset
constexpr float kDeckRaise = 264.0f;    // right column y offset
constexpr float kWideControlsH = 610.0f;  // the Controls page's height on a wide window

enum class Act { Up, Down, Left, Right, A, B, X, Start, PageUp, PageDown, Cancel };

struct Rect {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool Contains(ImVec2 p) const { return p.x >= x0 && p.x < x1 && p.y >= y0 && p.y < y1; }
};

struct PickerEntry {
  std::string label;
  std::string path;  // "" with up = the parent
  bool up = false;
  bool has_xex = false;
};

// A vendor package being offered: its license (fetched first, from the same
// source as its files), then the download, on a worker thread.
struct VendorJob {
  enum class Phase { kLicenseLoading, kLicense, kDownloading };
  const VendorPackage* package = nullptr;
  Phase phase = Phase::kLicenseLoading;
  std::thread thread;
  std::atomic<bool> finished{false};
  std::atomic<bool> cancel{false};
  std::atomic<unsigned long long> done{0};
  unsigned long long total = 0;
  bool ok = false;         // set by the thread before `finished`
  std::string error;       // likewise
  std::string license;     // likewise (the license text)
  std::vector<std::string> lines;  // the license, wrapped
  float wrap_w = 0.0f;
  int first_line = 0;
  ~VendorJob() {
    cancel = true;
    if (thread.joinable()) thread.join();
  }
};

// Held D-pad / stick direction with auto-repeat.
struct Repeat {
  int dir = -1;  // Act::Up..Right, -1 none
  uint64_t next = 0;
};

struct App {
  // Where each focusable item was drawn in the last frame (window pixels), for
  // moving the focus by direction.
  std::map<Item, Rect> rects;
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  ImFont* body = nullptr;
  ImFont* display = nullptr;
  float s = 1.0f;   // design unit -> window pixels
  float ox = 0.0f;  // left edge of the content column
  float oy = 0.0f;  // design units added to every y (Deck layout columns)
  float L = kLeft, Rr = kRight;  // the current column's edges
  float fk = 1.0f;  // text size factor (larger on the Deck)
  bool two_columns = false;
  SDL_Texture* logo = nullptr;  // logo.bmp next to the launcher, if present
  float logo_aspect = 1.0f;
  float width = 0.0f;

  bool deck = false;
  bool wine = false;
  std::string dist_dir;
  std::string version;  // version.txt from the package (build date and commit)
  bool settings_exist = false;
  Settings st;
  std::vector<int> fps_choices;
  DisplayInfo screen;
  // The Resolution drop-down: its choices (ResolutionChoices), and the open
  // list's highlighted row and first shown row.
  std::vector<std::string> res_choices;
  bool res_open = false;
  bool drawing_res_list = false;  // while the list is open only it takes the mouse
  int res_sel = 0;
  int res_first = 0;
  int res_opened_frame = 0;
  Item focus = Item::Resolution;
  std::string status;
  bool status_error = false;
  bool pad_hints = false;

  bool picker = false;
  bool picker_music = false;  // choosing the music folder, not the game's
  // The Controls page: controller buttons remapped (st.pad_map); the row
  // after the buttons resets them.
  bool controls = false;
  // The UI sizes page: a row per kind (UiSizeKinds).
  bool sizes = false;
  int size_sel = 0;
  int ctl_sel = 0;
  // Its keyboard view: an action per row; `capturing` waits for the key.
  bool ctl_keyboard = false;
  int key_sel = 0;
  bool capturing = false;
  // The controller view: `pad_capturing` waits for the button to use for the
  // selected game button.
  bool pad_capturing = false;
  // The header's logo made from the game's own (textures\launcher_logo.png).
  // Set by the logo thread (detached: shared, so it may finish after the launcher).
  std::shared_ptr<std::atomic<bool>> logo_made = std::make_shared<std::atomic<bool>>(false);
  bool logo_started = false;
  bool logo_from_game = false;
  bool window_resized = false;  // by the player: its size is remembered
  std::string picker_dir;
  std::vector<PickerEntry> entries;
  int sel = 0;
  int first_row = 0;
  bool picker_error = false;

  // NVIDIA DLAA: an RTX card, and the DLSS package (fetched on request).
  bool rtx = false;
  bool dlss_installed = false;
  // The game folder's version (IsTestedGameVersion), checked once per folder.
  std::string version_path;
  bool version_tested = false;
  std::unique_ptr<VendorJob> job;

  // The sharp UI pack (ui_pack.h), made in the background when it's missing
  // or out of date for the chosen game folder.
  struct PackJob {
    uipack::Progress progress;
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    bool ok = false;
    std::string error;
    std::thread thread;
    uint64_t started_ms = 0;
    ~PackJob() {
      cancel = true;
      if (thread.joinable()) thread.join();
    }
  };
  std::unique_ptr<PackJob> pack;
  std::string pack_checked;  // the game folder the pack was last checked for
  std::string pack_message;  // shown when idle: the last result

  bool launching = false;
  bool ride_at_start = false;  // --ride (testing): RIDE with the saved settings on the first frame
  uint64_t ride_at_ms = 0;     // --ride_after N (testing): ... after N seconds, as a player would
  sys::Process game;
  uint64_t launch_deadline = 0;
  uint64_t next_poll = 0;
  bool quit = false;

  std::vector<SDL_Gamepad*> pads;
  Repeat dpad;
  Repeat stick;
  std::vector<Act> acts;
  bool mouse_moved = false;
  // Pointer travel since start-up, ignoring the first second: gamescope (Steam
  // Deck Gaming Mode) parks the hidden pointer mid-screen at start, which must
  // not pull the focus off RIDE.
  float mouse_travel = 0.0f;
  uint64_t mouse_ignore_until = 0;
};

void SinglePassChanged(App& a);  // below

// ---- small helpers -------------------------------------------------------

ImVec2 P(const App& a, float x, float y) { return ImVec2(a.ox + x * a.s, (y + a.oy) * a.s); }
Rect R(const App& a, float x0, float y0, float x1, float y1) {
  return {a.ox + x0 * a.s, (y0 + a.oy) * a.s, a.ox + x1 * a.s, (y1 + a.oy) * a.s};
}
ImDrawList* DL() { return ImGui::GetBackgroundDrawList(); }

// Text width in design units, measured at the drawn size.
float TextW(const App& a, ImFont* f, float size, const std::string& t) {
  return f->CalcTextSizeA(size * a.fk * a.s, FLT_MAX, 0.0f, t.c_str()).x / a.s;
}

void Text(App& a, ImFont* f, float size, float x, float y, ImU32 col, const std::string& t, float wrap = 0.0f) {
  DL()->AddText(f, size * a.fk * a.s, P(a, x, y), col, t.c_str(), nullptr, wrap * a.s);
}
// Semi-bold look for small labels (only Regular and ExtraBold Italic ship).
void BoldText(App& a, float size, float x, float y, ImU32 col, const std::string& t) {
  ImVec2 p = P(a, x, y);
  DL()->AddText(a.body, size * a.fk * a.s, p, col, t.c_str());
  DL()->AddText(a.body, size * a.fk * a.s, ImVec2(p.x + std::max(0.6f, 0.45f * a.s), p.y), col, t.c_str());
}
void Label(App& a, float y, const std::string& t) { BoldText(a, 11.0f, a.L + 2, y, kMuted, t); }

void Box(const Rect& r, ImU32 fill, ImU32 border, float rounding, float s) {
  DL()->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), fill, rounding * s);
  if (border) DL()->AddRect(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), border, rounding * s, std::max(1.0f, s));
}
// The focus ring: ice cyan, just outside the item.
void Ring(const Rect& r, float s) {
  float g = 3.0f * s;
  DL()->AddRect(ImVec2(r.x0 - g, r.y0 - g), ImVec2(r.x1 + g, r.y1 + g), kCyan, 8.0f * s, 2.0f * s);
}
void Chevron(ImVec2 c, float size, bool right, ImU32 col, float s) {
  float h = size, w = size * 0.55f, d = right ? 1.0f : -1.0f;
  DL()->AddLine(ImVec2(c.x - d * w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + d * w * 0.5f, c.y), col, 1.8f * s);
  DL()->AddLine(ImVec2(c.x + d * w * 0.5f, c.y), ImVec2(c.x - d * w * 0.5f, c.y + h * 0.5f), col, 1.8f * s);
}
void Tick(ImVec2 o, float size, ImU32 col, float s) {
  // M3,8 L6.5,11.5 L13,4.5 on a 16 px box.
  float k = size / 16.0f;
  ImVec2 pts[3] = {ImVec2(o.x + 3 * k, o.y + 8 * k), ImVec2(o.x + 6.5f * k, o.y + 11.5f * k), ImVec2(o.x + 13 * k, o.y + 4.5f * k)};
  DL()->AddPolyline(pts, 3, col, 2.0f * s);
}

// Drops the first UTF-8 character (for shortening paths from the left).
void DropFirstChar(std::string& t) {
  size_t n = 1;
  while (n < t.size() && (static_cast<unsigned char>(t[n]) & 0xC0) == 0x80) ++n;
  t.erase(0, n);
}

bool Hover(const App& a, const Rect& r) {
  return !a.launching && (!a.res_open || a.drawing_res_list) && r.Contains(ImGui::GetIO().MousePos);
}
bool Clicked(const App& a, const Rect& r) { return Hover(a, r) && ImGui::IsMouseClicked(ImGuiMouseButton_Left); }

std::string FpsLabel(const App& a, int v) {
  if (v == kAutoFps) return a.screen.hz > 0 ? "Auto: match the display (" + std::to_string(a.screen.hz) + " Hz)" : "Auto: match the display";
  if (v == 0) return "Unlimited";
  if (v == kCustomFps) return "Custom";
  if (v == kFrameGenFps) {
    const int hz = a.screen.hz > 61 ? a.screen.hz : 60;
    return "Frame generation: " + std::to_string(hz / 2) + " shown as " + std::to_string(hz) + " FPS (experimental)";
  }
  if (v == 30) return "30 FPS (original)";
  return std::to_string(v) + " FPS";
}
// A size's shape, e.g. "16:9", "21:9" (empty when unusual).
std::string AspectName(int w, int h) {
  const double r = double(w) / h;
  struct Named { double ratio; const char* name; };
  static const Named kNames[] = {{16.0 / 9, "16:9"}, {16.0 / 10, "16:10"}, {4.0 / 3, "4:3"}, {5.0 / 4, "5:4"},
                                 {21.0 / 9, "21:9"}, {32.0 / 9, "32:9"}, {3.0 / 2, "3:2"}};
  for (const Named& n : kNames) {
    if (std::abs(r - n.ratio) < 0.03 || (n.ratio > 2.3 && n.ratio < 2.4 && r > 2.3 && r < 2.42)) return n.name;
  }
  return {};
}

std::string ResLabel(const App& a, const std::string& v) {
  if (int w = 0, h = 0; ParseSize(v, w, h)) {
    const std::string aspect = AspectName(w, h);
    const bool current = w == a.screen.width && h == a.screen.height;
    return std::to_string(w) + " x " + std::to_string(h) + (aspect.empty() ? "" : "  (" + aspect + ")") +
           (current ? "  - your display's current setting" : "");
  }
  if (v == "720p") return "720p";
  if (v == "1080p") return "1080p";
  if (v == "1440p") return "1440p";
  if (v == "4k") return "4K";
  if (a.screen.width > 0) {
    return "Match the display (" + std::to_string(a.screen.width) + " x " + std::to_string(a.screen.height) + ")";
  }
  return "Match the display";
}
// With single-pass rendering off the game draws with its own 2x MSAA, so the
// choices are that alone or a filter on top of it.
std::string AALabel(const std::string& v, bool single_pass) {
  if (!single_pass) {
    if (v == "fxaa") return "Anti-aliasing: MSAA 2x + FXAA";
    if (v == "smaa") return "Anti-aliasing: MSAA 2x + SMAA";
    return "Anti-aliasing: MSAA 2x (the game's own)";
  }
  if (v == "fxaa") return "Anti-aliasing: FXAA";
  if (v == "smaa") return "Anti-aliasing: SMAA (sharp)";
  if (v == "dlaa") return "Anti-aliasing: NVIDIA DLAA (RTX)";
  return "Anti-aliasing: none";
}
std::string MusicLabel(const std::string& v) {
  if (v == "mine") return "Music: your music files";
  if (v == "system") return "Music: your streaming app, e.g. Spotify (experimental)";
  return "Music: the game's soundtrack";
}
const char* kSharpenHint = "AMD CAS sharpening of the final picture. Off: shown as drawn.";
const char* kReampedLogoHint =
    "The title screen and the start-up loading screen show the ReAmped 3 logo instead of Amped 3's (made from "
    "your game files). Off: the game's own logo.";

// The player's music files' folder (the music folder next to the game unless
// they chose one).
std::string MusicDir(const App& a) {
  return a.st.music_dir.empty() ? JoinPath(TrimPath(a.dist_dir), "music") : a.st.music_dir;
}

// What the focused setting does: a heading and a paragraph for the left
// column (two columns); empty for the items that keep the sharp UI panel.
std::pair<std::string, std::string> Explain(App& a) {
  switch (a.focus) {
    case Item::GameFolder: {
      if (!GameFolderValid(a.st.game_path)) {
        if (!TrimPath(a.st.game_path).empty() && sys::FileExists(JoinPath(TrimPath(a.st.game_path), "default.xex"))) {
          return {"GAME FOLDER", "This folder has default.xex but not the rest of the game (" +
                                     MissingGameData(a.st.game_path) +
                                     " is missing): extract the whole disc again and choose that folder."};
        }
        return {"GAME FOLDER", "Choose the folder that holds default.xex (Browse)."};
      }
      if (a.version_path != a.st.game_path) {
        a.version_path = a.st.game_path;
        a.version_tested = IsTestedGameVersion(a.st.game_path);
      }
      return {"GAME FOLDER",
              a.version_tested ? "This is Amped 3 (USA / NTSC-U), the version ReAmped 3 is made from and tested with."
                               : "This isn't the USA (NTSC-U) version ReAmped 3 is made from. It should still work, "
                                 "as only the game's data files are read from here, but it hasn't been tested."};
    }
    case Item::Fullscreen:
      return {"FULLSCREEN", "On: the game fills the display. Off: it plays in a window you can move."};
    case Item::ReampedLogo: return {"REAMPED LOGO", kReampedLogoHint};
    case Item::SkipIntro:
      return {"SKIP INTRO MOVIES", "On: the game starts without the 2K and Indie Built movies, straight to the loading "
                                   "screen and the title. Off: they play first, as on the Xbox 360."};
    case Item::Tearing:
      return {"V-SYNC", a.deck ? "On: no tearing. Off: slightly less input lag, but the picture can tear."
                               : "On: no tearing. Off: slightly less input lag, but the picture can tear (not on a "
                                 "G-Sync / FreeSync display while the frame rate stays under its refresh rate)."};
    case Item::SinglePass:
      return {"FAST SINGLE-PASS RENDERING",
              "The Xbox 360 drew each frame in three strips (tiles) to fit its small video memory, so the scene was "
              "drawn three times. On: drawn once, which more than doubles the frame rate in busy scenes and keeps the "
              "Steam Deck near 60 FPS in the busiest spots; SMAA smooths the edges. Off: the original "
              "three-strip drawing with the game's own 2x MSAA anti-aliasing, much slower."};
    case Item::AA:
      if (!a.st.single_pass) {
        return {"ANTI-ALIASING",
                "Single-pass rendering is off, so the game's own 2x MSAA is on. SMAA or FXAA can be added on top "
                "for smoother edges still. Turn single-pass back on for more than double the frame rate (SMAA "
                "then replaces the MSAA)."};
      }
      return {"ANTI-ALIASING",
              "Smooths jagged edges. SMAA: sharp, clean edges, small cost (recommended). FXAA: the cheapest, a little "
              "softer. None: the picture as drawn (fine at 4K). The game's own 2x MSAA comes with single-pass "
              "rendering off."};
    case Item::Scaling:
      return {"SHARPENING",
              "AMD FidelityFX CAS sharpens the final picture, bringing back detail that anti-aliasing and scaling "
              "soften. 30% is a light touch; raise it if the picture looks soft. Off: shown as drawn."};
    case Item::Music: {
      std::string text;
      const std::string folder = " Folder: " + MusicDir(a) + " (A / Enter: choose another).";
      if (a.st.music == "mine") {
        text = "Your own music files (mp3, ogg, flac, wav; subfolders too) play instead of the game's songs. Next "
               "track: D-pad." + folder;
      } else if (a.st.music == "system") {
        text = "Experimental. Spotify or another streaming / music app (YouTube Music, Tidal, a browser...) plays "
               "instead of the game's songs: start it, and the game's music steps aside while it plays. D-pad skips "
               "its tracks. If nothing is playing when you ride and Spotify is installed, it's started for you. "
               "Apps that don't report to the system's media controls aren't followed.";
      } else {
        text = "The game's own soundtrack, as on the Xbox 360.";
      }
      return {"MUSIC", text + " Change the volume in the game's Options > Audio."};
    }
    case Item::Controls:
      return {"CONTROLS", "Change which controller buttons and keyboard keys do what."};
    case Item::UiSize:
      return {"MENUS AND HUD SIZE",
              std::string("100% is the size the game was made for, at any resolution; smaller leaves more of "
                          "the picture free. Each part shrinks towards its own corner or edge. A / Enter: set the "
                          "menus, HUD, help tips, radio popup and button legends separately") +
                  (a.deck ? " (on the Deck they can also go up to 150%, easier to read on its screen)"
                          : "") +
                  ". Applies when the game starts."};
    case Item::Save: return {"SAVE", "Saves these settings now (RIDE and Quit save them too)."};
    default: return {};
  }
}

std::string SharpenLabel(int v) {
  return v == 0 ? "Sharpening: off" : "Sharpening: " + std::to_string(v) + "%";
}

template <typename T>
void Step(const std::vector<T>& list, T& value, int delta, bool wrap) {
  auto it = std::find(list.begin(), list.end(), value);
  int i = it == list.end() ? 0 : int(it - list.begin());
  int n = int(list.size());
  i += delta;
  i = wrap ? (i % n + n) % n : std::clamp(i, 0, n - 1);
  value = list[i];
}

bool FolderValid(const App& a) { return GameFolderValid(a.st.game_path); }

bool SaveSettings(App& a) {
  int w = 0, h = 0;
  if (a.window && !a.deck && SDL_GetWindowSize(a.window, &w, &h) && a.window_resized) a.st.launcher_height = h;
  return sys::WriteFile(a.dist_dir + "settings.ini", FormatSettings(a.st));
}

// The Save button: says whether it worked (e.g. a read-only install folder).
void SaveSettingsAndSay(App& a) {
  a.status_error = !SaveSettings(a);
  a.status = a.status_error ? "Could not save settings.ini (is the folder read-only?)" : "Settings saved";
}

// ---- folder picker -----------------------------------------------------

// dir and select_path by value: they may point into a.entries, which this rebuilds.
void LoadPicker(App& a, std::string dir, std::string select_path = "") {
  a.picker_dir = dir;
  a.entries.clear();
  a.picker_error = false;
  if (dir.empty()) {
    if (a.wine) {
      std::string home = WineHomeDir(sys::EnvVar("HOME"), sys::EnvVar("USER"));
      if (sys::DirExists(home)) a.entries.push_back({"Home (" + home + ")", home});
    }
    for (const std::string& d : sys::Drives()) a.entries.push_back({d, d});
  } else {
    a.entries.push_back({"..", "", true});
    std::vector<std::string> names;
    a.picker_error = !sys::ListFolders(dir, names);
    for (size_t i = 0; i < names.size(); ++i) {
      PickerEntry e{names[i], JoinPath(dir, names[i])};
      if (i < 400) e.has_xex = sys::FileExists(JoinPath(e.path, "default.xex"));
      a.entries.push_back(e);
    }
  }
  a.sel = a.entries.size() > 1 && !dir.empty() ? 1 : 0;
  for (size_t i = 0; i < a.entries.size(); ++i) {
    if (!select_path.empty() && a.entries[i].path == select_path) a.sel = int(i);
  }
  a.first_row = 0;
}

void OpenPicker(App& a, bool music = false) {
  a.picker_music = music;
  std::string start;
  std::string cur = TrimPath(music ? MusicDir(a) : a.st.game_path);
  while (!cur.empty() && !sys::DirExists(cur)) cur = ParentDir(cur);
  if (!cur.empty()) start = cur;
  else if (a.wine) {
    start = WineHomeDir(sys::EnvVar("HOME"), sys::EnvVar("USER"));
    if (!sys::DirExists(start)) start = "";
  }
  a.picker = true;
  LoadPicker(a, start);
}

void PickerUp(App& a) {
  if (a.picker_dir.empty()) {
    a.picker = false;  // B at the drive list leaves the picker
    a.focus = a.picker_music ? Item::Music : Item::GameFolder;
    return;
  }
  std::string from = a.picker_dir;
  LoadPicker(a, ParentDir(a.picker_dir), from);
}

void PickerOpen(App& a, int i) {
  if (i < 0 || i >= int(a.entries.size())) return;
  if (a.entries[i].up) PickerUp(a);
  else LoadPicker(a, a.entries[i].path);
}

void PickerChoose(App& a) {
  if (a.picker_dir.empty()) return;
  a.picker = false;
  a.status.clear();
  if (a.picker_music) {
    a.st.music_dir = a.picker_dir;
    a.focus = Item::Music;
    return;
  }
  a.st.game_path = a.picker_dir;
  a.focus = InitialFocus(a.settings_exist, FolderValid(a));
}

// ---- vendor packages ---------------------------------------------------

void OfferVendor(App& a, const std::string& id) {
  const VendorPackage* p = FindVendorPackage(id);
  if (!p || a.job) return;
  a.job = std::make_unique<VendorJob>();
  VendorJob& job = *a.job;
  job.package = p;
  const std::string dist = a.dist_dir;
  job.thread = std::thread([&job, p, dist] {
    job.ok = vendor::Fetch(*p, p->license, dist, [&job](unsigned long long, unsigned long long) { return !job.cancel; },
                           job.error) &&
             sys::ReadFile(VendorDir(dist, p->id) + p->license.name, job.license);
    if (job.ok && job.license.empty()) job.error = "the license is empty", job.ok = false;
    job.finished = true;
  });
}

// The package's files, after the player accepted its license.
void StartVendorDownload(App& a) {
  VendorJob& job = *a.job;
  if (job.thread.joinable()) job.thread.join();
  job.finished = false;
  job.phase = VendorJob::Phase::kDownloading;
  job.total = 0;
  for (const VendorFile& f : job.package->files) job.total += f.size;
  const std::string dist = a.dist_dir;
  job.thread = std::thread([&job, dist] {
    unsigned long long before = 0;
    job.ok = true;
    for (const VendorFile& f : job.package->files) {
      job.ok = vendor::Fetch(*job.package, f, dist, [&job, before](unsigned long long done, unsigned long long) {
        job.done = before + done;
        return !job.cancel;
      }, job.error);
      if (!job.ok) break;
      before += f.size;
    }
    job.finished = true;
  });
}

// Back to the main screen. An optional package that isn't there just leaves
// its feature off (DLAA -> SMAA); RIDE always works.
void CloseVendor(App& a, bool installed, const std::string& message) {
  const VendorPackage* p = a.job->package;
  a.job.reset();
  if (p->id == "dlss") {
    a.dlss_installed = installed;
    a.st.aa = UsableAA(a.st.aa, installed);
  }
  a.status = message;
  a.status_error = !installed && !message.empty();
}

// Each frame: the worker's result.
void PollVendor(App& a) {
  if (!a.job || !a.job->finished) return;
  VendorJob& job = *a.job;
  const std::string name = job.package->name;
  if (job.phase == VendorJob::Phase::kLicenseLoading) {
    if (job.ok) {
      if (job.thread.joinable()) job.thread.join();
      job.phase = VendorJob::Phase::kLicense;
    } else {
      CloseVendor(a, false, job.cancel ? "" : "Couldn't get " + name + " (" + job.error + "); playing without it");
    }
  } else if (job.phase == VendorJob::Phase::kDownloading) {
    if (job.ok) CloseVendor(a, true, name + " installed");
    else if (job.cancel) CloseVendor(a, false, name + " download cancelled; playing without it");
    else CloseVendor(a, false, name + " download failed (" + job.error + "); playing without it");
  }
}

void SkipVendor(App& a) {
  if (!a.job) return;
  const std::string name = a.job->package->name;
  if (a.job->phase == VendorJob::Phase::kDownloading) {
    a.job->cancel = true;  // PollVendor reports it when the thread stops
    return;
  }
  // Not asked again until the player picks it again (the setting falls back).
  CloseVendor(a, false, "");
  a.status = name + " skipped; pick DLAA again to get it";
  a.status_error = false;
}

void HandleVendor(App& a, Act act) {
  VendorJob& job = *a.job;
  const int page = 20;
  switch (act) {
    case Act::Up: job.first_line = std::max(0, job.first_line - 1); break;
    case Act::Down: job.first_line = std::min(std::max(0, int(job.lines.size()) - 1), job.first_line + 1); break;
    case Act::PageUp: case Act::Left: job.first_line = std::max(0, job.first_line - page); break;
    case Act::PageDown: case Act::Right:
      job.first_line = std::min(std::max(0, int(job.lines.size()) - 1), job.first_line + page);
      break;
    case Act::A: case Act::Start:
      if (job.phase == VendorJob::Phase::kLicense) StartVendorDownload(a);
      break;
    case Act::B: case Act::Cancel: SkipVendor(a); break;
    default: break;
  }
}

// ---- launching ---------------------------------------------------------

void Launch(App& a) {
  if (!sys::CpuHasAvx2()) {
    a.status = "This PC's processor doesn't have AVX2, which ReAmped 3 needs (most CPUs from 2013 on do)";
    a.status_error = true;
    return;
  }
  if (!RideEnabled(FolderValid(a))) {
    a.status = "Choose your Amped 3 folder";
    a.status_error = true;
    a.focus = Item::GameFolder;
    return;
  }
  // Only a package marked required may hold back RIDE (none are today).
  for (const VendorPackage& p : VendorPackages()) {
    if (p.required && vendor::Available(p) && !vendor::Installed(p, a.dist_dir)) {
      OfferVendor(a, p.id);
      a.status = p.name + " is needed to play";
      a.status_error = true;
      return;
    }
  }
  a.st.aa = UsableAA(a.st.aa, a.dlss_installed);
  SaveSettings(a);
  // Your music goes in the music folder next to the game.
  if (a.st.music != "game") sys::MakeDir(a.dist_dir + "music");
  std::string exe = a.dist_dir + "amped3.exe";
  if (!sys::FileExists(exe)) {
    a.status = "amped3.exe is missing at: " + exe;
    a.status_error = true;
    return;
  }
  // The launcher's own arguments go to the game, except its --offer-vendor ID,
  // --ride_after N, --open-controls/--open-keys and --focus.
  std::vector<std::string> extra;
  const std::vector<std::string> launcher_args = sys::LauncherArgs();
  for (size_t i = 0; i < launcher_args.size(); ++i) {
    if (launcher_args[i] == "--offer-vendor" || launcher_args[i] == "--ride_after") ++i;
    else if (launcher_args[i] != "--open-controls" && launcher_args[i] != "--open-keys" &&
             launcher_args[i] != "--open-resolutions" && launcher_args[i] != "--open-sizes" &&
             launcher_args[i] != "--ride" && launcher_args[i].rfind("--focus=", 0) != 0) {
      extra.push_back(launcher_args[i]);
    }
  }
  // Test builds: diag_args.txt next to the launcher adds logging options
  // (one or more per line, # comments).
  {
    std::string text;
    if (sys::ReadFile(a.dist_dir + "diag_args.txt", text)) {
      std::istringstream lines(text);
      std::string line;
      while (std::getline(lines, line)) {
        if (const size_t hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream words(line);
        std::string word;
        while (words >> word) extra.push_back(word);
      }
    }
  }
  std::string args = BuildGameArgs(a.st, a.dist_dir, a.screen, extra);
  // V-Sync ticked at 60 FPS and below too (above 60 BuildGameArgs says so): no
  // tearing.
  if (!a.wine && !a.st.allow_tearing) args += " --d3d12_allow_variable_refresh_rate_and_tearing=false";
  // Under Proton (the Steam Deck, Linux), V-Sync ticked with a frame cap: real
  // vsync presents, from the UI thread. Replacing the queued frame, gamescope
  // dropped up to a sixth of the frames (judder when turning); measured on the
  // Deck at 60: ~160 frames lost per 30 s -> ~10, no hitches. Not uncapped (it
  // would outrun the display).
  if (a.wine && !a.st.allow_tearing && SelectedFps(a.st, a.screen.hz) > 0) {
    args += " --d3d12_present_vsync=true --d3d12_allow_variable_refresh_rate_and_tearing=false";
  }
  std::string error;
  if (!sys::StartProcess(exe, BuildCommandLine(exe, args), a.dist_dir, a.game, error)) {
    a.status = "Could not start the game: " + error;
    a.status_error = true;
    return;
  }
  sys::AllowForeground(a.game);
  a.launching = true;
  a.status = "Starting the game...";
  a.status_error = false;
  // Windows only lets the foreground process give another one the focus, and
  // the game's window appears a few seconds after it starts: stay until it
  // exists, bring it to the front, then close (after 20 s at the latest).
  a.launch_deadline = SDL_GetTicks() + 20000;
}

void PollLaunch(App& a) {
  uint64_t now = SDL_GetTicks();
  if (now < a.next_poll) return;
  a.next_poll = now + 100;
  if (sys::BringGameWindowToFront(a.game) || now > a.launch_deadline) {
    a.quit = true;
  } else if (sys::ProcessExited(a.game)) {
    // Closed before its window appeared: say so instead of vanishing.
    a.launching = false;
    a.status = "The game closed while starting. Details: amped3.log in " + TrimPath(a.dist_dir);
    a.status_error = true;
  }
}

// ---- input -------------------------------------------------------------

std::vector<Item> NavOrder(const App& a) {
  std::vector<Item> v = {Item::GameFolder, Item::Resolution, Item::FrameRate};
  if (a.st.fps_choice == kCustomFps) v.push_back(Item::CustomFps);
  v.insert(v.end(), {Item::Fullscreen, Item::SharpUi, Item::ReampedLogo, Item::Tearing, Item::SkipIntro, Item::SinglePass, Item::AA, Item::Scaling, Item::Music, Item::UiSize, Item::Save, Item::Controls, Item::Ride});
  return v;
}

// The nearest item drawn in that direction from the focused one (dx, dy:
// -1, 0 or 1): the distance that way plus eight times any sideways gap, so
// what's straight ahead wins over what's merely closer. False if there's none.
bool MoveFocusToward(App& a, int dx, int dy) {
  if (a.status == "Settings saved") a.status.clear();
  const auto cur_it = a.rects.find(a.focus == Item::Quit && !a.rects.count(Item::Quit) ? Item::Ride : a.focus);
  if (cur_it == a.rects.end()) return false;
  const Rect c = cur_it->second;
  Item best = a.focus;
  float best_score = 1e30f;
  for (const auto& [item, r] : a.rects) {
    if (item == cur_it->first) continue;
    float ahead, side;
    if (dy) {
      ahead = dy > 0 ? r.y0 - c.y1 : c.y0 - r.y1;
      side = std::max(0.0f, std::max(r.x0 - c.x1, c.x0 - r.x1));
    } else {
      ahead = dx > 0 ? r.x0 - c.x1 : c.x0 - r.x1;
      // (By the rows' middles: the toggles' boxes overlap the rows above and
      // below them, so a gap would say they're all on one row.)
      side = std::abs((r.y0 + r.y1) - (c.y0 + c.y1)) * 0.5f;
    }
    if (ahead < -2.0f * a.s) continue;  // not that way (a little overlap allowed)
    // Among equals, the one more in line with the focused item's middle.
    const float mid = dy ? std::abs((r.x0 + r.x1) - (c.x0 + c.x1)) : std::abs((r.y0 + r.y1) - (c.y0 + c.y1));
    const float score = std::max(0.0f, ahead) + 8.0f * side + 0.05f * mid;
    if (score < best_score) {
      best_score = score;
      best = item;
    }
  }
  if (best == a.focus) return false;
  a.focus = best;
  return true;
}

// Rows whose value left / right change (everything else: left / right move).
bool IsValueRow(Item item) {
  switch (item) {
    case Item::Resolution: case Item::FrameRate: case Item::CustomFps: case Item::AA:
    case Item::Scaling: case Item::Music: case Item::UiSize:
      return true;
    default:
      return false;
  }
}

void MoveFocus(App& a, int delta) {
  if (a.status == "Settings saved") a.status.clear();
  Item cur = a.focus == Item::Quit ? Item::Ride : a.focus;
  std::vector<Item> v = NavOrder(a);
  auto it = std::find(v.begin(), v.end(), cur);
  int i = it == v.end() ? 0 : int(it - v.begin());
  i = std::clamp(i + delta, 0, int(v.size()) - 1);
  a.focus = v[i];
}

void Change(App& a, int delta, bool wrap) {
  switch (a.focus) {
    case Item::Resolution: Step(a.res_choices, a.st.resolution, delta, wrap); break;
    case Item::FrameRate: Step(a.fps_choices, a.st.fps_choice, delta, wrap); break;
    case Item::CustomFps: a.st.custom_fps = std::clamp(std::clamp(a.st.custom_fps, 30, 1000) + delta, 30, 1000); break;
    case Item::AA: {
      // NVIDIA DLAA only with an RTX card (never on the Steam Deck); picking
      // it without NVIDIA's DLSS file offers the download.
      std::vector<std::string> values = AAValues();
      if (a.st.deck || !a.rtx) values.erase(std::remove(values.begin(), values.end(), "dlaa"), values.end());
      Step(values, a.st.aa, delta, wrap);
      if (a.st.aa == "dlaa" && !a.dlss_installed) OfferVendor(a, "dlss");
    } break;
    case Item::Scaling: Step(SharpenValues(), a.st.sharpen, delta, wrap); break;
    case Item::Music: Step(MusicValues(), a.st.music, delta, wrap); break;
    case Item::UiSize: a.st.ui_size = std::clamp(a.st.ui_size + 10 * delta, 50, UiSizeMax(nullptr, a.deck)); break;
    // The bottom buttons, left to right.
    case Item::Save: case Item::Controls: case Item::Quit: case Item::Ride: {
      const std::vector<Item> row = {Item::Save, Item::Controls, Item::Quit, Item::Ride};
      const auto it = std::find(row.begin(), row.end(), a.focus);
      if (it == row.end()) break;
      const int i = std::clamp(int(it - row.begin()) + (delta > 0 ? 1 : -1), 0, int(row.size()) - 1);
      a.focus = row[size_t(i)];
    } break;
    default: break;
  }
}

void OpenResolutionList(App& a);

void Activate(App& a) {
  switch (a.focus) {
    case Item::GameFolder: OpenPicker(a); break;
    case Item::Fullscreen: a.st.fullscreen = !a.st.fullscreen; break;
    case Item::SharpUi: a.st.sharp_ui = !a.st.sharp_ui; break;
    case Item::ReampedLogo: a.st.reamped_logo = !a.st.reamped_logo; break;
    case Item::Tearing: a.st.allow_tearing = !a.st.allow_tearing; break;
    case Item::SkipIntro: a.st.skip_intro = !a.st.skip_intro; break;
    case Item::SinglePass:
      a.st.single_pass = !a.st.single_pass;
      SinglePassChanged(a);
      break;
    case Item::Controls: a.controls = true; a.ctl_sel = 0; break;
    case Item::UiSize: a.sizes = true; a.size_sel = 0; break;
    case Item::Resolution: OpenResolutionList(a); break;
    case Item::Save:
      SaveSettingsAndSay(a);
      break;
    case Item::Music: if (a.st.music == "mine") OpenPicker(a, true); break;
    case Item::Quit: SaveSettings(a); a.quit = true; break;
    case Item::Ride: Launch(a); break;
    case Item::CustomFps: break;
    default: Change(a, 1, true); break;
  }
}

// ---- the Resolution drop-down -------------------------------------------

constexpr int kResRows = 6;  // rows shown at once (the list fits above the key hints)

void OpenResolutionList(App& a) {
  a.res_open = true;
  a.res_opened_frame = ImGui::GetFrameCount();
  auto it = std::find(a.res_choices.begin(), a.res_choices.end(), a.st.resolution);
  a.res_sel = it == a.res_choices.end() ? 0 : int(it - a.res_choices.begin());
  a.res_first = std::max(0, a.res_sel - kResRows / 2);
}

void HandleResolutionList(App& a, Act act) {
  const int n = int(a.res_choices.size());
  switch (act) {
    case Act::Up: a.res_sel = std::max(0, a.res_sel - 1); break;
    case Act::Down: a.res_sel = std::min(n - 1, a.res_sel + 1); break;
    case Act::PageUp: case Act::Left: a.res_sel = std::max(0, a.res_sel - kResRows); break;
    case Act::PageDown: case Act::Right: a.res_sel = std::min(n - 1, a.res_sel + kResRows); break;
    case Act::A: case Act::Start:
      if (a.res_sel < n) a.st.resolution = a.res_choices[size_t(a.res_sel)];
      a.res_open = false;
      break;
    case Act::B: case Act::Cancel: a.res_open = false; break;
    default: break;
  }
}

// The open list, over the page under the Resolution row.
void DrawResolutionList(App& a) {
  const int n = int(a.res_choices.size());
  if (a.res_sel < a.res_first) a.res_first = a.res_sel;
  if (a.res_sel >= a.res_first + kResRows) a.res_first = a.res_sel - kResRows + 1;
  const int shown = std::min(n, kResRows);
  a.res_first = std::clamp(a.res_first, 0, n - shown);
  const float row_h = 27.0f, top = 268.0f;
  Rect box = R(a, a.L, top, a.Rr, top + shown * row_h + 8);
  Box(box, kPlain, kCyan, 6, a.s);
  a.drawing_res_list = true;
  if (ImGui::GetFrameCount() > a.res_opened_frame && !Hover(a, box) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    a.res_open = false;
  }
  for (int i = 0; i < shown; ++i) {
    const int k = a.res_first + i;
    const float y = top + 4 + i * row_h;
    Rect r = R(a, a.L + 4, y, a.Rr - 4, y + row_h - 2);
    const bool current = a.res_choices[size_t(k)] == a.st.resolution;
    if (Hover(a, r) && a.mouse_moved) a.res_sel = k;
    if (k == a.res_sel) Box(r, kField, kCyan, 5, a.s);
    Text(a, a.body, 12.5f, a.L + 14, y + 4, current ? kCyan : kText, ResLabel(a, a.res_choices[size_t(k)]));
    if (Hover(a, r)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (Clicked(a, r)) {
      a.st.resolution = a.res_choices[size_t(k)];
      a.res_open = false;
    }
  }
  a.drawing_res_list = false;
  if (a.res_first > 0) Text(a, a.body, 11.0f, a.Rr - 24, top + 4, kMuted, "^");
  if (a.res_first + shown < n) Text(a, a.body, 11.0f, a.Rr - 24, top + shown * row_h - 14, kMuted, "v");
}

// X on Sharp UI: the made pack deleted and made again (a damaged pack, or to
// try again); the game keeps the originals until the new one is ready.
bool PackMade(const App& a) {
  return sys::FileExists(launcher::JoinPath(launcher::JoinPath(a.dist_dir, "textures"), "pack.txt"));
}

void RemakeSharpUi(App& a) {
  if (!a.st.sharp_ui || a.pack || !PackMade(a)) return;
  const std::string textures = launcher::JoinPath(a.dist_dir, "textures");
  sys::RemoveFile(launcher::JoinPath(textures, "pack.txt"));
  sys::RemoveTree(launcher::JoinPath(textures, "replace"));
  a.pack_checked.clear();  // the next frame's check starts the build
  a.status = "Making the sharp UI again";
  a.status_error = false;
}

void HandleMain(App& a, Act act) {
  switch (act) {
    case Act::X: if (a.focus == Item::SharpUi) RemakeSharpUi(a); break;
    // Up / down: the nearest item that way (in the list's order if the
    // layout isn't known yet). Left / right: change a value row's value,
    // else move.
    case Act::Up: if (!MoveFocusToward(a, 0, -1) && !a.rects.count(a.focus)) MoveFocus(a, -1); break;
    case Act::Down: if (!MoveFocusToward(a, 0, 1) && !a.rects.count(a.focus)) MoveFocus(a, 1); break;
    case Act::Left: if (IsValueRow(a.focus)) Change(a, -1, false); else MoveFocusToward(a, -1, 0); break;
    case Act::Right: if (IsValueRow(a.focus)) Change(a, 1, false); else MoveFocusToward(a, 1, 0); break;
    case Act::PageUp: if (a.focus == Item::CustomFps) Change(a, -10, false); break;
    case Act::PageDown: if (a.focus == Item::CustomFps) Change(a, 10, false); break;
    case Act::A: Activate(a); break;
    case Act::Start: Launch(a); break;
    default: break;  // B does nothing here, so a stray press never quits
  }
}

int PickerRows() { return 11; }  // rows shown in the folder list

void HandlePicker(App& a, Act act) {
  int n = int(a.entries.size());
  switch (act) {
    case Act::Up: a.sel = std::max(0, a.sel - 1); break;
    case Act::Down: a.sel = std::min(n - 1, a.sel + 1); break;
    case Act::PageUp: case Act::Left: a.sel = std::max(0, a.sel - PickerRows()); break;
    case Act::PageDown: case Act::Right: a.sel = std::min(n - 1, a.sel + PickerRows()); break;
    case Act::A: PickerOpen(a, a.sel); break;
    case Act::B: PickerUp(a); break;
    case Act::X: case Act::Start: PickerChoose(a); break;
    case Act::Cancel: a.picker = false; a.focus = a.picker_music ? Item::Music : Item::GameFolder; break;
    default: break;
  }
}

// The runtime's name for a key (src/ui/keybinds.cpp), "" for keys it lacks.
std::string RuntimeKeyName(SDL_Scancode sc) {
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return std::string(1, char('A' + (sc - SDL_SCANCODE_A)));
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return std::string(1, char('1' + (sc - SDL_SCANCODE_1)));
  if (sc == SDL_SCANCODE_0) return "0";
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return "F" + std::to_string(1 + (sc - SDL_SCANCODE_F1));
  if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return "Numpad" + std::to_string(1 + (sc - SDL_SCANCODE_KP_1));
  switch (sc) {
    case SDL_SCANCODE_KP_0: return "Numpad0";
    case SDL_SCANCODE_KP_ENTER: return "NumpadEnter";
    case SDL_SCANCODE_KP_PLUS: return "NumpadPlus";
    case SDL_SCANCODE_KP_MINUS: return "NumpadMinus";
    case SDL_SCANCODE_KP_MULTIPLY: return "NumpadStar";
    case SDL_SCANCODE_KP_DIVIDE: return "NumpadSlash";
    case SDL_SCANCODE_GRAVE: return "Backtick";
    case SDL_SCANCODE_MINUS: return "Minus";
    case SDL_SCANCODE_EQUALS: return "Plus";
    case SDL_SCANCODE_COMMA: return "Comma";
    case SDL_SCANCODE_PERIOD: return "Period";
    case SDL_SCANCODE_SEMICOLON: return "Semicolon";
    case SDL_SCANCODE_SLASH: return "Slash";
    case SDL_SCANCODE_BACKSLASH: return "Backslash";
    case SDL_SCANCODE_LEFTBRACKET: return "LBracket";
    case SDL_SCANCODE_RIGHTBRACKET: return "RBracket";
    case SDL_SCANCODE_APOSTROPHE: return "Quote";
    case SDL_SCANCODE_RETURN: return "Return";
    case SDL_SCANCODE_SPACE: return "Space";
    case SDL_SCANCODE_TAB: return "Tab";
    case SDL_SCANCODE_BACKSPACE: return "Backspace";
    case SDL_SCANCODE_DELETE: return "Delete";
    case SDL_SCANCODE_INSERT: return "Insert";
    case SDL_SCANCODE_HOME: return "Home";
    case SDL_SCANCODE_END: return "End";
    case SDL_SCANCODE_PAGEUP: return "PageUp";
    case SDL_SCANCODE_PAGEDOWN: return "PageDown";
    case SDL_SCANCODE_LEFT: return "Left";
    case SDL_SCANCODE_RIGHT: return "Right";
    case SDL_SCANCODE_UP: return "Up";
    case SDL_SCANCODE_DOWN: return "Down";
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return "Shift";
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return "Control";
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return "Alt";
    default: return {};
  }
}

void HandleKeyboardKeys(App& a, Act act) {
  const int rows = int(ShownKeyActions()) + 1;  // + Reset
  switch (act) {
    case Act::Up: a.key_sel = std::max(0, a.key_sel - 1); break;
    case Act::Down: a.key_sel = std::min(rows - 1, a.key_sel + 1); break;
    case Act::A:
      if (a.key_sel == rows - 1) a.st.keys.clear();
      else a.capturing = true;
      break;
    case Act::B: case Act::Cancel: a.ctl_keyboard = false; break;
    default: break;
  }
}

void HandleControls(App& a, Act act) {
  if (a.ctl_keyboard) {
    HandleKeyboardKeys(a, act);
    return;
  }
  const int rows = int(PadRows().size()) + 2;  // + Reset, Keyboard keys
  if (a.pad_capturing) return;  // the next controller button is the answer
  switch (act) {
    case Act::Up: a.ctl_sel = std::max(0, a.ctl_sel - 1); break;
    case Act::Down: a.ctl_sel = std::min(rows - 1, a.ctl_sel + 1); break;
    case Act::A:
      if (a.ctl_sel == rows - 2) a.st.pad_map.clear();
      else if (a.ctl_sel == rows - 1) a.ctl_keyboard = true, a.key_sel = 0;
      else a.pad_capturing = true;
      break;
    case Act::B: case Act::Cancel: a.controls = false; a.focus = Item::Controls; break;
    default: break;
  }
}

void PushDir(App& a, Repeat& r, int dir) {
  a.acts.push_back(Act(dir));
  r.dir = dir;
  r.next = SDL_GetTicks() + 350;
}

void HandleEvent(App& a, const SDL_Event& e) {
  ImGui_ImplSDL3_ProcessEvent(&e);
  switch (e.type) {
    case SDL_EVENT_WINDOW_RESIZED: if (!a.deck) a.window_resized = true; break;
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      // Closing the window keeps the settings, as Quit does.
      if (!a.launching) SaveSettings(a);
      a.quit = true;
      break;
    case SDL_EVENT_GAMEPAD_ADDED:
      if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) a.pads.push_back(g);
      a.pad_hints = true;
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      for (size_t i = 0; i < a.pads.size(); ++i) {
        if (SDL_GetGamepadID(a.pads[i]) == e.gdevice.which) {
          SDL_CloseGamepad(a.pads[i]);
          a.pads.erase(a.pads.begin() + i);
          break;
        }
      }
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
      a.pad_hints = true;
      if (a.pad_capturing) {
        // The Controls page waits for the button to use for a game button.
        static const std::pair<int, const char*> kNames[] = {
            {SDL_GAMEPAD_BUTTON_SOUTH, "A"}, {SDL_GAMEPAD_BUTTON_EAST, "B"}, {SDL_GAMEPAD_BUTTON_WEST, "X"},
            {SDL_GAMEPAD_BUTTON_NORTH, "Y"}, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "LB"},
            {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "RB"}, {SDL_GAMEPAD_BUTTON_BACK, "Back"},
            {SDL_GAMEPAD_BUTTON_START, "Start"}, {SDL_GAMEPAD_BUTTON_LEFT_STICK, "LS"},
            {SDL_GAMEPAD_BUTTON_RIGHT_STICK, "RS"}, {SDL_GAMEPAD_BUTTON_DPAD_UP, "Up"},
            {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "Down"}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "Left"},
            {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "Right"}};
        for (const auto& [button, name] : kNames) {
          if (button == e.gbutton.button && a.ctl_sel < int(PadRows().size())) {
            a.st.pad_map = PadBind(a.st.pad_map, PadButtonNames()[PadRows()[size_t(a.ctl_sel)]], name);
            a.pad_capturing = false;
          }
        }
        break;
      }
      switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: PushDir(a, a.dpad, int(Act::Up)); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: PushDir(a, a.dpad, int(Act::Down)); break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: PushDir(a, a.dpad, int(Act::Left)); break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: PushDir(a, a.dpad, int(Act::Right)); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: a.acts.push_back(Act::A); break;
        case SDL_GAMEPAD_BUTTON_EAST: a.acts.push_back(Act::B); break;
        case SDL_GAMEPAD_BUTTON_WEST: a.acts.push_back(Act::X); break;
        case SDL_GAMEPAD_BUTTON_START: a.acts.push_back(Act::Start); break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: a.acts.push_back(Act::PageUp); break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: a.acts.push_back(Act::PageDown); break;
        default: break;
      }
      break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      if (e.gbutton.button >= SDL_GAMEPAD_BUTTON_DPAD_UP && e.gbutton.button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) a.dpad.dir = -1;
      break;
    case SDL_EVENT_KEY_DOWN: {
      a.pad_hints = false;
      if (a.pad_capturing) {
        a.pad_capturing = false;  // a key cancels waiting for a controller button
        break;
      }
      if (a.capturing) {
        // The Controls page's keyboard view waits for a key: Escape cancels.
        a.capturing = false;
        if (e.key.key != SDLK_ESCAPE) {
          const std::string name = RuntimeKeyName(e.key.scancode);
          if (!name.empty()) SetKeys(a.st, KeyActions()[size_t(a.key_sel)].id, name);
        }
        break;
      }
      bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
      // Ctrl + / Ctrl -: the window (and everything in it) larger or smaller.
      if ((e.key.mod & SDL_KMOD_CTRL) && !a.deck &&
          (e.key.key == SDLK_EQUALS || e.key.key == SDLK_PLUS || e.key.key == SDLK_KP_PLUS ||
           e.key.key == SDLK_MINUS || e.key.key == SDLK_KP_MINUS)) {
        int w = 0, h = 0;
        SDL_GetWindowSize(a.window, &w, &h);
        const bool larger = e.key.key != SDLK_MINUS && e.key.key != SDLK_KP_MINUS;
        h = std::max(360, int(std::lround(h * (larger ? 1.1f : 1.0f / 1.1f))));
        SDL_SetWindowSize(a.window, int(std::lround(h * kDeckDesignW / kDeckDesignH)), h);
        a.window_resized = true;
        break;
      }
      switch (e.key.key) {
        case SDLK_UP: a.acts.push_back(Act::Up); break;
        case SDLK_DOWN: a.acts.push_back(Act::Down); break;
        case SDLK_LEFT: a.acts.push_back(Act::Left); break;
        case SDLK_RIGHT: a.acts.push_back(Act::Right); break;
        case SDLK_TAB: a.acts.push_back(shift ? Act::Up : Act::Down); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: a.acts.push_back(Act::A); break;
        case SDLK_BACKSPACE: a.acts.push_back(Act::B); break;
        case SDLK_ESCAPE: a.acts.push_back(Act::Cancel); break;
        case SDLK_X: a.acts.push_back(Act::X); break;
        case SDLK_PAGEUP: a.acts.push_back(Act::PageUp); break;
        case SDLK_PAGEDOWN: a.acts.push_back(Act::PageDown); break;
        default: break;
      }
      break;
    }
    case SDL_EVENT_MOUSE_MOTION:
      if (SDL_GetTicks() < a.mouse_ignore_until) break;
      a.mouse_travel += std::abs(e.motion.xrel) + std::abs(e.motion.yrel);
      if (a.mouse_travel > 8.0f) {
        a.mouse_moved = true;
        a.pad_hints = false;
      }
      break;
    default: break;
  }
}

// D-pad auto-repeat, and the left stick as a D-pad.
void UpdateRepeat(App& a) {
  uint64_t now = SDL_GetTicks();
  int dir = -1;
  for (SDL_Gamepad* g : a.pads) {
    float x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    float limit = a.stick.dir >= 0 ? 0.35f : 0.6f;  // hysteresis
    if (std::fabs(x) < limit && std::fabs(y) < limit) continue;
    if (std::fabs(y) >= std::fabs(x)) dir = int(y < 0 ? Act::Up : Act::Down);
    else dir = int(x < 0 ? Act::Left : Act::Right);
    break;
  }
  if (dir != a.stick.dir) {
    if (dir >= 0) {
      a.pad_hints = true;
      PushDir(a, a.stick, dir);
    } else {
      a.stick.dir = -1;
    }
  }
  for (Repeat* r : {&a.dpad, &a.stick}) {
    if (r->dir >= 0 && now >= r->next) {
      a.acts.push_back(Act(r->dir));
      r->next = now + 90;
    }
  }
}

// ---- drawing -----------------------------------------------------------

void DrawHeader(App& a) {
  float h = (108.0f + (a.two_columns ? kDeckHeader : 0.0f)) * a.s;
  ImDrawList* dl = DL();
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(a.width, h), kHeaderTop, kHeaderTop, kNavy, kNavy);
  // Faint, static snowfall dots on a 26 px tile.
  float tile = 26.0f * a.s;
  const float dots[4][3] = {{5, 7, 0.9f}, {17, 3, 0.6f}, {12, 18, 0.7f}, {23, 21, 0.9f}};
  for (float ty = 0; ty < h; ty += tile) {
    for (float tx = 0; tx < a.width; tx += tile) {
      for (auto& d : dots) {
        if (ty + d[1] * a.s < h) dl->AddCircleFilled(ImVec2(tx + d[0] * a.s, ty + d[1] * a.s), std::max(0.6f, d[2] * a.s), IM_COL32(255, 255, 255, 46), 8);
      }
    }
  }
  if (a.logo) {
    // The logo, left.
    float lh = a.two_columns ? 132.0f : 70.0f;
    float lw = lh * a.logo_aspect;
    float oy = a.oy;
    a.oy = 0.0f;
    DL()->AddImage(ImTextureRef(ImTextureID(reinterpret_cast<intptr_t>(a.logo))), P(a, a.L - 8, 8), P(a, a.L - 8 + lw, 8 + lh));
    a.oy = oy;
    return;
  }
  float size = 38.0f;
  float x = a.L, y = 22.0f;
  Text(a, a.display, size, x, y, kWhite, "Re");
  x += TextW(a, a.display, size, "Re");
  Text(a, a.display, size, x, y, kCyan, "Amped");
  x += TextW(a, a.display, size, "Amped");
  Text(a, a.display, size, x, y, kWhite, " 3");
}

// A rounded key cap ("A" in a circle for face buttons) and its label.
float Hint(App& a, float x, float y, const std::string& cap, const std::string& label, bool round) {
  float size = 10.5f;
  float cw = round ? 17.0f : TextW(a, a.body, size, cap) + 12.0f;
  if (cap == "<>" || cap == "^v") cw = 26.0f;
  Rect r = R(a, x, y, x + cw, y + 17);
  if (round) DL()->AddCircleFilled(ImVec2((r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2), 8.5f * a.s, kBorder);
  else Box(r, kBorder, 0, 4, a.s);
  ImVec2 c((r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2);
  if (cap == "<>") {
    Chevron(ImVec2(c.x - 5 * a.s, c.y), 8 * a.s, false, kText, a.s);
    Chevron(ImVec2(c.x + 5 * a.s, c.y), 8 * a.s, true, kText, a.s);
  } else if (cap == "^v") {
    float k = 4 * a.s;
    DL()->AddTriangleFilled(ImVec2(c.x - 9 * a.s, c.y + 2 * a.s), ImVec2(c.x - 9 * a.s + 2 * k, c.y + 2 * a.s), ImVec2(c.x - 9 * a.s + k, c.y - 3 * a.s), kText);
    DL()->AddTriangleFilled(ImVec2(c.x + 1 * a.s, c.y - 2 * a.s), ImVec2(c.x + 1 * a.s + 2 * k, c.y - 2 * a.s), ImVec2(c.x + 1 * a.s + k, c.y + 3 * a.s), kText);
  } else {
    float tw = TextW(a, a.body, size, cap);
    BoldText(a, size, x + (cw - tw) / 2, y + 1.5f, kText, cap);
  }
  x += cw + 6;
  Text(a, a.body, 11.5f, x, y + 0.5f, kMuted, label);
  return x + TextW(a, a.body, 11.5f, label) + 16;
}

// Single-pass rendering switched: off brings back the game's own MSAA (shown
// as the anti-aliasing, alone); on can't have it, so SMAA takes over.
void SinglePassChanged(App& a) {
  if (!a.st.single_pass) {
    a.st.aa = "none";
    a.status = "The game's own 2x MSAA is on (see Anti-aliasing)";
  } else {
    if (a.st.aa == "none") a.st.aa = "smaa";
    a.status = "Single pass: SMAA smooths the edges";
  }
  a.status_error = false;
}

void DrawHints(App& a, float y) {
  float x = a.L + 2;
  if (a.job) {
    const bool downloading = a.job->phase == VendorJob::Phase::kDownloading;
    if (a.pad_hints) {
      if (!downloading) x = Hint(a, x, y, "A", "Accept and download", true);
      x = Hint(a, x, y, "B", downloading ? "Cancel" : "Not now", true);
      if (!downloading) Hint(a, x, y, "^v", "Read", false);
    } else {
      if (!downloading) x = Hint(a, x, y, "Enter", "Accept and download", false);
      x = Hint(a, x, y, "Esc", downloading ? "Cancel" : "Not now", false);
      if (!downloading) Hint(a, x, y, "^v", "Read", false);
    }
    return;
  }
  if (a.picker) {
    if (a.pad_hints) {
      x = Hint(a, x, y, "A", "Open", true);
      x = Hint(a, x, y, "B", "Up", true);
      Hint(a, x, y, "X", "Choose this folder", true);
    } else {
      x = Hint(a, x, y, "Enter", "Open", false);
      x = Hint(a, x, y, "Backspace", "Up", false);
      x = Hint(a, x, y, "X", "Choose", false);
      Hint(a, x, y, "Esc", "Cancel", false);
    }
  } else if (a.pad_hints) {
    x = Hint(a, x, y, "A", "Select", true);
    x = Hint(a, x, y, "<>", "Change", false);
    if (a.focus == Item::CustomFps) x = Hint(a, x, y, "LB / RB", "10 at a time", false);
    Hint(a, x, y, "START", "Ride", false);
  } else {
    x = Hint(a, x, y, "^v", "Move", false);
    x = Hint(a, x, y, "<>", "Change", false);
    x = Hint(a, x, y, "Enter", "Select", false);
    if (!a.deck) Hint(a, x, y, "Ctrl +/-", "Size", false);
  }
  // Which build this is, for bug reports.
  if (!a.version.empty() && !a.picker) {
    const float right = a.two_columns ? kDeckDesignW - kLeft : a.Rr;  // the page's right edge
    Text(a, a.body, 10.5f, right - TextW(a, a.body, 10.5f, a.version), y + 1.0f, kMuted, a.version);
  }
}

// A focusable item: mouse hover moves the focus to it.
void Track(App& a, Item item, const Rect& r) {
  a.rects[item] = r;
  if (a.mouse_moved && Hover(a, r)) a.focus = item;
}

// A choice row: value and arrows; click the arrows to step, elsewhere for the
// next value.
void ChoiceRow(App& a, Item item, float y, const std::string& text, float text_x = -1.0f) {
  if (text_x < 0) text_x = a.L + 12;
  Rect r = R(a, a.L, y, a.Rr, y + 34);
  Rect left = R(a, a.Rr - 62, y, a.Rr - 32, y + 34);
  Rect right = R(a, a.Rr - 32, y, a.Rr, y + 34);
  Track(a, item, r);
  bool focused = a.focus == item;
  Box(r, kField, Hover(a, r) || focused ? kCyan : kBorder, 6, a.s);
  if (focused) Ring(r, a.s);
  Text(a, a.body, 12.5f, text_x, y + 9, kText, text);
  ImU32 arrow = focused ? kCyan : kMuted;
  Chevron(ImVec2((left.x0 + left.x1) / 2, (left.y0 + left.y1) / 2), 10 * a.s, false, arrow, a.s);
  Chevron(ImVec2((right.x0 + right.x1) / 2, (right.y0 + right.y1) / 2), 10 * a.s, true, arrow, a.s);
  if (Hover(a, r)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (Clicked(a, r)) {
    a.focus = item;
    if (item == Item::Resolution && !Clicked(a, left) && !Clicked(a, right)) OpenResolutionList(a);
    else Change(a, Clicked(a, left) ? -1 : 1, !Clicked(a, left) && !Clicked(a, right));
  }
}

void CheckRow(App& a, Item item, float y, const std::string& text, bool& value) {
  float w = 16 + 8 + TextW(a, a.body, 12.5f, text);
  Rect r = R(a, a.L, y, a.L + w, y + 22);
  Track(a, item, r);
  Rect box = R(a, a.L, y + 3, a.L + 16, y + 19);
  bool hover = Hover(a, r);
  Box(box, value ? kCyan : kField, value || hover ? kCyan : kBorder, 4, a.s);
  if (value) Tick(ImVec2(box.x0, box.y0), 16 * a.s, kNavy, a.s);
  Text(a, a.body, 12.5f, a.L + 24, y + 3, kText, text);
  if (a.focus == item) Ring(R(a, a.L - 2, y, a.L + w + 4, y + 22), a.s);
  if (hover) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (Clicked(a, r)) {
    a.focus = item;
    value = !value;
  }
}

std::string PackError(const std::string& error);

// Starts the sharp UI pack when the folder is valid and the pack is missing
// or out of date; picks up a finished one.
void UpdatePack(App& a, bool valid) {
  if (a.pack && a.pack->finished) {
    a.pack->thread.join();
    a.pack_message = a.pack->ok ? "Sharp UI ready" : PackError(a.pack->error);
    if (a.pack->ok) a.logo_started = false;  // the logo again, from the sharp art
    a.pack.reset();
  }
  if (a.pack || !valid || !a.st.sharp_ui || a.pack_checked == a.st.game_path) return;
  a.pack_checked = a.st.game_path;
  uipack::Options o;
  o.game_dir = a.st.game_path;
  o.textures_dir = launcher::JoinPath(a.dist_dir, "textures");
  o.esrgan_dir = launcher::JoinPath(launcher::JoinPath(a.dist_dir, "tools"), "esrgan");
  o.scale = a.deck ? 2 : 4;
  if (!uipack::UpscalerPresent(o.esrgan_dir) || uipack::UpToDate(o)) return;
  if (!sys::ClaimPackBuild()) {
    a.pack_message = "Sharp UI is still being made (from the last start); it's used once ready";
    return;
  }
  a.pack = std::make_unique<App::PackJob>();
  App::PackJob* job = a.pack.get();
  job->started_ms = SDL_GetTicks();
  job->thread = std::thread([job, o] {
    job->ok = uipack::Build(o, job->progress, job->cancel, job->error);
    sys::ReleasePackBuild();
    job->finished = true;
  });
}

// The header's logo: the title screen's REAMPED3 logo, made from the player's
// own game files (textures\launcher_logo.png; again from the sharp UI pack
// once it's made); logo.bmp (ours) until then.
void UpdateLogo(App& a, bool valid) {
  const std::string textures = launcher::JoinPath(a.dist_dir, "textures");
  const std::string png = launcher::JoinPath(textures, "launcher_logo.png");
  if (!a.logo_started && valid) {
    a.logo_started = true;
    const std::string stamp = launcher::JoinPath(textures, "pack.txt");
    // launcher_logo.txt: the logo drawing's version it was made with.
    const std::string version = launcher::JoinPath(textures, "launcher_logo.txt");
    const std::string want = std::to_string(amped3::title_logo::kLogoVersion);
    std::string made;
    sys::ReadFile(version, made);
    if (sys::FileExists(png) && (!sys::FileExists(stamp) || sys::FileTime(png) >= sys::FileTime(stamp)) &&
        made == want) {
      *a.logo_made = true;
    } else {
      uipack::Options o;
      o.game_dir = a.st.game_path;
      o.textures_dir = textures;
      sys::MakeDir(textures);
      std::thread([made = a.logo_made, o, png, version, want] {
        if (uipack::WriteLauncherLogo(o, png)) {
          sys::WriteFile(version, want);
          *made = true;
        }
      }).detach();
    }
  }
  if (!a.logo_made->exchange(false)) return;
  std::string data;
  int w = 0, h = 0, channels = 0;
  if (!sys::ReadFile(png, data)) return;
  stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()), int(data.size()), &w, &h,
                                          &channels, 4);
  if (!pixels) return;
  // Cropped to what's drawn (the texture has room around the logo).
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (pixels[(size_t(y) * w + x) * 4 + 3] > 8) {
        x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
      }
    }
  }
  if (x1 < x0) x0 = 0, y0 = 0, x1 = w - 1, y1 = h - 1;
  const int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
  if (SDL_Surface* surface = SDL_CreateSurfaceFrom(cw, ch, SDL_PIXELFORMAT_RGBA32, pixels + (size_t(y0) * w + x0) * 4, w * 4)) {
    if (SDL_Texture* texture = SDL_CreateTextureFromSurface(a.renderer, surface)) {
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
      if (a.logo) SDL_DestroyTexture(a.logo);
      a.logo = texture;
      a.logo_aspect = float(cw) / float(std::max(1, ch));
      a.logo_from_game = true;
    }
    SDL_DestroySurface(surface);
  }
  stbi_image_free(pixels);
}

// The sharp UI being made, in plain words: how far along, and how long.
std::string PackStatus(const App& a) {
  if (!a.pack) return a.pack_message;
  const int total = a.pack->progress.total, done = a.pack->progress.done;
  if (total == 0) return "Upscaling UI art: getting started...";
  std::string left;
  if (done >= 8 && done < total) {
    const double per = double(SDL_GetTicks() - a.pack->started_ms) / done;
    const int minutes = int(std::ceil(per * (total - done) / 60000.0));
    left = ", about " + std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes") + " left";
  }
  return "Upscaling UI art: " + std::to_string(done * 100 / std::max(1, total)) + "% done" + left;
}

// Why the sharp UI couldn't be made, in plain words.
std::string PackError(const std::string& error) {
  if (error == "cancelled") return "Upscaling UI art stopped. It carries on next time.";
  if (error.find("Vulkan") != std::string::npos || error.find("upscaler") != std::string::npos)
    return "Couldn't upscale the UI art: your graphics card or driver couldn't run the enlarging tool "
           "(it needs Vulkan; updating the graphics driver may help). The game uses its original UI.";
  if (error.find("GuiPackages") != std::string::npos)
    return "Couldn't upscale the UI art: the game's UI files weren't found in your game folder.";
  return "Couldn't upscale the UI art, so the game uses its original UI. Turn Sharp UI off and on to try again.";
}

void DrawMain(App& a) {
  bool valid = FolderValid(a);
  UpdatePack(a, valid);

  // Game folder: the path and Browse (A opens the picker).
  Label(a, 124, "GAME FOLDER");
  {
    Rect row = R(a, a.L, 144, a.Rr, 178);
    Rect field = R(a, a.L, 144, a.Rr - 94, 178);
    Rect browse = R(a, a.Rr - 86, 144, a.Rr, 178);
    Track(a, Item::GameFolder, row);
    bool focused = a.focus == Item::GameFolder;
    Box(field, kField, Hover(a, field) ? kCyan : kBorder, 6, a.s);
    std::string path = TrimPath(a.st.game_path);
    float max_w = a.Rr - 94 - a.L - 20;
    std::string shown = path;
    while (!shown.empty() && TextW(a, a.body, 12.0f, shown) > max_w) DropFirstChar(shown);
    if (shown != path) shown = "..." + shown;
    Text(a, a.body, 12.0f, a.L + 10, 153, path.empty() ? kMuted : kText, path.empty() ? "No folder chosen" : shown);
    Box(browse, kPlain, Hover(a, browse) ? kCyan : kBorder, 6, a.s);
    float bw = TextW(a, a.body, 13.0f, "Browse");
    BoldText(a, 13.0f, a.Rr - 43 - bw / 2, 152, kPillText, "Browse");
    if (focused) Ring(row, a.s);
    if (Hover(a, row)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (Clicked(a, row)) {
      a.focus = Item::GameFolder;
      OpenPicker(a);
    }
    if (path.empty()) Text(a, a.body, 11.5f, a.L + 2, 183, kError, "Pick your extracted Amped 3 folder (the one with default.xex).");
    else if (!valid) Text(a, a.body, 11.5f, a.L + 2, 183, kError, "No default.xex in this folder.");
  }

  // Resolution: a drop-down of the display's own modes (A opens it; left /
  // right step through it).
  Label(a, 210, "RESOLUTION");
  ChoiceRow(a, Item::Resolution, 230, ResLabel(a, a.st.resolution));

  // Frame rate, then the custom cap or a hint.
  Label(a, 280, "FRAME RATE");
  ChoiceRow(a, Item::FrameRate, 300, FpsLabel(a, a.st.fps_choice));
  if (a.st.fps_choice == kCustomFps) {
    ChoiceRow(a, Item::CustomFps, 342, "Custom cap: " + std::to_string(std::clamp(a.st.custom_fps, 30, 1000)) + " FPS");
  }

  // Two columns: under the frame rate, what the focused setting does; the
  // sharp UI option's panel (what it does and costs, its progress) otherwise.
  // Under the frame rate (or the custom cap's row), in the page's own text
  // colour and size.
  const auto [explain_label, explain_text] = a.two_columns ? Explain(a) : std::pair<std::string, std::string>{};
  const float panel_y = a.st.fps_choice == kCustomFps ? 384.0f : 344.0f;
  if (!explain_label.empty()) {
    Label(a, panel_y, explain_label.c_str());
    Text(a, a.body, 13.0f, a.L + 2, panel_y + 18, kText, explain_text, a.Rr - a.L - 4);
  } else if (a.two_columns) {
    Label(a, panel_y, "SHARP UI");
    std::string text;
    const char* when = a.deck ? "about 15-20 minutes on the Steam Deck" : "about 5 minutes";
    if (!a.st.sharp_ui) {
      text = std::string("Upscales the menus, HUD, button icons and loading screens for high resolutions "
                         "(the game's own are blurry above 720p). Turn it on and it's done once, on this PC, from "
                         "your game files: ") +
             when + ", with Real-ESRGAN (an image upscaler, not an AI assistant; nothing leaves the PC). You can "
             "play meanwhile.";
    } else if (a.pack) {
      text = PackStatus(a) + ". You can play meanwhile; the sharp UI is used from your next start.";
    } else if (!a.pack_message.empty() && a.pack_message != "Sharp UI ready") {
      text = a.pack_message;
    } else {
      text = "On: the menus, HUD, button icons and loading screens are sharp. Made with Real-ESRGAN, an image "
             "upscaler that runs on this PC (not an AI assistant; nothing leaves the PC)." +
             std::string(a.focus == Item::SharpUi ? " X: delete them and make them again." : "");
    }
    Text(a, a.body, 13.0f, a.L + 2, panel_y + 18, a.pack ? kCyan : kText, text, a.Rr - a.L - 4);
  }

  // Two columns: the rest goes to the right column, raised beside the above.
  if (a.two_columns) {
    a.L += kDeckColumn2;
    a.Rr += kDeckColumn2;
    a.oy -= kDeckRaise;
  }

  // Toggles.
  CheckRow(a, Item::Fullscreen, 388, "Fullscreen", a.st.fullscreen);
  // Beside it: the sharp UI pack.
  a.L += 150;
  CheckRow(a, Item::SharpUi, 388, "Sharp UI", a.st.sharp_ui);
  // And the ReAmped 3 logo in the game.
  a.L += 140;
  CheckRow(a, Item::ReampedLogo, 388, "ReAmped logo", a.st.reamped_logo);
  a.L -= 290;
  // V-Sync is the opposite of allow_tearing (what the settings file keeps).
  bool vsync = !a.st.allow_tearing;
  CheckRow(a, Item::Tearing, 414, "V-Sync", vsync);
  a.st.allow_tearing = !vsync;
  // Beside it: the publisher movies at start.
  a.L += 150;
  CheckRow(a, Item::SkipIntro, 414, "Skip intro movies", a.st.skip_intro);
  a.L -= 150;
  const bool single_pass = a.st.single_pass;
  CheckRow(a, Item::SinglePass, 440, "Fast single-pass rendering",
           a.st.single_pass);
  if (a.st.single_pass != single_pass) SinglePassChanged(a);  // clicked

  // Image.
  Label(a, 474, "IMAGE AND MUSIC");
  ChoiceRow(a, Item::AA, 492, AALabel(a.st.aa, a.st.single_pass));
  ChoiceRow(a, Item::Scaling, 530, SharpenLabel(a.st.sharpen));
  ChoiceRow(a, Item::Music, 568, MusicLabel(a.st.music));
  ChoiceRow(a, Item::UiSize, 606, "Menus and HUD size: " + std::to_string(a.st.ui_size) + "%");

  // Bottom bar: status, Quit (saves), RIDE.
  {
    float y = 650;
    const float pad = 32.0f, gap = 10.0f;
    float ride_w = TextW(a, a.display, 20.0f, "RIDE") + 52;
    float quit_w = TextW(a, a.body, 13.0f, "Quit") + pad;
    Rect ride = R(a, a.Rr - ride_w, y, a.Rr, y + 40);
    Rect quit = R(a, a.Rr - ride_w - gap - quit_w, y, a.Rr - ride_w - gap, y + 40);
    const float ctl_w = TextW(a, a.body, 13.0f, "Controls") + pad;
    const float ctl_x1 = a.Rr - ride_w - gap - quit_w - gap;
    Rect controls = R(a, ctl_x1 - ctl_w, y, ctl_x1, y + 40);
    // Save: settings.ini now (RIDE and Quit save too).
    const float save_w = TextW(a, a.body, 13.0f, "Save") + pad;
    const float save_x1 = ctl_x1 - ctl_w - gap;
    Rect save = R(a, save_x1 - save_w, y, save_x1, y + 40);
    Track(a, Item::Save, save);
    Box(save, kPlain, Hover(a, save) ? kCyan : kBorder, 6, a.s);
    BoldText(a, 13.0f, save_x1 - save_w + pad / 2, y + 12, kPillText, "Save");
    if (a.focus == Item::Save) Ring(save, a.s);
    if (Hover(a, save)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (Clicked(a, save)) {
      a.focus = Item::Save;
      SaveSettingsAndSay(a);
    }
    Track(a, Item::Ride, ride);
    Track(a, Item::Quit, quit);
    Track(a, Item::Controls, controls);
    Box(controls, kPlain, Hover(a, controls) ? kCyan : kBorder, 6, a.s);
    BoldText(a, 13.0f, ctl_x1 - ctl_w + pad / 2, y + 12, kPillText, "Controls");
    if (a.focus == Item::Controls) Ring(controls, a.s);
    if (Hover(a, controls)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (Clicked(a, controls)) {
      a.focus = Item::Controls;
      a.controls = true;
      a.ctl_sel = 0;
    }
    bool enabled = RideEnabled(valid);
    Box(quit, kPlain, Hover(a, quit) ? kCyan : kBorder, 6, a.s);
    BoldText(a, 13.0f, a.Rr - ride_w - gap - quit_w + pad / 2, y + 12, kPillText, "Quit");
    ImU32 go = Hover(a, ride) && enabled ? kGoHover : kGo;
    if (!enabled) go = (go & 0x00FFFFFF) | (128u << 24);
    Box(ride, go, 0, 6, a.s);
    Text(a, a.display, 20.0f, a.Rr - ride_w + 26, y + 8, enabled ? kWhite : IM_COL32(255, 255, 255, 128), "RIDE");
    if (a.focus == Item::Ride) Ring(ride, a.s);
    if (a.focus == Item::Quit) Ring(quit, a.s);
    if (Hover(a, ride) || Hover(a, quit)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (Clicked(a, quit)) {
      SaveSettings(a);
      a.quit = true;
    }
    if (Clicked(a, ride)) {
      a.focus = Item::Ride;
      Launch(a);
    }
    std::string status = a.status;
    bool error = a.status_error;
    if (status.empty() && !enabled) {
      status = "Choose your Amped 3 folder";
      error = false;
    } else if (status.empty() && a.focus == Item::Music && a.st.music == "mine") {
      status = "A / Enter: choose your music folder";
    } else if (status.empty() && a.focus == Item::Music && a.st.music == "system") {
      status = "Start Spotify (or another music app); the game's music pauses while it plays";
    } else if (status.empty() && !a.two_columns && a.focus == Item::Scaling) {
      status = kSharpenHint;
    } else if (status.empty() && !a.two_columns && a.focus == Item::ReampedLogo) {
      status = kReampedLogoHint;
    } else if (status.empty() && a.focus == Item::SharpUi) {
      status = a.st.sharp_ui ? "Sharp menus, HUD, icons and loading screens at high resolutions"
                             : "On: the game's UI upscaled once, in a few minutes. Off: the original, blurry UI";
    } else if (status.empty() && a.st.sharp_ui) {
      status = PackStatus(a);
    }
    // Beside the buttons when there's room, else on its own line under them
    // (the Steam Deck's row is full: a narrow column wrapped it a word a line).
    const float beside = a.Rr - ride_w - 4 * gap - quit_w - ctl_w - save_w - a.L - 2;
    if (beside >= 140) {
      Text(a, a.body, 12.0f, a.L + 2, y + (status.size() > 34 ? 4 : 12), error ? kError : kCyan, status, beside);
    } else if (!status.empty()) {
      Text(a, a.body, 12.0f, a.L + 2, y + 46, error ? kError : kCyan, status, a.Rr - a.L - 4);
    }
  }
  if (a.two_columns) {
    a.L -= kDeckColumn2;
    a.Rr -= kDeckColumn2;
    a.oy += kDeckRaise;
  }
}

void DrawKeyboardKeys(App& a) {
  Label(a, 124, "KEYBOARD KEYS");
  Text(a, a.body, 11.5f, a.L + 2, 142, kMuted,
       a.capturing ? "Press the new key (Esc: keep the old one)." : "Enter on an action, then press its new key.");
  const std::vector<KeyAction>& actions = KeyActions();
  const std::vector<std::vector<size_t>> conflicts = KeyConflicts(a.st);
  bool any_conflict = false;
  for (const auto& c : conflicts) any_conflict |= !c.empty();
  if (any_conflict && !a.capturing) {
    Text(a, a.body, 11.5f, a.L + 260, 124, kError, "Red: a key used for more than one action");
  }
  const float row_h = 25.0f, top = 166.0f;
  const float col_w = (a.Rr - a.L - 12) / 2;
  const size_t count = ShownKeyActions();
  const size_t half = (count + 1) / 2;
  for (size_t i = 0; i <= count; ++i) {
    const bool reset = i == count;
    const float x0 = reset ? a.L : a.L + (i < half ? 0 : col_w + 12);
    const float y = top + (reset ? float(half) : float(i % half)) * row_h + (reset ? 12 : 0);
    Rect r = R(a, x0, y, x0 + col_w, y + row_h - 4);
    const bool focused = a.key_sel == int(i);
    if (Hover(a, r) && a.mouse_moved && !a.capturing) a.key_sel = int(i);
    Box(r, focused && a.capturing ? kPlain : kField, focused ? kCyan : kBorder, 5, a.s);
    if (reset) {
      BoldText(a, 11.5f, x0 + 10, y + 4, a.st.keys.empty() ? kMuted : kText, "Reset to the default keys");
      if (Clicked(a, r)) a.st.keys.clear();
      continue;
    }
    const std::string keys = KeysFor(a.st, actions[i].id);
    bool changed = false;
    for (const auto& kv : a.st.keys) changed |= kv.first == actions[i].id;
    // What it does on the left, its key on the right (red: shared).
    BoldText(a, 11.5f, x0 + 10, y + 4, kText, actions[i].label);
    const std::string shown = focused && a.capturing ? "press a key..." : keys;
    Text(a, a.body, 11.5f, x0 + col_w - 10 - TextW(a, a.body, 11.5f, shown), y + 4,
         focused && a.capturing ? kCyan : !conflicts[i].empty() ? kError : changed ? kCyan : kMuted, shown);
    if (Clicked(a, r)) {
      a.key_sel = int(i);
      a.capturing = true;
    }
  }
  Text(a, a.body, 11.5f, a.L + 2, top + float(half + 1) * row_h + 22, kMuted, "B  back");
}

void DrawControls(App& a) {
  if (a.ctl_keyboard) {
    DrawKeyboardKeys(a);
    return;
  }
  Label(a, 124, "CONTROLLER BUTTONS");
  Text(a, a.body, 11.5f, a.L + 2, 142, a.pad_capturing ? kCyan : kMuted,
       a.pad_capturing ? "Press the controller button to use (any key: keep it as it is)."
                       : "A on an action, then press the button you want for it (the two buttons swap).");
  const std::vector<std::string>& names = PadButtonNames();
  const std::vector<size_t>& rows = PadRows();
  const size_t per_col = (rows.size() + 1) / 2;
  const float row_h = 27.0f, top = 166.0f;
  const float col_w = (a.Rr - a.L - 12) / 2;
  // Keyboard keys: a row under Reset.
  {
    const float y = top + float(per_col + 1) * row_h + 14;
    Rect r = R(a, a.L, y, a.L + col_w, y + row_h - 4);
    const bool focused = a.ctl_sel == int(rows.size()) + 1;
    if (Hover(a, r) && a.mouse_moved) a.ctl_sel = int(rows.size()) + 1;
    Box(r, kField, focused ? kCyan : kBorder, 5, a.s);
    BoldText(a, 12.0f, a.L + 10, y + 5, kText, "Keyboard keys  >");
    if (Clicked(a, r)) a.ctl_keyboard = true, a.key_sel = 0;
  }
  for (size_t i = 0; i <= rows.size(); ++i) {
    // Two columns, then Reset under them.
    const bool reset = i == rows.size();
    const float x0 = reset ? a.L : a.L + (i < per_col ? 0 : col_w + 12);
    const float x1 = reset ? a.L + col_w : x0 + col_w;
    const float y = top + (reset ? float(per_col) : float(i % per_col)) * row_h + (reset ? 14 : 0);
    Rect r = R(a, x0, y, x1, y + row_h - 4);
    const bool focused = a.ctl_sel == int(i);
    if (Hover(a, r) && a.mouse_moved) a.ctl_sel = int(i);
    Box(r, kField, focused ? kCyan : kBorder, 5, a.s);
    if (reset) {
      BoldText(a, 12.0f, x0 + 10, y + 5, a.st.pad_map.empty() ? kMuted : kText, "Reset to the game's buttons");
      if (Clicked(a, r)) a.st.pad_map.clear();
      continue;
    }
    // What the game button does, and the controller button that does it.
    const std::string& game = names[rows[i]];
    const std::vector<std::string> buttons = PadButtonsFor(a.st.pad_map, game);
    std::string shown;
    for (const std::string& b : buttons) shown += (shown.empty() ? "" : ", ") + b;
    const bool problem = buttons.size() != 1;  // none, or two buttons (a hand-edited map)
    if (buttons.empty()) shown = "no button";
    else if (buttons.size() > 1) shown += "  (both)";
    const bool waiting = a.pad_capturing && focused;
    BoldText(a, 11.5f, x0 + 10, y + 5, kText, PadActionLabel(game));
    const std::string right = waiting ? "press a button..." : shown;
    Text(a, a.body, 12.0f, x0 + col_w - 10 - TextW(a, a.body, 12.0f, right), y + 5,
         waiting ? kCyan : problem ? kError : (buttons.size() == 1 && buttons[0] == game) ? kMuted : kCyan, right);
    if (Clicked(a, r)) {
      a.ctl_sel = int(i);
      a.pad_capturing = true;
    }
  }
  Text(a, a.body, 11.5f, a.L + 2, top + float(per_col + 2) * row_h + 32, kMuted, "B  back");
}

// ---- the UI sizes page ---------------------------------------------------

void HandleSizes(App& a, Act act) {
  const int rows = int(UiSizeKinds().size()) + 1;  // + Reset
  switch (act) {
    case Act::Up: a.size_sel = std::max(0, a.size_sel - 1); break;
    case Act::Down: a.size_sel = std::min(rows - 1, a.size_sel + 1); break;
    case Act::Left: case Act::Right:
      if (a.size_sel < rows - 1) {
        const UiSizeKind& k = UiSizeKinds()[size_t(a.size_sel)];
        int& v = a.st.*k.value;
        v = std::clamp(v + (act == Act::Left ? -10 : 10), 50, UiSizeMax(&k, a.deck));
      }
      break;
    case Act::A:
      if (a.size_sel == rows - 1) {
        for (const UiSizeKind& k : UiSizeKinds()) a.st.*k.value = 100;
      }
      break;
    case Act::B: case Act::Cancel: a.sizes = false; a.focus = Item::UiSize; break;
    default: break;
  }
}

void DrawSizes(App& a) {
  Label(a, 124, "MENUS AND HUD SIZES");
  Text(a, a.body, 12.5f, a.L + 2, 142, kText,
       "Each part on its own, on top of the overall size (" + std::to_string(a.st.ui_size) +
           "%). 100% is the game's own size; each shrinks (or on the Deck grows) from its own corner or edge. "
           "Applies when the game starts.",
       a.Rr - a.L - 4);
  const auto& kinds = UiSizeKinds();
  const float row_h = 40.0f, top = 186.0f;
  for (size_t i = 0; i <= kinds.size(); ++i) {
    const bool reset = i == kinds.size();
    const float y = top + float(i) * row_h + (reset ? 10 : 0);
    Rect r = R(a, a.L, y, a.Rr, y + row_h - 6);
    const bool focused = a.size_sel == int(i);
    if (Hover(a, r) && a.mouse_moved) a.size_sel = int(i);
    Box(r, kField, focused ? kCyan : kBorder, 6, a.s);
    if (focused) Ring(r, a.s);
    if (reset) {
      BoldText(a, 12.5f, a.L + 12, y + 9, kText, "Reset all to 100%");
      if (Clicked(a, r)) {
        for (const UiSizeKind& k : kinds) a.st.*k.value = 100;
      }
      continue;
    }
    int& v = a.st.*kinds[i].value;
    Text(a, a.body, 12.5f, a.L + 12, y + 9, kText, kinds[i].label);
    const std::string value = std::to_string(v) + "%";
    Text(a, a.body, 12.5f, a.Rr - 80 - TextW(a, a.body, 12.5f, value), y + 9, v == 100 ? kMuted : kCyan, value);
    Rect left = R(a, a.Rr - 62, y, a.Rr - 32, y + row_h - 6), right = R(a, a.Rr - 32, y, a.Rr, y + row_h - 6);
    const ImU32 arrow = focused ? kCyan : kMuted;
    Chevron(ImVec2((left.x0 + left.x1) / 2, (left.y0 + left.y1) / 2), 10 * a.s, false, arrow, a.s);
    Chevron(ImVec2((right.x0 + right.x1) / 2, (right.y0 + right.y1) / 2), 10 * a.s, true, arrow, a.s);
    const int top_size = UiSizeMax(&kinds[i], a.deck);
    if (Clicked(a, left)) a.size_sel = int(i), v = std::clamp(v - 10, 50, top_size);
    else if (Clicked(a, right)) a.size_sel = int(i), v = std::clamp(v + 10, 50, top_size);
  }
  Text(a, a.body, 11.5f, a.L + 2, top + float(kinds.size() + 1) * row_h + 24, kMuted, "B  back");
}

void DrawPicker(App& a) {
  Label(a, 124, a.picker_music ? "CHOOSE YOUR MUSIC FOLDER" : "CHOOSE YOUR AMPED 3 FOLDER");
  // The current folder, with a tick when it holds default.xex.
  bool here_valid = !a.picker_music && !a.picker_dir.empty() && GameFolderValid(a.picker_dir);
  Rect cur = R(a, a.L, 144, a.Rr, 178);
  Box(cur, kField, here_valid ? kCyan : kBorder, 6, a.s);
  std::string dir = a.picker_dir.empty() ? "Drives" : a.picker_dir;
  float avail = a.Rr - a.L - 24 - (here_valid ? 130 : 0);
  std::string shown = dir;
  while (shown.size() > 1 && TextW(a, a.body, 12.0f, "..." + shown) > avail) DropFirstChar(shown);
  if (shown != dir) shown = "..." + shown;
  Text(a, a.body, 12.0f, a.L + 10, 153, kText, shown);
  if (here_valid) {
    Tick(P(a, a.Rr - 126, 153), 16 * a.s, kCyan, a.s);
    BoldText(a, 11.5f, a.Rr - 106, 154, kCyan, "default.xex found");
  }
  // default.xex here but not the rest: a part-extracted disc.
  const bool part = !a.picker_music && !here_valid && !a.picker_dir.empty() &&
                    sys::FileExists(JoinPath(a.picker_dir, "default.xex"));
  const std::string part_note =
      part ? "default.xex is here but not the rest of the game (" + MissingGameData(a.picker_dir) +
                 " is missing): extract the whole disc again."
           : "";
  const char* note = a.picker_error ? "This folder can't be opened."
                     : a.picker_music ? "Open the folder with your music (its subfolders are played too), then choose it."
                     : here_valid   ? "This is it: choose this folder."
                     : part         ? part_note.c_str()
                                    : "Open the folder that holds default.xex, then choose it.";
  Text(a, a.body, 11.5f, a.L + 2, 183, a.picker_error || part ? kError : here_valid ? kCyan : kMuted, note);

  // The list.
  const float row_h = 30.0f, top = 206.0f;
  const int rows = PickerRows();
  int n = int(a.entries.size());
  if (a.sel < a.first_row) a.first_row = a.sel;
  if (a.sel >= a.first_row + rows) a.first_row = a.sel - rows + 1;
  Rect list = R(a, a.L, top, a.Rr, top + rows * row_h);
  float wheel = ImGui::GetIO().MouseWheel;
  if (Hover(a, list) && wheel != 0.0f) {
    a.first_row = std::clamp(a.first_row - int(wheel * 3), 0, std::max(0, n - rows));
    a.sel = std::clamp(a.sel, a.first_row, std::min(n - 1, a.first_row + rows - 1));
  }
  Box(list, kField, kBorder, 6, a.s);
  for (int i = a.first_row; i < std::min(n, a.first_row + rows); ++i) {
    float y = top + (i - a.first_row) * row_h;
    Rect r = R(a, a.L + 3, y + 2, a.Rr - 3, y + row_h - 1);
    const PickerEntry& e = a.entries[i];
    bool hover = Hover(a, r);
    if (a.mouse_moved && hover) a.sel = i;
    if (i == a.sel) {
      Box(r, kBorder, kCyan, 5, a.s);
    }
    std::string label = e.up ? ".. (up one folder)" : e.label;
    Text(a, a.body, 12.5f, a.L + 14, y + 7, e.up ? kMuted : kText, label);
    if (e.has_xex) Tick(P(a, a.Rr - 28, y + 6), 16 * a.s, kCyan, a.s);
    if (hover) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (hover && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      PickerOpen(a, i);
      break;
    }
    if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      a.sel = i;
      if (e.up) {
        PickerUp(a);
        break;
      }
    }
  }
  if (n == 0) Text(a, a.body, 12.5f, a.L + 14, top + 7, kMuted, "(empty)");

  // Buttons: Cancel, Up, Choose this folder.
  float y = 570;
  float choose_w = TextW(a, a.body, 13.0f, "Choose this folder") + 40;
  Rect choose = R(a, a.Rr - choose_w, y, a.Rr, y + 40);
  float cancel_w = TextW(a, a.body, 13.0f, "Cancel") + 32;
  Rect cancel = R(a, a.L, y, a.L + cancel_w, y + 40);
  float up_w = TextW(a, a.body, 13.0f, "Up") + 32;
  Rect up = R(a, a.Rr - choose_w - 10 - up_w, y, a.Rr - choose_w - 10, y + 40);
  bool can_choose = !a.picker_dir.empty();
  Box(cancel, kPlain, Hover(a, cancel) ? kCyan : kBorder, 6, a.s);
  BoldText(a, 13.0f, a.L + 16, y + 12, kPillText, "Cancel");
  Box(up, kPlain, Hover(a, up) ? kCyan : kBorder, 6, a.s);
  BoldText(a, 13.0f, a.Rr - choose_w - 10 - up_w + 16, y + 12, kPillText, "Up");
  ImU32 go = can_choose ? (Hover(a, choose) ? kGoHover : kGo) : (kGo & 0x00FFFFFF) | (128u << 24);
  Box(choose, go, 0, 6, a.s);
  BoldText(a, 13.0f, a.Rr - choose_w + 20, y + 12, can_choose ? kWhite : IM_COL32(255, 255, 255, 128), "Choose this folder");
  if (Hover(a, cancel) || Hover(a, up) || Hover(a, choose)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (Clicked(a, cancel)) {
    a.picker = false;
    a.focus = a.picker_music ? Item::Music : Item::GameFolder;
  } else if (Clicked(a, up)) {
    if (!a.picker_dir.empty()) PickerUp(a);
  } else if (Clicked(a, choose)) {
    PickerChoose(a);
  }
}

// The license (scrollable), then the download's progress. Skip / Cancel
// are always there: an optional package never stands between the player and
// RIDE.
void DrawVendor(App& a) {
  VendorJob& job = *a.job;
  const VendorPackage& p = *job.package;
  Label(a, 124, "GET " + p.name + (p.required ? " (NEEDED)" : " (OPTIONAL)"));
  std::string intro = p.purpose + " ReAmped 3 doesn't include " + p.maker + "'s files: this downloads them from " +
                      p.maker + "'s own release once you accept the license below (" + p.license_name + ").";
  Text(a, a.body, 12.0f, a.L + 2, 144, kText, intro, a.Rr - a.L - 4);

  const float top = 214.0f, row_h = 16.0f;
  const int rows = 21;
  Rect box = R(a, a.L, top, a.Rr, top + rows * row_h + 12);
  Box(box, kField, kBorder, 6, a.s);
  const float size = 11.0f;
  if (job.phase == VendorJob::Phase::kLicenseLoading) {
    Text(a, a.body, 12.0f, a.L + 12, top + 10, kMuted, "Getting the license from " + p.maker + "...");
  } else if (job.phase == VendorJob::Phase::kLicense) {
    // Wrapped once per width.
    const float wrap = (a.Rr - a.L - 24) * a.s;
    if (job.lines.empty() || job.wrap_w != wrap) {
      job.lines.clear();
      job.wrap_w = wrap;
      const float px = size * a.fk * a.s;
      const char* t = job.license.c_str();
      const char* end = t + job.license.size();
      while (t < end) {
        const char* nl = std::find(t, end, '\n');
        const char* line = t;
        while (line < nl) {
          const char* cut = a.body->CalcWordWrapPosition(px, line, nl, wrap);
          if (cut <= line) cut = line + 1;
          std::string piece(line, cut);
          while (!piece.empty() && (piece.back() == '\r' || piece.back() == ' ')) piece.pop_back();
          job.lines.push_back(piece);
          line = cut;
          while (line < nl && *line == ' ') ++line;
        }
        if (nl == t) job.lines.emplace_back();
        t = nl < end ? nl + 1 : end;
      }
    }
    float wheel = ImGui::GetIO().MouseWheel;
    if (Hover(a, box) && wheel != 0.0f) job.first_line -= int(wheel * 3);
    job.first_line = std::clamp(job.first_line, 0, std::max(0, int(job.lines.size()) - rows));
    for (int i = 0; i < rows && job.first_line + i < int(job.lines.size()); ++i) {
      Text(a, a.body, size, a.L + 12, top + 6 + i * row_h, kPillText, job.lines[job.first_line + i]);
    }
  } else {
    unsigned long long done = job.done, total = std::max(1ULL, job.total);
    Text(a, a.body, 12.5f, a.L + 12, top + 14, kText,
         "Downloading from " + p.maker + ": " + std::to_string(done >> 20) + " / " + std::to_string(total >> 20) + " MB");
    Rect bar = R(a, a.L + 12, top + 44, a.Rr - 12, top + 58);
    Box(bar, kNavy, kBorder, 4, a.s);
    Rect fill = bar;
    fill.x1 = bar.x0 + (bar.x1 - bar.x0) * float(double(done) / double(total));
    if (fill.x1 > fill.x0 + 2) Box(fill, kCyan, 0, 4, a.s);
    Text(a, a.body, 11.5f, a.L + 12, top + 70, kMuted, "Each file is checked against its pinned checksum before it's used.");
  }

  // Buttons: Not now / Cancel (left), Accept and download (right).
  float y = 570;
  const bool downloading = job.phase == VendorJob::Phase::kDownloading;
  const std::string skip = downloading ? "Cancel" : "Not now";
  float skip_w = TextW(a, a.body, 13.0f, skip) + 32;
  Rect skip_r = R(a, a.L, y, a.L + skip_w, y + 40);
  Box(skip_r, kPlain, Hover(a, skip_r) ? kCyan : kBorder, 6, a.s);
  BoldText(a, 13.0f, a.L + 16, y + 12, kPillText, skip);
  if (Hover(a, skip_r)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  const std::string go = "Accept and download";
  float go_w = TextW(a, a.body, 13.0f, go) + 40;
  Rect go_r = R(a, a.Rr - go_w, y, a.Rr, y + 40);
  const bool can_go = job.phase == VendorJob::Phase::kLicense;
  if (!downloading) {
    ImU32 col = can_go ? (Hover(a, go_r) ? kGoHover : kGo) : (kGo & 0x00FFFFFF) | (128u << 24);
    Box(go_r, col, 0, 6, a.s);
    BoldText(a, 13.0f, a.Rr - go_w + 20, y + 12, can_go ? kWhite : IM_COL32(255, 255, 255, 128), go);
    if (can_go && Hover(a, go_r)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }
  if (Clicked(a, skip_r)) SkipVendor(a);
  else if (can_go && Clicked(a, go_r)) StartVendorDownload(a);
}

void Frame(App& a) {
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGuiIO& io = ImGui::GetIO();
  a.width = io.DisplaySize.x;
  // Two columns on a wide screen that's small in design units (the Deck's
  // 1280x800), except while choosing the folder (its list wants the height).
  a.two_columns = !a.picker && !a.controls && !a.sizes && !a.job && io.DisplaySize.x >= io.DisplaySize.y * 1.4f;
  // The Controls page on a wide window: full width, and less tall than the
  // folder picker's layout, so its text is as large as the main page's.
  const bool wide_controls = (a.controls || a.sizes) && io.DisplaySize.x >= io.DisplaySize.y * 1.4f;
  const float dw = a.two_columns || wide_controls ? kDeckDesignW : kDesignW;
  const float dh = a.two_columns ? kDeckDesignH : wide_controls ? kWideControlsH : kDesignH;
  a.s = std::min(io.DisplaySize.x / dw, io.DisplaySize.y / dh);
  a.ox = std::floor((io.DisplaySize.x - dw * a.s) / 2);
  a.oy = a.two_columns ? kDeckHeader : 0.0f;
  a.L = kLeft;
  a.Rr = a.two_columns ? kLeft + 400.0f : wide_controls ? kDeckDesignW - kLeft : kRight;
  a.fk = a.two_columns || wide_controls ? 1.1f : 1.0f;
  PollVendor(a);
  if (a.ride_at_start && !a.launching && SDL_GetTicks() >= a.ride_at_ms) {
    a.ride_at_start = false;
    Launch(a);
  }
  if (!a.launching) {
    for (Act act : a.acts) {
      if (a.job) HandleVendor(a, act);
      else if (a.picker) HandlePicker(a, act);
      else if (a.res_open) HandleResolutionList(a, act);
      else if (a.sizes) HandleSizes(a, act);
      else if (a.controls) HandleControls(a, act);
      else HandleMain(a, act);
      if (a.launching || a.quit) break;
    }
  }
  a.acts.clear();
  // (Cleared after the input above: it moves by the last frame's layout.)
  a.rects.clear();  // filled again by this frame's Track calls
  UpdateLogo(a, FolderValid(a));
  DrawHeader(a);
  if (a.job) DrawVendor(a);
  else if (a.picker) DrawPicker(a);
  else if (a.sizes) DrawSizes(a);
  else if (a.controls) DrawControls(a);
  else DrawMain(a);
  DrawHints(a, a.two_columns ? kDeckDesignH - 36 - kDeckHeader : wide_controls ? kWideControlsH - 36 : 708);
  // The open Resolution list over everything (the hints too).
  if (a.res_open && !a.job && !a.picker && !a.controls && !a.sizes) DrawResolutionList(a);
  a.mouse_moved = false;
  ImGui::Render();
  SDL_SetRenderScale(a.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
  SDL_SetRenderDrawColor(a.renderer, 0x0F, 0x17, 0x24, 0xFF);
  SDL_RenderClear(a.renderer);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), a.renderer);
  SDL_RenderPresent(a.renderer);
}

}  // namespace

int RunLauncher() {
  App a;
  a.dist_dir = sys::ExeDir();
  a.wine = sys::RunningUnderWine();
  a.screen = sys::PrimaryDisplay();
  a.deck = IsDeck(a.wine, a.screen);
  // ReAmped3.sh's board check (a docked Deck too), or Steam's own SteamDeck=1.
  if (a.wine && (sys::EnvVar("REAMPED3_STEAM_DECK") == "1" || sys::EnvVar("SteamDeck") == "1")) a.deck = true;
  // REAMPED3_LAUNCHER_DECK=1 (fullscreen) or =window (1280x800 window): the
  // Deck's layout and frame-rate list on any PC, for checking them.
  std::string deck_env = sys::EnvVar("REAMPED3_LAUNCHER_DECK");
  bool deck_window = deck_env == "window";
  if (!deck_env.empty()) a.deck = true;
  a.fps_choices = FpsChoices(a.deck);

  if (sys::ReadFile(a.dist_dir + "version.txt", a.version)) {
    while (!a.version.empty() && (a.version.back() == '\n' || a.version.back() == '\r' || a.version.back() == ' '))
      a.version.pop_back();
  }
  std::string ini;
  a.settings_exist = sys::ReadFile(a.dist_dir + "settings.ini", ini);
  a.st = ParseSettings(a.settings_exist ? &ini : nullptr, a.deck);
  // First start on a Steam Deck: the settings measured to run best there.
  if (!a.settings_exist && a.deck) ApplyDeckRecommended(a.st);
  a.res_choices = ResolutionChoices(sys::DisplayModes(), a.st.resolution);
  if (!FolderValid(a)) {
    std::string found = AutoDetectGameDir(a.dist_dir);
    if (!found.empty()) a.st.game_path = found;
  }
  a.focus = InitialFocus(a.settings_exist, FolderValid(a));
  // DLAA saved but NVIDIA's DLSS file not fetched (skipped or removed): SMAA,
  // without asking again until DLAA is picked.
  if (const VendorPackage* dlss = FindVendorPackage("dlss")) {
    a.rtx = !a.deck && vendor::Available(*dlss);
    a.dlss_installed = vendor::Installed(*dlss, a.dist_dir);
  }
  a.st.aa = UsableAA(a.st.aa, a.dlss_installed && a.rtx);

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;
  float cs = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (cs <= 0.0f) cs = 1.0f;
  SDL_WindowFlags flags = SDL_WINDOW_HIDDEN;
  int w, h;
  if (a.deck && !deck_window) {
    flags |= SDL_WINDOW_FULLSCREEN;
    const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    w = m ? m->w : 1280;
    h = m ? m->h : 800;
  } else if (deck_window) {
    w = 1280;
    h = 800;
  } else {
    // The two-column layout (as on the Deck), half the screen's height
    // (in Windows' scaled units: a TV at 300% is ~700 tall), never over 70%.
    SDL_Rect usable;
    float height = 800.0f * cs;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)) {
      const float most = usable.h * 0.7f;
      height = std::clamp(usable.h * 0.5f, std::min(480.0f, most), most);
      // The player's size (dragged or Ctrl +/-), within that.
      if (a.st.launcher_height > 0) height = std::clamp(float(a.st.launcher_height), std::min(360.0f, most), most);
    }
    h = int(std::lround(height));
    w = int(std::lround(height * kDeckDesignW / kDeckDesignH));
  }
  if (!a.deck && !deck_window) flags |= SDL_WINDOW_RESIZABLE;
  a.window = SDL_CreateWindow("ReAmped 3", w, h, flags);
  if (!a.deck && !deck_window) SDL_SetWindowAspectRatio(a.window, kDeckDesignW / kDeckDesignH, kDeckDesignW / kDeckDesignH);
  if (!a.window) return 1;
  a.renderer = SDL_CreateRenderer(a.window, nullptr);
  if (!a.renderer) return 1;
  SDL_SetRenderVSync(a.renderer, 1);
  // The ReAmped 3 logo (logo.bmp, 32-bit with alpha) if it's there; else the
  // header draws the name in text.
  if (SDL_Surface* logo = SDL_LoadBMP((a.dist_dir + "logo.bmp").c_str())) {
    a.logo_aspect = float(logo->w) / float(std::max(1, logo->h));
    a.logo = SDL_CreateTextureFromSurface(a.renderer, logo);
    SDL_DestroySurface(logo);
  }
  SDL_SetWindowPosition(a.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  // Exo 2 from the fonts folder next to the launcher (else the built-in font).
  std::string fonts = a.dist_dir + "fonts\\";
  // SemiBold reads better on a TV and the Deck (Regular if it's missing).
  for (const char* file : {"Exo2-SemiBold.ttf", "Exo2-Regular.ttf"}) {
    if (!a.body && sys::FileExists(fonts + file)) a.body = io.Fonts->AddFontFromFileTTF((fonts + file).c_str(), 16.0f);
  }
  if (!a.body) a.body = io.Fonts->AddFontDefaultVector();
  if (sys::FileExists(fonts + "Exo2-ExtraBoldItalic.ttf")) a.display = io.Fonts->AddFontFromFileTTF((fonts + "Exo2-ExtraBoldItalic.ttf").c_str(), 38.0f);
  if (!a.display) a.display = a.body;
  io.FontDefault = a.body;
  ImGui_ImplSDL3_InitForSDLRenderer(a.window, a.renderer);
  ImGui_ImplSDLRenderer3_Init(a.renderer);

  if (!FolderValid(a)) OpenPicker(a);
  a.pad_hints = a.deck || SDL_HasGamepad();
  // --offer-vendor ID: the package's license screen straight away;
  // --open-controls: the Controls page; --open-resolutions: the Resolution
  // list; --focus=N: item N (Item order) focused (testing).
  const std::vector<std::string> args = sys::LauncherArgs();
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--offer-vendor" && i + 1 < args.size()) OfferVendor(a, args[i + 1]);
    if (args[i] == "--open-controls") a.controls = true;
    if (args[i] == "--open-keys") a.controls = a.ctl_keyboard = true;
    if (args[i].rfind("--focus=", 0) == 0) a.focus = Item(std::atoi(args[i].c_str() + 8));
    if (args[i] == "--open-resolutions") a.focus = Item::Resolution, OpenResolutionList(a);
    if (args[i] == "--open-sizes") a.sizes = true;
    if (args[i] == "--ride") a.ride_at_start = true;
    if (args[i] == "--ride_after" && i + 1 < args.size()) {
      a.ride_at_start = true;
      a.ride_at_ms = SDL_GetTicks() + 1000ull * std::strtoull(args[++i].c_str(), nullptr, 10);
    }
  }

  Frame(a);
  SDL_ShowWindow(a.window);
  SDL_RaiseWindow(a.window);
  a.mouse_ignore_until = SDL_GetTicks() + 1000;
  int fast_frames = 3;
  while (!a.quit) {
    bool busy = a.launching || a.job || a.dpad.dir >= 0 || a.stick.dir >= 0 || fast_frames > 0;
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, busy ? 16 : 250)) {
      HandleEvent(a, e);
      while (SDL_PollEvent(&e)) HandleEvent(a, e);
      fast_frames = 3;
    } else if (fast_frames > 0) {
      --fast_frames;
    }
    UpdateRepeat(a);
    if (a.quit) break;
    if (a.launching) PollLaunch(a);
    Frame(a);
  }

  a.job.reset();   // stops a download in progress
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  for (SDL_Gamepad* g : a.pads) SDL_CloseGamepad(g);
  if (a.logo) SDL_DestroyTexture(a.logo);
  SDL_DestroyRenderer(a.renderer);
  SDL_DestroyWindow(a.window);
  SDL_Quit();
  // After RIDE the sharp UI pack is finished without a window (the README's
  // "you can ride meanwhile"; used from the next start); Quit stops it (it
  // carries on at the next start).
  if (a.launching && a.pack && !a.pack->finished) {
    sys::ReleaseSingleInstance();
    while (!a.pack->finished) std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  a.pack.reset();
  // Under Proton the launcher stays (without a window) until the game exits,
  // so `proton run` and Steam see one program from start to quit.
  if (a.launching && a.wine) sys::WaitForExit(a.game);
  sys::CloseProcess(a.game);
  return 0;
}
