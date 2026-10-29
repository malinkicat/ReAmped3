// --selftest: checks of the launcher's pure logic. Prints SELFTEST OK (exit 0)
// or the first failure (exit 1).
#include <process.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "logic.h"
#include "system.h"

using namespace launcher;

namespace {

int g_failures = 0;

std::string Utf8(const std::filesystem::path& p) {
  std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("SELFTEST FAIL: %s\n", what.c_str());
    ++g_failures;
  }
}

void CheckEq(const std::string& got, const std::string& want, const std::string& what) {
  if (got != want) {
    std::printf("SELFTEST FAIL: %s\n  got:  %s\n  want: %s\n", what.c_str(), got.c_str(), want.c_str());
    ++g_failures;
  }
}

Settings Make(const std::string& res, int fps, bool fullscreen, bool tearing, const std::string& aa,
              const std::string& post, int custom = 141) {
  Settings s = DefaultSettings(false);
  s.game_path = "C:\\Games\\Amped 3";
  s.resolution = res;
  s.fps_choice = fps;
  s.custom_fps = custom;
  s.fullscreen = fullscreen;
  s.allow_tearing = tearing;
  s.aa = aa;
  s.post = post;
  return s;
}

// The command lines the WPF launcher (project/launcher/Launcher.cs) builds for
// the same settings, display and arguments.
void CommandLines() {
  const std::string dist = "C:\\ReAmped3\\";
  const std::string tail = " --mnk_mode=true --log_file=\"C:\\ReAmped3\\amped3.log\"";
  const std::string vsync = " --vsync=true --d3d12_allow_variable_refresh_rate_and_tearing=false";
  struct Case {
    const char* name;
    Settings s;
    DisplayInfo d;
    std::vector<std::string> args;
    std::string want;
  };
  std::vector<Case> cases = {
      {"4K, 60, defaults", Make("4k", 60, true, true, "none", "fsr"), {3840, 2160, 60}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=3 --draw_resolution_scale_x=3 --draw_resolution_scale_y=3"
       " --window_width=3840 --window_height=2160 --fullscreen=true --swap_post_effect=none --present_effect=fsr --framerate=60" + tail},
      {"match the display 1440p, auto 144 Hz VRR, windowed", Make("native", kAutoFps, false, true, "fxaa", "cas"), {2560, 1440, 144}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=2 --draw_resolution_scale_x=2 --draw_resolution_scale_y=2"
       " --window_width=2560 --window_height=1440 --fullscreen=false --swap_post_effect=fxaa --present_effect=cas --framerate=141" + tail},
      {"1080p, auto 144 Hz vsync, play_diag args", Make("1080p", kAutoFps, true, false, "none", "fsr"), {1920, 1080, 144},
       {"--frame_times=true", "--fps_log=true"},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=1 --draw_resolution_scale_x=1 --draw_resolution_scale_y=1"
       " --window_width=1920 --window_height=1080 --fullscreen=true --swap_post_effect=none --present_effect=fsr --framerate=144" + tail +
           vsync + " --frame_times=true --fps_log=true"},
      {"720p, unlimited, vsync", Make("720p", 0, true, false, "fxaa_extreme", "bilinear"), {1920, 1080, 60}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=1 --draw_resolution_scale_x=1 --draw_resolution_scale_y=1"
       " --window_width=1280 --window_height=720 --fullscreen=true --swap_post_effect=fxaa_extreme --present_effect=bilinear --framerate=0" +
           tail + vsync},
      {"1440p, custom 5 -> 30", Make("1440p", kCustomFps, true, false, "none", "fsr", 5), {1920, 1080, 60}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=2 --draw_resolution_scale_x=2 --draw_resolution_scale_y=2"
       " --window_width=2560 --window_height=1440 --fullscreen=true --swap_post_effect=none --present_effect=fsr --framerate=30" + tail},
      {"match the display, unknown display", Make("native", kAutoFps, true, true, "none", "fsr"), {0, 0, 0}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=1 --draw_resolution_scale_x=1 --draw_resolution_scale_y=1"
       " --window_width=1280 --window_height=720 --fullscreen=true --swap_post_effect=none --present_effect=fsr --framerate=60" + tail},
      {"4K, custom 141 VRR", Make("4k", kCustomFps, true, true, "none", "fsr", 141), {3840, 2160, 144}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=3 --draw_resolution_scale_x=3 --draw_resolution_scale_y=3"
       " --window_width=3840 --window_height=2160 --fullscreen=true --swap_post_effect=none --present_effect=fsr --framerate=141" + tail},
  };
  for (const Case& c : cases) CheckEq(BuildGameArgs(c.s, dist, c.d, c.args), c.want, std::string("command line: ") + c.name);

  // The Steam Deck: the auto-detected game folder, Match the display at
  // 1280x800, 60, and run.sh's extras passed through.
  Settings deck = DefaultSettings(true);
  deck.game_path = "Z:\\home\\deck\\ReAmped3\\game";
  CheckEq(BuildGameArgs(deck, "Z:\\home\\deck\\ReAmped3\\", {1280, 800, 60}, {"--log_file=amped3.log", "--log_flush_interval=1"}),
          "--game_data_root=\"Z:\\home\\deck\\ReAmped3\\game\" --resolution_scale=1 --draw_resolution_scale_x=1 --draw_resolution_scale_y=1"
          " --window_width=1280 --window_height=800 --fullscreen=true --swap_post_effect=none --present_effect=fsr --framerate=60"
          " --mnk_mode=true --log_file=\"Z:\\home\\deck\\ReAmped3\\amped3.log\" --log_file=amped3.log --log_flush_interval=1",
          "command line: Steam Deck defaults");

  CheckEq(BuildCommandLine("C:\\ReAmped3\\amped3.exe", "--a=1"), "\"C:\\ReAmped3\\amped3.exe\" --a=1", "exe quoting");
  CheckEq(QuoteArg("--fps_log=true"), "--fps_log=true", "plain argument");
  CheckEq(QuoteArg("--log_file=C:\\a b\\x.log"), "\"--log_file=C:\\a b\\x.log\"", "argument with a space");
  CheckEq(QuoteArg("C:\\a b\\"), "\"C:\\a b\\\\\"", "argument ending in a backslash");
}

