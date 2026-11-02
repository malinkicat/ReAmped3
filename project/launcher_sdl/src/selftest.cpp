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
#include "ui_pack.h"

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
              int sharpen, int custom = 141) {
  Settings s = DefaultSettings(false);
  s.game_path = "C:\\Games\\Amped 3";
  s.resolution = res;
  s.fps_choice = fps;
  s.custom_fps = custom;
  s.fullscreen = fullscreen;
  s.allow_tearing = tearing;
  s.aa = aa;
  s.sharpen = sharpen;
  return s;
}

// The command lines the WPF launcher (project/launcher/Launcher.cs) builds for
// the same settings, display and arguments.
void CommandLines() {
  const std::string dist = "C:\\ReAmped3\\";
  const std::string tail =
      " --mnk_mode=true --gpu_tile_collapse=true --user_music=off --sharp_ui=false --anisotropic_override=5"
      " --log_file=\"C:\\ReAmped3\\amped3.log\"";
  const std::string vsync = " --vsync=true --d3d12_allow_variable_refresh_rate_and_tearing=false";
  struct Case {
    const char* name;
    Settings s;
    DisplayInfo d;
    std::vector<std::string> args;
    std::string want;
  };
  std::vector<Case> cases = {
      {"4K, 60, defaults", Make("4k", 60, true, true, "none", 30), {3840, 2160, 60}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=3 --draw_resolution_scale_x=3 --draw_resolution_scale_y=3"
       " --window_width=3840 --window_height=2160 --fullscreen=true --swap_post_effect=none --present_effect=cas --present_cas_additional_sharpness=0.3 --framerate=60" + tail},
      {"match the display 1440p, auto 144 Hz VRR, windowed", Make("native", kAutoFps, false, true, "fxaa", 30), {2560, 1440, 144}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=2 --draw_resolution_scale_x=2 --draw_resolution_scale_y=2"
       " --window_width=2560 --window_height=1440 --fullscreen=false --swap_post_effect=fxaa --present_effect=cas --present_cas_additional_sharpness=0.3 --framerate=141" + tail},
      {"1080p, auto 144 Hz vsync, play_diag args", Make("1080p", kAutoFps, true, false, "none", 30), {1920, 1080, 144},
       {"--frame_times=true", "--fps_log=true"},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=2 --draw_resolution_scale_x=2 --draw_resolution_scale_y=2"
       " --window_width=1920 --window_height=1080 --fullscreen=true --swap_post_effect=none --present_effect=cas --present_cas_additional_sharpness=0.3 --framerate=144" + tail +
           vsync + " --frame_times=true --fps_log=true"},
      {"720p, unlimited, vsync", Make("720p", 0, true, false, "fxaa", 0), {1920, 1080, 60}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=1 --draw_resolution_scale_x=1 --draw_resolution_scale_y=1"
       " --window_width=1280 --window_height=720 --fullscreen=true --swap_post_effect=fxaa --present_effect=bilinear --framerate=0" +
           tail + vsync},
      {"1440p, custom 5 -> 30", Make("1440p", kCustomFps, true, false, "none", 30, 5), {1920, 1080, 60}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=2 --draw_resolution_scale_x=2 --draw_resolution_scale_y=2"
       " --window_width=2560 --window_height=1440 --fullscreen=true --swap_post_effect=none --present_effect=cas --present_cas_additional_sharpness=0.3 --framerate=30" + tail},
      {"match the display, unknown display", Make("native", kAutoFps, true, true, "none", 30), {0, 0, 0}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=1 --draw_resolution_scale_x=1 --draw_resolution_scale_y=1"
       " --window_width=1280 --window_height=720 --fullscreen=true --swap_post_effect=none --present_effect=cas --present_cas_additional_sharpness=0.3 --framerate=60" + tail},
      {"4K, custom 141 VRR", Make("4k", kCustomFps, true, true, "none", 30, 141), {3840, 2160, 144}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=3 --draw_resolution_scale_x=3 --draw_resolution_scale_y=3"
       " --window_width=3840 --window_height=2160 --fullscreen=true --swap_post_effect=none --present_effect=cas --present_cas_additional_sharpness=0.3 --framerate=141" + tail},
      {"4K, frame generation at 144 Hz: 72, vsync, single pass", Make("4k", kFrameGenFps, true, true, "none", 30),
       {3840, 2160, 144}, {},
       "--game_data_root=\"C:\\Games\\Amped 3\" --resolution_scale=3 --draw_resolution_scale_x=3 --draw_resolution_scale_y=3"
       " --window_width=3840 --window_height=2160 --fullscreen=true --swap_post_effect=none --present_effect=cas --present_cas_additional_sharpness=0.3 --framerate=72"
       " --mnk_mode=true --frame_generation=true --gpu_tile_collapse=true --user_music=off --sharp_ui=false"
       " --anisotropic_override=5"
       " --log_file=\"C:\\ReAmped3\\amped3.log\"" + vsync},
  };
  for (const Case& c : cases) CheckEq(BuildGameArgs(c.s, dist, c.d, c.args), c.want, std::string("command line: ") + c.name);

  // The Steam Deck: the auto-detected game folder, Match the display at
  // 1280x800, 60, and run.sh's extras passed through.
  Settings deck = DefaultSettings(true);
  deck.game_path = "Z:\\home\\deck\\ReAmped3\\game";
  CheckEq(BuildGameArgs(deck, "Z:\\home\\deck\\ReAmped3\\", {1280, 800, 60}, {"--log_file=amped3.log", "--log_flush_interval=1"}),
          "--game_data_root=\"Z:\\home\\deck\\ReAmped3\\game\" --resolution_scale=1 --draw_resolution_scale_x=1 --draw_resolution_scale_y=1"
          " --window_width=1280 --window_height=800 --fullscreen=true --present_letterbox=false --tall_squeeze=0.9000"
          " --swap_post_effect=smaa --present_effect=cas --present_cas_additional_sharpness=0.3 --framerate=30"
          " --mnk_mode=true --gpu_tile_collapse=true --user_music=off --sharp_ui=false --anisotropic_override=5 --log_file=\"Z:\\home\\deck\\ReAmped3\\amped3.log\" --log_file=amped3.log --log_flush_interval=1",
          "command line: Steam Deck defaults");
  Settings deck90 = deck;
  deck90.fps_choice = 90;
  Check(BuildGameArgs(deck90, "Z:\\home\\deck\\ReAmped3\\", {1280, 800, 90}, {}).find(
            " --anisotropic_override=4") != std::string::npos,
        "Steam Deck above 60: 8x anisotropic filtering");

  // Sharpening off: a plain copy when the frame is the screen's size, CAS
  // (no extra sharpening) when it's shrunk; FXAA Extreme reads as FXAA.
  Check(BuildGameArgs(Make("4k", 60, true, true, "smaa", 0), dist, {3840, 2160, 60}, {}).find(" --present_effect=bilinear") !=
            std::string::npos,
        "sharpening off at 4K: plain copy");
  Check(BuildGameArgs(Make("1080p", 60, true, true, "smaa", 0), dist, {1920, 1080, 60}, {})
                .find(" --present_effect=cas --present_cas_additional_sharpness=0.0") != std::string::npos,
        "sharpening off at 1080p: CAS shrink");
  {
    const std::string old_aa = "aa=fxaa_extreme\r\npost=fsr\r\n";
    Settings o = ParseSettings(&old_aa, false);
    Check(o.aa == "fxaa" && o.sharpen == 30, "old settings: FXAA Extreme -> FXAA, post ignored");
  }
  const RenderSize r1080 = RenderSizes(Make("1080p", 60, true, true, "smaa", 30), {1920, 1080, 60});
  Check(r1080.draw_w == 2560 && r1080.draw_h == 1440 && r1080.out_w == 1920 && r1080.out_h == 1080, "render sizes 1080p");

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
  int up[][2] = {{720, 1}, {800, 2}, {1080, 2}, {1440, 2}, {1600, 3}, {2160, 3}, {2880, 3}};
  for (auto& c : up) {
    Check(ChooseInternalScale(c[0], true) == c[1], "ChooseInternalScale(" + std::to_string(c[0]) + ", up)");
  }
  Check(AutoFpsFor(60, true) == 60 && AutoFpsFor(144, true) == 141 && AutoFpsFor(144, false) == 144 && AutoFpsFor(0, true) == 60,
        "AutoFpsFor");
}

