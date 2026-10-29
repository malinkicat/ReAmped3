// The launcher's pure logic: settings.ini, the game's command line, the focus
// rules and Steam Deck detection. Checked by --selftest (selftest.cpp).
// Strings are UTF-8.
#pragma once

#include <string>
#include <vector>

namespace launcher {

// Frame-rate choices (values as in settings.ini, plus two markers):
// 0 = unlimited, kAutoFps = just under the display's refresh rate (VRR),
// kCustomFps = Settings::custom_fps.
constexpr int kAutoFps = -2;
constexpr int kCustomFps = -1;

struct DisplayInfo {
  int width = 0;   // primary display's current mode, 0 if unknown
  int height = 0;
  int hz = 0;      // refresh rate, 0 if unknown
};

struct Settings {
  std::string game_path;
  std::string resolution;  // 720p, 1080p, 1440p, 4k, native
  int fps_choice = 60;     // one of FpsChoices()
  int custom_fps = 141;
  bool allow_tearing = true;
  bool fullscreen = true;
  std::string aa = "none";   // AAValues()
  std::string post = "fsr";  // PostValues()
  bool wide_fill = false;    // hidden option, kept as read
};

// The frame-rate list: on Windows 60 (default), Auto, 30, 90, 100, 120, 144,
// Unlimited, Custom; on the Steam Deck 30, 40, 60 (default), 90, Unlimited,
// Custom.
std::vector<int> FpsChoices(bool deck);
const std::vector<std::string>& ResolutionValues();
const std::vector<std::string>& AAValues();
const std::vector<std::string>& PostValues();

// Defaults when settings.ini is missing (or a key is): 4K on Windows, Match
// the display on the Deck; 60 FPS.
Settings DefaultSettings(bool deck);
// settings.ini text -> settings, read as the WPF launcher read it (unknown
// values fall back the same way). text == nullptr: no settings.ini.
Settings ParseSettings(const std::string* text, bool deck);
// settings -> settings.ini text (same keys, order and values as before).
std::string FormatSettings(const Settings& s);

// Match the display: the largest internal scale (the guest renders 720 lines)
// that fits the display height.
int ChooseInternalScale(int display_height);
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
std::string QuoteArg(const std::string& arg);

// Focus targets on the main screen.
enum class Item { GameFolder, Resolution, FrameRate, CustomFps, Fullscreen, Tearing, AA, Scaling, Quit, Ride };
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

// A game folder is one that holds default.xex.
bool GameFolderValid(const std::string& path);
// The `game` folder next to the launcher, if it holds default.xex; else "".
std::string AutoDetectGameDir(const std::string& dist_dir);

}  // namespace launcher
