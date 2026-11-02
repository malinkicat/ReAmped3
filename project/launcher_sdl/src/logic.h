// The launcher's pure logic: settings.ini, the game's command line, the focus
// rules and Steam Deck detection. Checked by --selftest (selftest.cpp).
// Strings are UTF-8.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace launcher {

// Frame-rate choices (values as in settings.ini, plus two markers):
// 0 = unlimited, kAutoFps = just under the display's refresh rate (VRR),
// kCustomFps = Settings::custom_fps, kFrameGenFps = half the display's refresh
// rate, each frame followed by a generated one (AMD FSR 3 frame generation).
constexpr int kFrameGenFps = -3;
constexpr int kAutoFps = -2;
constexpr int kCustomFps = -1;

struct DisplayInfo {
  int width = 0;   // primary display's current mode, 0 if unknown
  int height = 0;
  int hz = 0;      // refresh rate, 0 if unknown
};

struct Settings {
  std::string game_path;
  std::string resolution;  // native (match the display), WxH (one of its modes), or 720p, 1080p, 1440p, 4k
  int fps_choice = 60;     // one of FpsChoices(); 30 = the game's own, 60 the first-run default
  int custom_fps = 141;
  bool allow_tearing = true;
  bool fullscreen = true;
  std::string aa = "none";   // AAValues()
  // Sharpening, 0-100 (AMD CAS strength); 0 = a plain copy when the frame is
  // the screen's size. Shrinking a larger frame always goes through CAS.
  int sharpen = 30;
  bool wide_fill = false;    // hidden option, kept as read
  // Draw the game's tiled frame in one pass (--gpu_tile_collapse): much faster
  // in busy scenes, without the game's 2x MSAA (use FXAA or a higher scale).
  bool single_pass = true;
  // Music: game (the game's soundtrack), mine (the music folder's files
  // instead), mix (both) or system (another player, e.g. Spotify, in place of
  // the game's music while it plays) -> --user_music=off|on|mix|system.
  std::string music = "game";
  // The folder of the player's music files (mine/mix); empty: the music
  // folder next to the game -> --user_music_dir.
  std::string music_dir;
  // UI sizes in percent, 50-150 (100: as on the Xbox 360, at any resolution):
  // everything, then each kind's own on top -> --ui_size, --ui_size_<kind>.
  int ui_size = 100;  // also in the game's Options > Display
  int ui_size_menus = 100, ui_size_hud = 100, ui_size_tips = 100, ui_size_music = 100, ui_size_legends = 100;
  int music_volume = 100;  // the music volume, 0-100, whatever plays (set in the game's menu)
  int launcher_height = 0;  // the launcher window's height (0 = half the screen's)
  // Graphics toggles (set in the game's menu): the game's distance fog,
  // falling snow and board spray -> --screen_fog / --falling_snow / --board_spray.
  bool screen_fog = true;
  bool falling_snow = true;
  bool board_spray = true;
  // The sharp UI pack (ui_pack.cpp: the UI's art enlarged on this PC from the
  // game's own files); off -> --sharp_ui=false.
  bool sharp_ui = false;  // opt-in: the first build takes minutes (the launcher explains)
  // The title screen and the start-up loading screen with the ReAmped 3 logo
  // (made from the game's own) instead of Amped 3's -> --title_logo=true.
  bool reamped_logo = false;
  // Skip the 2K and Indie Built movies at start (--skip_intro).
  bool skip_intro = false;
  // Controller buttons remapped (Controls page): "A=B,B=A" (physical=what
  // the game sees), empty for none -> --pad_button_map.
  std::string pad_map;
  // Keyboard keys changed on the Controls page: action id (KeyActions) ->
  // the runtime's key names ("Space", "Shift+Up"); only those changed from
  // the default -> --keybind_<id>.
  std::vector<std::pair<std::string, std::string>> keys;
  // Running on a Steam Deck (from DefaultSettings / ParseSettings; not saved).
  bool deck = false;
};