void Scales() {
  int cases[][2] = {{720, 1}, {800, 1}, {1080, 1}, {1440, 2}, {1600, 2}, {2160, 3}, {2880, 3}, {600, 1}};
  for (auto& c : cases) {
    Check(ChooseInternalScale(c[0]) == c[1], "ChooseInternalScale(" + std::to_string(c[0]) + ")");
  }
  Check(AutoFpsFor(60, true) == 60 && AutoFpsFor(144, true) == 141 && AutoFpsFor(144, false) == 144 && AutoFpsFor(0, true) == 60,
        "AutoFpsFor");
}

void SettingsFile() {
  Settings w = ParseSettings(nullptr, false);
  Check(w.resolution == "4k" && w.fps_choice == 60 && w.fullscreen && w.allow_tearing && w.aa == "none" && w.post == "fsr" &&
            w.game_path.empty(),
        "Windows defaults");
  Settings d = ParseSettings(nullptr, true);
  Check(d.resolution == "native" && d.fps_choice == 60, "Deck defaults: Match the display, 60");

  const std::string text =
      "game_path=D:\\Games\\Amped 3\r\nresolution=1440p\r\nframerate=auto\r\nallow_tearing=false\r\n"
      "fullscreen=true\r\naa=fxaa\r\npost=cas\r\nwide_fill=false\r\n";
  Settings s = ParseSettings(&text, false);
  Check(s.game_path == "D:\\Games\\Amped 3" && s.resolution == "1440p" && s.fps_choice == kAutoFps && !s.allow_tearing &&
            s.fullscreen && s.aa == "fxaa" && s.post == "cas" && !s.wide_fill,
        "parse settings.ini");
  CheckEq(FormatSettings(s), text, "settings.ini round trip");

  const std::string odd =
      "\xEF\xBB\xBF GAME_PATH = C:\\x \nresolution=8k\nframerate=abc\nallow_tearing=TRUE\nfullscreen=yes\naa=weird\npost=\nnoise\n";
  Settings o = ParseSettings(&odd, false);
  Check(o.game_path == "C:\\x" && o.resolution == "4k" && o.fps_choice == 0 && o.allow_tearing && !o.fullscreen && o.aa == "none" &&
            o.post == "fsr",
        "parse like the WPF launcher (case, BOM, bad values)");

  const std::string f40 = "framerate=40\n";
  Settings c = ParseSettings(&f40, false);
  Check(c.fps_choice == kCustomFps && c.custom_fps == 40, "Windows: 40 is a custom cap");
  Check(ParseSettings(&f40, true).fps_choice == 40, "Deck: 40 is a preset");
  const std::string fauto = "framerate=auto\n";
  Check(ParseSettings(&fauto, true).fps_choice == 60, "Deck: auto -> 60");
  const std::string f141 = "framerate=141\n";
  Settings cu = ParseSettings(&f141, true);
  Check(cu.fps_choice == kCustomFps && cu.custom_fps == 141, "Deck: 141 is a custom cap");

  Settings low = Make("4k", kCustomFps, true, true, "none", "fsr", 5);
  Check(FormatSettings(low).find("framerate=30\r\n") != std::string::npos, "custom saved clamped");
}