void SettingsFile() {
  Settings w = ParseSettings(nullptr, false);
  Check(w.resolution == "native" && w.fps_choice == 30 && w.fullscreen && w.allow_tearing && w.aa == "smaa" && w.sharpen == 30 &&
            w.game_path.empty(),
        "Windows defaults");
  Settings d = ParseSettings(nullptr, true);
  Check(d.resolution == "native" && d.fps_choice == 30, "Deck defaults: Match the display, 30");

  const std::string text =
      "game_path=D:\\Games\\Amped 3\r\nresolution=1440p\r\nframerate=auto\r\nallow_tearing=false\r\n"
      "fullscreen=true\r\naa=fxaa\r\nsharpen=40\r\nwide_fill=false\r\nsingle_pass=true\r\nmusic=mix\r\nsharp_ui=false\r\n";
  Settings s = ParseSettings(&text, false);
  Check(s.game_path == "D:\\Games\\Amped 3" && s.resolution == "2560x1440" && s.fps_choice == kAutoFps && !s.allow_tearing &&
            s.fullscreen && s.aa == "fxaa" && s.sharpen == 40 && !s.wide_fill && s.music == "mine",
        "parse settings.ini");
  // (An old preset is saved back as its size; "taking turns", no longer offered, as your music files.)
  std::string resaved = text;
  resaved.replace(resaved.find("resolution=1440p"), 16, "resolution=2560x1440");
  resaved.replace(resaved.find("music=mix"), 9, "music=mine");
  CheckEq(FormatSettings(s), resaved, "settings.ini round trip");

  const std::string odd =
      "\xEF\xBB\xBF GAME_PATH = C:\\x \nresolution=8k\nframerate=abc\nallow_tearing=TRUE\nfullscreen=yes\naa=weird\npost=\nnoise\n";
  Settings o = ParseSettings(&odd, false);
  Check(o.game_path == "C:\\x" && o.resolution == "native" && o.fps_choice == 0 && o.allow_tearing && !o.fullscreen && o.aa == "smaa" &&
            o.sharpen == 30,
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

  Check(ParseSettings(nullptr, false).music == "game", "music default: the game's soundtrack");
  const std::string bad_music = "music=spotify\n";
  Check(ParseSettings(&bad_music, false).music == "game", "unknown music value -> game");

  const std::string framegen_text = FormatSettings(Make("4k", kFrameGenFps, true, true, "none", 30));
  Check(framegen_text.find("framerate=framegen\r\n") != std::string::npos &&
            ParseSettings(&framegen_text, false).fps_choice == 60,
        "frame generation (withdrawn) read back as 60");
  Settings low = Make("4k", kCustomFps, true, true, "none", 30, 5);
  Check(FormatSettings(low).find("framerate=30\r\n") != std::string::npos, "custom saved clamped");
}

void PadMap() {
  std::string m = PadMapSet("", "A", "B");
  m = PadMapSet(m, "B", "A");
  CheckEq(m, "A=B,B=A", "pad map: swap A and B");
  Check(PadMapGet(m, "A") == "B" && PadMapGet(m, "b") == "A" && PadMapGet(m, "X") == "X", "pad map: read back");
  CheckEq(PadMapSet(m, "A", "A"), "B=A", "pad map: back to itself drops the pair");
  // Press-to-bind: X pressed for the game's A swaps them.
  CheckEq(PadBind("", "A", "X"), "A=X,X=A", "pad bind: swap");
  CheckEq(PadBind("A=X,X=A", "A", "A"), "", "pad bind: back to the default");
  Check(PadButtonsFor("A=X,X=A", "A") == std::vector<std::string>{"X"}, "pad bind: the button for a game button");
  {
    Settings k;
    SetKeys(k, "x", "N");
    SetKeys(k, "y", "N");
    const auto c = KeyConflicts(k);
    Check(c[6] == std::vector<size_t>{7} && c[7] == std::vector<size_t>{6} && c[0].empty(), "keys: conflicts found");
  }
  Settings s = ParseSettings(nullptr, false);
  s.pad_map = m;
  const std::string text = FormatSettings(s);
  Check(text.find("pad_map=A=B,B=A\r\n") != std::string::npos && ParseSettings(&text, false).pad_map == m,
        "pad map saved and read back");
  Check(BuildGameArgs(s, "C:\\g\\", {}, {}).find(" --pad_button_map=A=B,B=A") != std::string::npos, "pad map arg");
  // The UI size (set in the game's menu): saved, read back, passed on.
  Settings u = ParseSettings(nullptr, false);
  Check(BuildGameArgs(u, "C:\\g\\", {}, {}).find("--ui_size") == std::string::npos, "ui size: 100% passes nothing");
  u.ui_size = 80;
  u.ui_size_tips = 130;
  const std::string ui_text = FormatSettings(u);
  const Settings back = ParseSettings(&ui_text, false), deck_back = ParseSettings(&ui_text, true);
  Check(back.ui_size == 80 && back.ui_size_tips == 100 && back.ui_size_menus == 100 && deck_back.ui_size_tips == 130,
        "ui sizes saved and read back (PC: shrink only; Deck: tips up to 150)");
  const std::string ui_args = BuildGameArgs(u, "C:\\g\\", {}, {});
  Check(ui_args.find(" --ui_size=80") != std::string::npos && ui_args.find(" --ui_size_tips=130") != std::string::npos &&
            ui_args.find("--ui_size_menus") == std::string::npos && ui_args.find("--ui_scale") == std::string::npos,
        "ui size args");
  Settings rec = ParseSettings(nullptr, true);
  ApplyDeckRecommended(rec);
  Check(rec.ui_size_tips == 130 && rec.ui_size_hud == 100 && rec.ui_size_menus == 100, "Deck recommended: bigger help tips only");
}

void Keys() {
  Settings s = ParseSettings(nullptr, false);
  Check(KeysFor(s, "a") == "Space", "keys: default");
  {
    bool one_each = true;
    for (const auto& c : KeyConflicts(s)) one_each = one_each && c.empty();
    for (const KeyAction& a : KeyActions()) one_each = one_each && std::string(a.keys).find(',') == std::string::npos;
    Check(one_each, "keys: one key per action by default, no conflicts");
  }
  SetKeys(s, "a", "J");
  const std::string text = FormatSettings(s);
  Check(text.find("key_a=J\r\n") != std::string::npos && KeysFor(ParseSettings(&text, false), "a") == "J",
        "keys: saved and read back");
  Check(BuildGameArgs(s, "C:\\g\\", {}, {}).find(" --keybind_a=J") != std::string::npos, "keys: arg");
  SetKeys(s, "a", "Space");
  Check(s.keys.empty(), "keys: back to the default is dropped");
}

void Ultrawide() {
  Settings s = Make("native", 60, true, true, "smaa", 30);
  const std::string uw = BuildGameArgs(s, "C:\\g\\", {3440, 1440, 144}, {});
  Check(uw.find("--draw_resolution_scale_x=3 --draw_resolution_scale_y=2") != std::string::npos &&
            uw.find("--present_letterbox=false --ultrawide_squeeze=0.7442") != std::string::npos,
        "3440x1440: 3x2 scale, squeezed");
  const std::string uw4k = BuildGameArgs(s, "C:\\g\\", {5120, 2160, 120}, {});
  Check(uw4k.find("--draw_resolution_scale_x=4 --draw_resolution_scale_y=3") != std::string::npos &&
            uw4k.find("--ultrawide_squeeze=0.7500") != std::string::npos,
        "5120x2160: 4x3 scale");
  Check(BuildGameArgs(s, "C:\\g\\", {2560, 1440, 144}, {}).find("ultrawide") == std::string::npos, "16:9: no squeeze");
  const std::string tall = BuildGameArgs(s, "C:\\g\\", {1920, 1200, 60}, {});
  Check(tall.find("ultrawide") == std::string::npos && tall.find(" --present_letterbox=false --tall_squeeze=0.9000") != std::string::npos,
        "16:10: a taller view");
}

void SharpUi() {
  {
    int w = 0, h = 0;
    Check(ParseSize("2560x1080", w, h) && w == 2560 && h == 1080 && !ParseSize("native", w, h) && !ParseSize("4k", w, h),
          "resolution sizes parsed");
    const std::vector<std::string> list =
        ResolutionChoices({{1920, 1080}, {3840, 2160}, {800, 600}, {1920, 1080}, {2560, 1440}}, "native");
    Check(list == std::vector<std::string>({"native", "3840x2160", "2560x1440", "1920x1080"}),
          "resolution list: Match first, largest first, no duplicates, nothing under 720p");
    Settings r;
    r.resolution = "2560x1080";
    r.game_path = "C:\\g";
    const std::string args = BuildGameArgs(r, "C:\\g\\", {3840, 2160, 60}, {});
    Check(args.find(" --window_width=2560 --window_height=1080") != std::string::npos &&
              args.find("--ultrawide_squeeze=") != std::string::npos,
          "a chosen 2560x1080: that window, ultrawide");
    const std::string text = "resolution=2560x1080\n";
    Check(ParseSettings(&text, false).resolution == "2560x1080", "a chosen size is kept in settings.ini");
  }
  Check(!ParseSettings(nullptr, false).reamped_logo &&
            BuildGameArgs(ParseSettings(nullptr, false), "C:\\g\\", {}, {}).find("title_logo") == std::string::npos,
        "ReAmped logo off by default: no arg");
  {
    const std::string text = "reamped_logo=true\n";
    const Settings r = ParseSettings(&text, false);
    Check(r.reamped_logo && FormatSettings(r).find("reamped_logo=true\r\n") != std::string::npos &&
              BuildGameArgs(r, "C:\\g\\", {}, {}).find(" --title_logo=true") != std::string::npos,
          "ReAmped logo on: saved, --title_logo=true");
  }
  Check(!ParseSettings(nullptr, false).sharp_ui, "sharp UI off by default (opt-in)");
  const std::string on = "sharp_ui=true\n";
  Settings s = ParseSettings(&on, false);
  Check(s.sharp_ui && FormatSettings(s).find("sharp_ui=true\r\n") != std::string::npos, "sharp UI on saved");
  Check(BuildGameArgs(s, "C:\\g\\", {}, {}).find("sharp_ui") == std::string::npos, "sharp UI on: no arg");
  Check(BuildGameArgs(ParseSettings(nullptr, false), "C:\\g\\", {}, {}).find(" --sharp_ui=false") != std::string::npos,
        "sharp UI off -> arg");
  // Reference values from tools/xpr_extract.py tiled_offset.
  Check(uipack::TiledOffset(5, 3, 64, 16) == 99 && uipack::TiledOffset(37, 9, 64, 16) == 1579 &&
            uipack::TiledOffset(100, 40, 128, 4) == 7464,
        "Xenos tiled offsets");
}

void FrameRates() {
  std::vector<int> deck = FpsChoices(true);
  Check(deck.size() >= 4 && deck[0] == 30 && deck[1] == 40 && deck[2] == 60 && deck[3] == 90, "Deck list 30/40/60/90");
  Check(DefaultSettings(true).fps_choice == 30 && DefaultSettings(false).fps_choice == 30, "default 30 (the game's own)");
  Check(std::find(deck.begin(), deck.end(), kCustomFps) != deck.end() && std::find(deck.begin(), deck.end(), 0) != deck.end(),
        "Deck keeps Custom and Unlimited");
  std::vector<int> win = FpsChoices(false);
  Check(win == std::vector<int>({30, 60, kAutoFps, 90, 100, 120, 144, 0, kCustomFps}),
        "Windows list: 30 first, then 60, Auto, ...");
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

void VendorPackageTable() {
  std::vector<std::string> ids;
  for (const VendorPackage& p : VendorPackages()) {
    Check(!p.id.empty() && std::find(ids.begin(), ids.end(), p.id) == ids.end(), "vendor package id unique: " + p.id);
    ids.push_back(p.id);
    Check(!p.name.empty() && !p.maker.empty() && !p.purpose.empty() && !p.license_name.empty(),
          "vendor package described: " + p.id);
    std::vector<VendorFile> files = {p.license};
    files.insert(files.end(), p.files.begin(), p.files.end());
    Check(p.files.size() >= 1, "vendor package has files: " + p.id);
    for (const VendorFile& f : files) {
      Check(f.url.rfind("https://", 0) == 0, "vendor file over https: " + f.name);
      Check(ValidSha256(f.sha256) && f.size > 0 && !f.name.empty() && f.name.find_first_of("\\/") == std::string::npos,
            "vendor file pinned: " + f.name);
    }
  }
  const VendorPackage* dlss = FindVendorPackage("dlss");
  Check(dlss && !dlss->required && dlss->needs == VendorNeeds::kNvidiaRtx, "DLSS: optional, RTX only");
  Check(dlss && dlss->files.size() == 1 && dlss->files[0].name == "nvngx_dlss.dll" && dlss->files[0].size == 58956912,
        "DLSS: nvngx_dlss.dll v310.9.1");
  Check(!FindVendorPackage("nope"), "unknown vendor package");
  CheckEq(VendorDir("C:\\ReAmped3\\", "dlss"), "C:\\ReAmped3\\vendor\\dlss\\", "vendor folder");
  Check(!ValidSha256("3975567B8943C53ACCE397F2B72380092F84F162D00B0D2C7D08A1025C563983") && !ValidSha256("abc"),
        "SHA-256 must be 64 lowercase hex digits");
  Check(IsNvidiaRtx(0x10DE, "NVIDIA GeForce RTX 2080 Ti") && IsNvidiaRtx(0x10DE, "NVIDIA RTX A4000"),
        "RTX adapters");
  Check(!IsNvidiaRtx(0x10DE, "NVIDIA GeForce GTX 1080") && !IsNvidiaRtx(0x1002, "AMD Radeon RTX-like"),
        "not RTX: GTX, other vendors");
  CheckEq(UsableAA("dlaa", false), "smaa", "DLAA without DLSS -> SMAA");
  CheckEq(UsableAA("dlaa", true), "smaa", "a saved DLAA choice -> SMAA (no longer offered)");
  CheckEq(UsableAA("fxaa", false), "fxaa", "other AA unchanged");
}

int RunSelfTest() {
  sys::AttachParentConsole();
  Scales();
  CommandLines();
  SettingsFile();
  FrameRates();
  SharpUi();
  Ultrawide();
  PadMap();
  Keys();
  VendorPackageTable();
  Focus();
  Deck();
  Paths();
  Folders();
  if (g_failures == 0) std::printf("SELFTEST OK\n");
  std::fflush(stdout);
  return g_failures == 0 ? 0 : 1;
}