// The controller buttons the Controls page lists (and --pad_button_map takes).
const std::vector<std::string>& PadButtonNames();
// What `physical` acts as in a pad map (itself when not remapped).
std::string PadMapGet(const std::string& map, const std::string& physical);
// The map with `physical` acting as `target` (pairs that map a button to
// itself are left out; the order follows PadButtonNames).
std::string PadMapSet(const std::string& map, const std::string& physical, const std::string& target);
// The controller buttons that act as `game` (usually one; none or several
// only in a hand-edited map).
std::vector<std::string> PadButtonsFor(const std::string& map, const std::string& game);
// "Press the button to use for `game`": `physical` now acts as it, and the
// button that did swaps to what `physical` did, so every game button keeps
// exactly one controller button.
std::string PadBind(const std::string& map, const std::string& game, const std::string& physical);
// For each keyboard action (KeyActions order), the other actions sharing one
// of its keys (empty: no conflict).
std::vector<std::vector<size_t>> KeyConflicts(const Settings& s);

// The keyboard's game actions (the runtime's keybind_<id> options).
struct KeyAction {
  const char* id;     // e.g. "a", "lstick_up"
  const char* label;  // what it does in the game, e.g. "Steer left / spin left"
  const char* keys;   // the runtime's default
  const char* pad;    // the controller button it stands for, e.g. "Left stick left"
  bool shown = true;  // false: no known use in the game (kept bound, not listed)
};
const std::vector<KeyAction>& KeyActions();
// How many of KeyActions() are listed (the unlisted ones come last).
size_t ShownKeyActions();
// The Controls page's controller rows: PadButtonNames() indices of the
// buttons with a known use in the game, and what each does.
const std::vector<size_t>& PadRows();
std::string PadActionLabel(const std::string& game_button);
// The keys an action is bound to (the setting, else the default).
std::string KeysFor(const Settings& s, const std::string& id);
// Binds an action (back to the default drops it from s.keys).
void SetKeys(Settings& s, const std::string& id, const std::string& keys);

// The frame-rate list: on Windows 30 (the game's own, default), 60, Auto, 90,
// 100, 120, 144, Unlimited, Custom; on the Steam Deck 30 (default), 40, 60,
// 90, Unlimited, Custom.
std::vector<int> FpsChoices(bool deck);
const std::vector<std::string>& ResolutionValues();
// "2560x1440" -> 2560, 1440 (false if it isn't a size).
bool ParseSize(const std::string& v, int& w, int& h);
// The Resolution drop-down: Match the display, then the display's own modes
// (largest first, at least 1280x720, one per size); the current value is
// kept in the list if the display no longer offers it.
std::vector<std::string> ResolutionChoices(const std::vector<std::pair<int, int>>& modes, const std::string& current);
const std::vector<std::string>& AAValues();
const std::vector<int>& SharpenValues();
// The size the game draws at and the window's (for the launcher's hint).
struct RenderSize {
  int draw_w, draw_h, out_w, out_h;
};
RenderSize RenderSizes(const Settings& s, const DisplayInfo& display);
const std::vector<std::string>& MusicValues();
// The UI size kinds the launcher's UI sizes page lists: the setting, its
// label and the game's option name (--ui_size_<id>).
struct UiSizeKind {
  int Settings::*value;
  const char* id;
  const char* label;
  bool corner;  // grows from a corner (larger allowed on the Steam Deck); else shrink only
};
// The largest size for a kind (or the overall size: nullptr): 100 (shrink
// only), but corner-anchored overlays up to 150 on the Steam Deck's small screen.
int UiSizeMax(const UiSizeKind* kind, bool deck);
const std::vector<UiSizeKind>& UiSizeKinds();

// Defaults when settings.ini is missing (or a key is): 4K on Windows, Match
// the display on the Deck; 30 FPS (the game's own).
Settings DefaultSettings(bool deck);
// The Steam Deck's recommended settings (the launcher's Deck button): full
// screen at its 1280x800, 60 FPS, SMAA, single-pass rendering, V-Sync on.
void ApplyDeckRecommended(Settings& s);
// settings.ini text -> settings, read as the WPF launcher read it (unknown
// values fall back the same way). text == nullptr: no settings.ini.
Settings ParseSettings(const std::string* text, bool deck);
// settings -> settings.ini text (same keys, order and values as before).
std::string FormatSettings(const Settings& s);