void FrameRates() {
  std::vector<int> deck = FpsChoices(true);
  Check(deck.size() >= 4 && deck[0] == 30 && deck[1] == 40 && deck[2] == 60 && deck[3] == 90, "Deck list 30/40/60/90");
  Check(DefaultSettings(true).fps_choice == 60, "Deck default 60");
  Check(std::find(deck.begin(), deck.end(), kCustomFps) != deck.end() && std::find(deck.begin(), deck.end(), 0) != deck.end(),
        "Deck keeps Custom and Unlimited");
  std::vector<int> win = FpsChoices(false);
  Check(win == std::vector<int>({60, kAutoFps, 30, 90, 100, 120, 144, 0, kCustomFps}), "Windows list as before");
}

void Focus() {
  Check(InitialFocus(false, true) == Item::Resolution, "first run (auto-detected folder): focus on the first setting");
  Check(InitialFocus(true, true) == Item::Ride, "later run: focus on RIDE");
  Check(InitialFocus(true, false) == Item::GameFolder, "saved folder without default.xex: focus on the game folder");
  Check(InitialFocus(false, false) == Item::GameFolder, "first run, no folder: focus on the game folder");
}

void Deck() {
  Check(IsDeck(true, {1280, 800, 60}), "Wine + 1280x800 is the Deck");
  Check(IsDeck(true, {1280, 800, 90}), "Deck OLED at 90 Hz");
  Check(!IsDeck(true, {1920, 1080, 60}), "Wine on a bigger display is not");
  Check(!IsDeck(false, {1280, 800, 60}), "Windows at 1280x800 is not");
}

void Paths() {
  CheckEq(ParentDir("C:\\Games\\Amped 3"), "C:\\Games", "ParentDir");
  CheckEq(ParentDir("C:\\Games\\"), "C:\\", "ParentDir to the root");
  CheckEq(ParentDir("C:\\"), "", "ParentDir of a root is the drive list");
  CheckEq(ParentDir("Z:\\home\\deck"), "Z:\\home", "ParentDir under Wine");
  CheckEq(ParentDir("\\\\server\\share"), "", "ParentDir of a network share");
  CheckEq(JoinPath("C:\\", "Games"), "C:\\Games", "JoinPath at a root");
  CheckEq(JoinPath("C:\\Games", "x"), "C:\\Games\\x", "JoinPath");
  CheckEq(WineHomeDir("/home/deck", "deck"), "Z:\\home\\deck", "picker start under Wine");
  CheckEq(WineHomeDir("", "deck"), "Z:\\home\\deck", "picker start from USER");
}

// A real folder: RIDE only with default.xex; the `game` folder next to the
// launcher is found.
void Folders() {
  std::error_code ec;
  std::filesystem::path base = std::filesystem::temp_directory_path(ec) / ("reamped3_selftest_" + std::to_string(_getpid()));
  std::filesystem::path game = base / "game";
  std::filesystem::create_directories(game, ec);
  std::string dist = Utf8(base) + "\\";
  std::string dir = Utf8(game);
  Check(!GameFolderValid(""), "empty folder is not valid");
  Check(!GameFolderValid(dir), "folder without default.xex is not valid");
  Check(!RideEnabled(GameFolderValid(dir)), "RIDE disabled without default.xex");
  Check(AutoDetectGameDir(dist).empty(), "no auto-detect without default.xex");
  sys::WriteFile(JoinPath(dir, "default.xex"), "x");
  Check(GameFolderValid(dir), "folder with default.xex is valid");
  Check(RideEnabled(GameFolderValid(dir)), "RIDE enabled with default.xex");
  CheckEq(AutoDetectGameDir(dist), dir, "auto-detect the game folder next to the launcher");
  std::filesystem::remove_all(base, ec);
}

}  // namespace

int RunSelfTest() {
  sys::AttachParentConsole();
  Scales();
  CommandLines();
  SettingsFile();
  FrameRates();
  Focus();
  Deck();
  Paths();
  Folders();
  if (g_failures == 0) std::printf("SELFTEST OK\n");
  std::fflush(stdout);
  return g_failures == 0 ? 0 : 1;
}