// Match the display: the internal scale (the guest renders 720 lines) for the
// display height: the largest that fits, or with round_up (PCs) the smallest
// that covers it (drawn larger and scaled down), up to 3.
int ChooseInternalScale(int display_height, bool round_up = false);
// With tearing allowed (VRR), a cap 3 below the refresh rate keeps VRR
// engaged (144 Hz -> 141); with vsync, the refresh rate itself. 60 Hz
// displays (and unknown ones) get 60.
int AutoFpsFor(int hz, bool vrr);
// The frame-rate cap passed to the game (0 = unlimited); custom: 30 to 1000.
int SelectedFps(const Settings& s, int hz);

// The game's arguments (after the quoted exe path), exactly as the WPF
// launcher built them; launcher_args are appended as given.
std::string BuildGameArgs(const Settings& s, const std::string& dist_dir, const DisplayInfo& display,
                          const std::vector<std::string>& launcher_args);
// The full command line: "exe" args.
std::string BuildCommandLine(const std::string& exe, const std::string& args);
// One argument for a Windows command line (quoted only when needed).
// --name="path", quoted for the Windows command line (trailing backslashes too).
std::string PathFlag(const std::string& name, const std::string& path);
std::string QuoteArg(const std::string& arg);

// Focus targets on the main screen.
enum class Item { GameFolder, Resolution, FrameRate, CustomFps, Fullscreen, SharpUi, ReampedLogo, Tearing, SkipIntro, SinglePass, AA, Scaling, Music, UiSize, Controls, Recommended, Save, Quit, Ride };
// Later runs (settings.ini exists, valid folder): RIDE. First run: the first
// setting. No valid folder: the game folder.
Item InitialFocus(bool settings_exist, bool folder_valid);
bool RideEnabled(bool folder_valid);

// The Steam Deck: running under Wine/Proton with a 1280x800 primary display.
bool IsDeck(bool under_wine, const DisplayInfo& display);

// Paths (Windows style). ParentDir of a drive root is "" (the drive list).
std::string JoinPath(const std::string& dir, const std::string& name);
std::string ParentDir(const std::string& path);
std::string TrimPath(const std::string& path);
// Where the folder picker starts under Wine: the Linux home on drive Z:.
std::string WineHomeDir(const std::string& home_env, const std::string& user_env);

// Vendor packages: third-party runtime files ReAmped 3 doesn't ship, fetched
// from their makers' own release on the player's request, after the player
// accepts their license. Each file is checked against its pinned size and
// SHA-256 before it's used. Another library is one more table entry.
struct VendorFile {
  std::string url;     // the maker's official download
  std::string sha256;  // lowercase hex
  unsigned long long size = 0;
  std::string name;    // file name in the package folder
};
enum class VendorNeeds { kAnyPc, kNvidiaRtx };
struct VendorPackage {
  std::string id;         // folder name: <launcher folder>\vendor\<id>
  std::string name;       // shown to the player
  std::string maker;      // whose files they are ("NVIDIA")
  std::string purpose;    // why it's offered
  std::string license_name;
  VendorFile license;     // shown before the download, kept with the files
  std::vector<VendorFile> files;
  VendorNeeds needs = VendorNeeds::kAnyPc;
  // Required packages may hold back RIDE; optional ones never do (skip,
  // cancel or a failed download just leave the feature off).
  bool required = false;
};
const std::vector<VendorPackage>& VendorPackages();
const VendorPackage* FindVendorPackage(const std::string& id);
std::string VendorDir(const std::string& dist_dir, const std::string& id);
// 64 lowercase hex digits.
bool ValidSha256(const std::string& hex);
// An NVIDIA (PCI vendor 0x10DE) GeForce RTX / RTX-class adapter.
bool IsNvidiaRtx(unsigned vendor_id, const std::string& description);
// The AA setting the game can use: DLAA needs the DLSS package installed;
// without it SMAA.
std::string UsableAA(const std::string& aa, bool dlss_installed);

// The first of the game's key files/folders missing from path ("" = none).
std::string MissingGameData(const std::string& path);
// A game folder holds default.xex and the rest of the game's data.
bool GameFolderValid(const std::string& path);
// The folder's default.xex is the USA (NTSC-U) one ReAmped 3 is built from
// and tested with (other versions: untested).
bool IsTestedGameVersion(const std::string& path);
// The `game` folder next to the launcher, if it holds default.xex; else "".
std::string AutoDetectGameDir(const std::string& dist_dir);

}  // namespace launcher
