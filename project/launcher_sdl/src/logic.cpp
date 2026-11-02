#include "logic.h"

#include <algorithm>
#include <climits>
#include <cstdio>

#include "system.h"

namespace launcher {

namespace {

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && IsSpace(s[b])) ++b;
  while (e > b && IsSpace(s[e - 1])) --e;
  return s.substr(b, e - b);
}

std::string Lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  }
  return s;
}

// int.TryParse: optional sign and digits; on failure the result is 0.
int ParseInt(const std::string& text) {
  std::string s = Trim(text);
  size_t i = 0;
  bool negative = false;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) negative = s[i++] == '-';
  if (i == s.size()) return 0;
  long long value = 0;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return 0;
    value = value * 10 + (s[i] - '0');
    if (value > (long long)INT_MAX + 1) return 0;
  }
  if (negative) value = -value;
  if (value > INT_MAX || value < INT_MIN) return 0;
  return int(value);
}

// bool.TryParse: "true" / "false" in any case; on failure the result is false.
bool ParseBool(const std::string& text) { return Lower(Trim(text)) == "true"; }

bool Contains(const std::vector<std::string>& list, const std::string& value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

bool IsSep(char c) { return c == '\\' || c == '/'; }

}  // namespace

std::vector<int> FpsChoices(bool deck) {
  // (No frame generation for now: with only the camera's motion known, the
  // riders and the game's HUD come out doubled. BuildGameArgs still handles
  // kFrameGenFps for when they have motion vectors of their own.)
  if (deck) return {30, 40, 60, 90, 0, kCustomFps};
  return {30, 60, kAutoFps, 90, 100, 120, 144, 0, kCustomFps};
}

const std::vector<std::string>& ResolutionValues() {
  static const std::vector<std::string> v = {"720p", "1080p", "1440p", "4k", "native"};
  return v;
}
bool ParseSize(const std::string& v, int& w, int& h) {
  const size_t x = v.find('x');
  if (x == std::string::npos || x == 0 || x + 1 >= v.size()) return false;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i != x && (v[i] < '0' || v[i] > '9')) return false;
  }
  w = std::atoi(v.c_str());
  h = std::atoi(v.c_str() + x + 1);
  return w >= 640 && h >= 360 && w <= 16384 && h <= 16384;
}

std::vector<std::string> ResolutionChoices(const std::vector<std::pair<int, int>>& modes, const std::string& current) {
  std::vector<std::pair<int, int>> sizes;
  for (const auto& m : modes) {
    if (m.first >= 1280 && m.second >= 720 && std::find(sizes.begin(), sizes.end(), m) == sizes.end()) sizes.push_back(m);
  }
  std::sort(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) {
    return a.first * a.second != b.first * b.second ? a.first * a.second > b.first * b.second : a.first > b.first;
  });
  std::vector<std::string> v = {"native"};
  for (const auto& [w, h] : sizes) v.push_back(std::to_string(w) + "x" + std::to_string(h));
  if (!current.empty() && std::find(v.begin(), v.end(), current) == v.end()) v.push_back(current);
  return v;
}

const std::vector<std::string>& AAValues() {
  // (No DLAA: without motion vectors for the riders and trails it ghosts them;
  // the runtime keeps --dlss_mode for when it has them.)
  // (No FXAA Extreme either: FXAA and SMAA cover it.)
  static const std::vector<std::string> v = {"none", "fxaa", "smaa"};
  return v;
}
const std::vector<std::string>& MusicValues() {
  // (No "taking turns" any more: confusing, and unreliable in testing. A saved
  // "mix" reads as your music files.)
  static const std::vector<std::string> v = {"game", "mine", "system"};
  return v;
}

const std::vector<UiSizeKind>& UiSizeKinds() {
  static const std::vector<UiSizeKind> kinds = {
      {&Settings::ui_size_menus, "menus", "Menus", false},
      {&Settings::ui_size_hud, "hud", "HUD (score, meters, timers)", true},
      {&Settings::ui_size_tips, "tips", "Help tips", true},
      {&Settings::ui_size_music, "music", "Radio / music popup", true},
      {&Settings::ui_size_legends, "legends", "Menus' button legends", true}};
  return kinds;
}

int UiSizeMax(const UiSizeKind* kind, bool deck) { return kind && kind->corner && deck ? 150 : 100; }

const std::vector<int>& SharpenValues() {
  static const std::vector<int> v = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
  return v;
}

Settings DefaultSettings(bool deck) {
  Settings s;
  s.resolution = "native";  // match the display
  // Single-pass rendering has no MSAA: SMAA by default (cheap, sharp).
  s.aa = "smaa";
  s.deck = deck;
  return s;
}

void ApplyDeckRecommended(Settings& s) {
  // Measured on the Deck (OLED, Proton 10): 60.0 FPS average, 1% low 58.5 at
  // the open world's busiest spot, filling its 16:10 screen.
  s.resolution = "native";
  s.fps_choice = 60;
  s.aa = "smaa";
  s.single_pass = true;
  s.allow_tearing = false;
  s.fullscreen = true;
  // Help tips' text is ~12 px tall at 800p: 130% reads clearly and stays crisp.
  s.ui_size_tips = 130;
}

Settings ParseSettings(const std::string* text, bool deck) {
  Settings s = DefaultSettings(deck);
  int framerate = 30;  // the game's own
  // settings.ini from before single-pass rendering: no anti-aliasing was the
  // default then (the game's MSAA was on) - now SMAA is.
  bool saw_single_pass = false;
  if (text) {
    std::string all = *text;
    if (all.size() >= 3 && all.compare(0, 3, "\xEF\xBB\xBF") == 0) all.erase(0, 3);
    size_t pos = 0;
    while (pos <= all.size()) {
      size_t end = all.find_first_of("\r\n", pos);
      if (end == std::string::npos) end = all.size();
      std::string line = all.substr(pos, end - pos);
      pos = end + 1;
      if (end < all.size() && all[end] == '\r' && pos < all.size() && all[pos] == '\n') ++pos;
      size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string key = Lower(Trim(line.substr(0, eq)));
      std::string val = Trim(line.substr(eq + 1));
      if (key == "game_path") s.game_path = val;
      else if (key == "resolution") {
        // The old presets are sizes now (drawn the same way).
        int w = 0, h = 0;
        s.resolution = val == "720p" ? "1280x720" : val == "1080p" ? "1920x1080" : val == "1440p" ? "2560x1440"
                       : val == "4k" ? "3840x2160" : val == "native" || ParseSize(val, w, h) ? val : "native";
      }
      else if (key == "framerate")
        framerate = Lower(val) == "auto" ? kAutoFps : Lower(val) == "framegen" ? kFrameGenFps : ParseInt(val);
      else if (key == "allow_tearing") s.allow_tearing = ParseBool(val);
      else if (key == "fullscreen") s.fullscreen = ParseBool(val);
      else if (key == "aa") s.aa = val == "fxaa_extreme" ? "fxaa" : Contains(AAValues(), val) ? val : AAValues()[0];
      else if (key == "sharpen") s.sharpen = std::clamp(ParseInt(val), 0, 100) / 10 * 10;
      else if (key == "wide_fill") s.wide_fill = ParseBool(val);
      else if (key == "music_volume") s.music_volume = std::clamp(ParseInt(val), 0, 100);
      else if (key == "ui_size") s.ui_size = std::clamp(ParseInt(val), 50, UiSizeMax(nullptr, deck));
      else if (key.rfind("ui_size_", 0) == 0) {
        for (const UiSizeKind& k : UiSizeKinds()) {
          if (key == std::string("ui_size_") + k.id) s.*k.value = std::clamp(ParseInt(val), 50, UiSizeMax(&k, deck));
        }
      }
      else if (key == "launcher_height") s.launcher_height = std::clamp(ParseInt(val), 0, 8640);
      else if (key == "music") s.music = val == "mix" ? "mine" : Contains(MusicValues(), val) ? val : MusicValues()[0];
      else if (key == "single_pass") s.single_pass = ParseBool(val), saw_single_pass = true;
      else if (key == "screen_fog") s.screen_fog = ParseBool(val);
      else if (key == "falling_snow") s.falling_snow = ParseBool(val);
      else if (key == "board_spray") s.board_spray = ParseBool(val);
      else if (key == "sharp_ui") s.sharp_ui = ParseBool(val);
      else if (key == "reamped_logo") s.reamped_logo = ParseBool(val);
      else if (key == "music_dir") s.music_dir = val;
      else if (key == "pad_map") s.pad_map = val;
      else if (key.rfind("key_", 0) == 0) SetKeys(s, key.substr(4), val);
      if (end == all.size()) break;
    }
  }
  if (text && !saw_single_pass && s.aa == "none") {
    s.aa = "smaa";
  }
  std::vector<int> choices = FpsChoices(deck);
  if (std::find(choices.begin(), choices.end(), framerate) != choices.end()) {
    s.fps_choice = framerate;
  } else if (framerate == kAutoFps || framerate == kFrameGenFps) {
    s.fps_choice = 60;  // no Auto on the Deck (its panel is the display); frame generation withdrawn
  } else {
    s.fps_choice = kCustomFps;
    s.custom_fps = framerate;
  }
  return s;
}

const std::vector<VendorPackage>& VendorPackages() {
  // NVIDIA DLSS (DLAA): nvngx_dlss.dll from NVIDIA's DLSS SDK on GitHub,
  // v310.9.1, the commit the GPU backend is built against.
  static const std::string kDlss = "https://raw.githubusercontent.com/NVIDIA/DLSS/374959484e79a640feaba44c93ac8cfb0a03f5b5/";
  static const std::vector<VendorPackage> packages = {
      {"dlss",
       "NVIDIA DLSS",
       "NVIDIA",
       "NVIDIA DLAA anti-aliasing on GeForce RTX graphics cards.",
       "NVIDIA RTX SDKs license",
       {kDlss + "LICENSE.txt", "d4216e39ebef5f9b50a6712ebb37beeb5379862a67733a9999c651f21592aaf0", 26620,
        "nvngx_dlss.license.txt"},
       {{kDlss + "lib/Windows_x86_64/rel/nvngx_dlss.dll",
         "3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983", 58956912, "nvngx_dlss.dll"}},
       VendorNeeds::kNvidiaRtx,
       false},
  };
  return packages;
}

const VendorPackage* FindVendorPackage(const std::string& id) {
  for (const VendorPackage& p : VendorPackages()) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

std::string VendorDir(const std::string& dist_dir, const std::string& id) {
  return JoinPath(JoinPath(dist_dir, "vendor"), id) + "\\";
}

bool ValidSha256(const std::string& hex) {
  if (hex.size() != 64) return false;
  for (char c : hex) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool IsNvidiaRtx(unsigned vendor_id, const std::string& description) {
  return vendor_id == 0x10DE && description.find("RTX") != std::string::npos;
}

std::string UsableAA(const std::string& aa, bool) {
  return aa == "dlaa" ? "smaa" : aa;  // DLAA is no longer offered: a saved choice becomes SMAA
}

std::string FormatSettings(const Settings& s) {
  auto b = [](bool v) { return std::string(v ? "true" : "false"); };
  std::string fps = s.fps_choice == kAutoFps       ? "auto"
                    : s.fps_choice == kFrameGenFps ? "framegen"
                                                   : std::to_string(SelectedFps(s, 0));
  std::string out;
  out += "game_path=" + TrimPath(s.game_path) + "\r\n";
  out += "resolution=" + s.resolution + "\r\n";
  out += "framerate=" + fps + "\r\n";
  out += "allow_tearing=" + b(s.allow_tearing) + "\r\n";
  out += "fullscreen=" + b(s.fullscreen) + "\r\n";
  out += "aa=" + s.aa + "\r\n";
  out += "sharpen=" + std::to_string(s.sharpen) + "\r\n";
  out += "wide_fill=" + b(s.wide_fill) + "\r\n";
  out += "single_pass=" + b(s.single_pass) + "\r\n";
  out += "music=" + s.music + "\r\n";
  if (s.music_volume != 100) out += "music_volume=" + std::to_string(s.music_volume) + "\r\n";
  if (s.ui_size != 100) out += "ui_size=" + std::to_string(s.ui_size) + "\r\n";
  for (const UiSizeKind& k : UiSizeKinds()) {
    if (s.*k.value != 100) out += std::string("ui_size_") + k.id + "=" + std::to_string(s.*k.value) + "\r\n";
  }
  if (s.launcher_height > 0) out += "launcher_height=" + std::to_string(s.launcher_height) + "\r\n";
  if (!s.screen_fog) out += "screen_fog=false\r\n";
  if (!s.falling_snow) out += "falling_snow=false\r\n";
  if (!s.board_spray) out += "board_spray=false\r\n";
  out += "sharp_ui=" + b(s.sharp_ui) + "\r\n";
  if (s.reamped_logo) out += "reamped_logo=true\r\n";
  if (!s.music_dir.empty()) out += "music_dir=" + s.music_dir + "\r\n";
  if (!s.pad_map.empty()) out += "pad_map=" + s.pad_map + "\r\n";
  for (const auto& [id, keys] : s.keys) out += "key_" + id + "=" + keys + "\r\n";
  return out;
}

const std::vector<KeyAction>& KeyActions() {
  // The runtime's defaults (mnk_input_driver.cpp).
  // Labels: what each does, from the game's own trick book (GameInfo.res
  // TrickBookData.lua with the string table's TrickBookB_*_buttons glyphs).
  static const std::vector<KeyAction> actions = {
      {"lstick_up", "Lean forward / front flip", "W", "Left stick up"},
      {"lstick_down", "Slow down, stop / back flip", "S", "Left stick down"},
      {"lstick_left", "Steer left / spin left", "A", "Left stick left"},
      {"lstick_right", "Steer right / spin right", "D", "Left stick right"},
      {"a", "Jump (hold: crouch) / tail grab", "Space", "A"},
      {"b", "Indy grab / rail and butter tricks", "L", "B"},
      {"x", "Melon grab / rail and butter tricks", "J", "X"},
      {"y", "Nose grab / rail and butter tricks", "K", "Y"},
      {"left_trigger", "Backside nata spin (both triggers: tuck)", "U", "LT"},
      {"right_trigger", "Frontside nata spin (both triggers: tuck)", "O", "RT"},
      {"left_shoulder", "Bonk (don't lock onto rails)", "I", "LB"},
      {"right_shoulder", "Throw snowball / bonk", "P", "RB"},
      {"rstick_up", "Look up", "Up", "Right stick up"},
      {"rstick_down", "Look down", "Down", "Right stick down"},
      {"rstick_left", "Look left", "Left", "Right stick left"},
      {"rstick_right", "Look right", "Right", "Right stick right"},
      {"dpad_down", "Snowball mode", "G", "D-pad down"},
      {"dpad_left", "Previous song", "F", "D-pad left"},
      {"dpad_right", "Next song", "H", "D-pad right"},
      {"back", "Reset when stuck / quick save (menus)", "Backspace", "Back"},
      {"start", "Pause", "Return", "Start"},
      {"dpad_up", "D-pad up", "T", "D-pad up", false},
      {"lstick_press", "Left stick press", "C", "Left stick press", false},
      {"rstick_press", "Right stick press", "V", "Right stick press", false}};
  return actions;
}

size_t ShownKeyActions() {
  size_t n = 0;
  while (n < KeyActions().size() && KeyActions()[n].shown) ++n;
  return n;
}

std::string PadActionLabel(const std::string& game_button) {
  const std::string pad = game_button == "Up" || game_button == "Down" || game_button == "Left" || game_button == "Right"
                              ? "D-pad " + Lower(game_button)
                              : game_button;
  for (const KeyAction& a : KeyActions()) {
    if (a.shown && pad == a.pad) return a.label;
  }
  return {};
}

const std::vector<size_t>& PadRows() {
  static const std::vector<size_t> rows = [] {
    std::vector<size_t> v;
    for (size_t i = 0; i < PadButtonNames().size(); ++i) {
      if (!PadActionLabel(PadButtonNames()[i]).empty()) v.push_back(i);
    }
    return v;
  }();
  return rows;
}

std::string KeysFor(const Settings& s, const std::string& id) {
  for (const auto& [k, v] : s.keys) {
    if (k == id) return v;
  }
  for (const KeyAction& a : KeyActions()) {
    if (id == a.id) return a.keys;
  }
  return {};
}

void SetKeys(Settings& s, const std::string& id, const std::string& keys) {
  const KeyAction* action = nullptr;
  for (const KeyAction& a : KeyActions()) {
    if (id == a.id) action = &a;
  }
  if (!action) return;  // not an action this launcher knows
  s.keys.erase(std::remove_if(s.keys.begin(), s.keys.end(), [&](const auto& kv) { return kv.first == id; }),
               s.keys.end());
  if (keys != action->keys) s.keys.emplace_back(id, keys);
}

const std::vector<std::string>& PadButtonNames() {
  static const std::vector<std::string> names = {"A",  "B",     "X",    "Y",  "LB", "RB",   "Back",
                                                 "Start", "LS", "RS", "Up", "Down", "Left", "Right"};
  return names;
}

std::string PadMapGet(const std::string& map, const std::string& physical) {
  size_t pos = 0;
  while (pos <= map.size()) {
    const size_t end = std::min(map.find(',', pos), map.size());
    const std::string pair = map.substr(pos, end - pos);
    const size_t eq = pair.find('=');
    if (eq != std::string::npos && Lower(pair.substr(0, eq)) == Lower(physical)) {
      for (const std::string& n : PadButtonNames()) {
        if (Lower(n) == Lower(pair.substr(eq + 1))) return n;
      }
    }
    pos = end + 1;
  }
  return physical;
}

std::string PadMapSet(const std::string& map, const std::string& physical, const std::string& target) {
  std::string out;
  for (const std::string& n : PadButtonNames()) {
    const std::string to = Lower(n) == Lower(physical) ? target : PadMapGet(map, n);
    if (to == n) continue;
    if (!out.empty()) out += ",";
    out += n + "=" + to;
  }
  return out;
}

std::vector<std::string> PadButtonsFor(const std::string& map, const std::string& game) {
  std::vector<std::string> out;
  for (const std::string& n : PadButtonNames()) {
    if (PadMapGet(map, n) == game) out.push_back(n);
  }
  return out;
}

std::string PadBind(const std::string& map, const std::string& game, const std::string& physical) {
  const std::vector<std::string> before = PadButtonsFor(map, game);
  const std::string was = PadMapGet(map, physical);
  std::string out = PadMapSet(map, physical, game);
  for (const std::string& q : before) {
    if (q != physical) out = PadMapSet(out, q, was);
  }
  return out;
}

std::vector<std::vector<size_t>> KeyConflicts(const Settings& s) {
  const std::vector<KeyAction>& actions = KeyActions();
  std::vector<std::vector<std::string>> keys(actions.size());
  for (size_t i = 0; i < actions.size(); ++i) {
    const std::string list = KeysFor(s, actions[i].id);
    size_t pos = 0;
    while (pos <= list.size()) {
      const size_t end = std::min(list.find(',', pos), list.size());
      if (end > pos) keys[i].push_back(Lower(list.substr(pos, end - pos)));
      pos = end + 1;
    }
  }
  std::vector<std::vector<size_t>> out(actions.size());
  for (size_t i = 0; i < actions.size(); ++i) {
    for (size_t j = 0; j < actions.size(); ++j) {
      if (i == j) continue;
      bool shared = false;
      for (const std::string& k : keys[i]) shared |= std::find(keys[j].begin(), keys[j].end(), k) != keys[j].end();
      if (shared) out[i].push_back(j);
    }
  }
  return out;
}

int ChooseInternalScale(int display_height, bool round_up) {
  if (round_up) {
    for (int scale = 1; scale < 3; ++scale) {
      if (720 * scale >= display_height) return scale;
    }
    return 3;
  }
  for (int scale = 3; scale > 1; --scale) {
    if (720 * scale <= display_height) return scale;
  }
  return 1;
}

int AutoFpsFor(int hz, bool vrr) {
  if (hz <= 61) return 60;
  return vrr ? hz - 3 : hz;
}

int SelectedFps(const Settings& s, int hz) {
  if (s.fps_choice == kAutoFps) return AutoFpsFor(hz, s.allow_tearing);
  if (s.fps_choice == kFrameGenFps) return hz > 61 ? hz / 2 : 30;
  if (s.fps_choice >= 0) return s.fps_choice;
  return std::min(1000, std::max(30, s.custom_fps));
}

RenderSize RenderSizes(const Settings& s, const DisplayInfo& display) {
  // The same choices as BuildGameArgs.
  int width = 1280, height = 720, scale = 1;
  if (s.resolution == "1080p") {
    width = 1920; height = 1080; scale = s.deck ? 1 : 2;
  } else if (s.resolution == "1440p") {
    width = 2560; height = 1440; scale = 2;
  } else if (s.resolution == "4k") {
    width = 3840; height = 2160; scale = 3;
  } else if (s.resolution == "native" && display.width > 0 && display.height > 0) {
    width = display.width;
    height = display.height;
    scale = ChooseInternalScale(display.height, !s.deck);
  } else if (int w = 0, h = 0; ParseSize(s.resolution, w, h)) {
    width = w;
    height = h;
    scale = ChooseInternalScale(h, !s.deck);
  }
  int scale_x = scale;
  if (double(width) / height > 16.0 / 9.0 * 1.02) scale_x = std::clamp((width + 1279) / 1280, scale, 4);
  return {1280 * scale_x, 720 * scale, width, height};
}

std::string BuildGameArgs(const Settings& s, const std::string& dist_dir, const DisplayInfo& display,
                          const std::vector<std::string>& launcher_args) {
  // The guest renders 1280x720; the scale multiplies the internal resolution.
  int width = 1280, height = 720, scale = 1;
  // On a PC, a display between steps is drawn at the step above and scaled
  // down (sharp); the Deck keeps the step below (its GPU budget).
  if (s.resolution == "1080p") {
    width = 1920; height = 1080; scale = s.deck ? 1 : 2;
  } else if (s.resolution == "1440p") {
    width = 2560; height = 1440; scale = 2;
  } else if (s.resolution == "4k") {
    width = 3840; height = 2160; scale = 3;
  } else if (s.resolution == "native") {
    if (display.width > 0 && display.height > 0) {
      width = display.width;
      height = display.height;
      scale = ChooseInternalScale(display.height, !s.deck);
    }
  } else if (int w = 0, h = 0; ParseSize(s.resolution, w, h)) {
    width = w;
    height = h;
    scale = ChooseInternalScale(h, !s.deck);
  }
  int fps = SelectedFps(s, display.hz);
  bool high_fps = fps == 0 || fps > 60;
  // Ultrawide (wider than 16:9): the game's 16:9 frame is drawn that much
  // wider and its scene and UI squeezed to match (--ultrawide_squeeze), then
  // stretched to the screen: a wider view, nothing stretched, menus centred.
  int scale_x = scale;
  const double aspect = double(width) / height;
  const bool ultrawide = aspect > 16.0 / 9.0 * 1.02;
  if (ultrawide) scale_x = std::clamp((width + 1279) / 1280, scale, 4);
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                " --resolution_scale=%d --draw_resolution_scale_x=%d --draw_resolution_scale_y=%d"
                " --window_width=%d --window_height=%d --fullscreen=%s",
                scale, scale_x, scale, width, height, s.fullscreen ? "true" : "false");
  std::string args = "--game_data_root=\"" + TrimPath(s.game_path) + "\"" + buf;
  if (ultrawide) {
    std::snprintf(buf, sizeof(buf), " --present_letterbox=false --ultrawide_squeeze=%.4f", 16.0 / 9.0 / aspect);
    args += buf;
  } else if (aspect < 16.0 / 9.0 * 0.98) {
    // Taller than 16:9 (16:10: the Steam Deck, many laptops): the same,
    // vertically - a taller view filling the screen instead of bars.
    std::snprintf(buf, sizeof(buf), " --present_letterbox=false --tall_squeeze=%.4f", aspect / (16.0 / 9.0));
    args += buf;
  }
  // NVIDIA DLAA (RTX GPUs) replaces the post-process AA; it needs the
  // single-pass frame (below). Without an RTX GPU the game shows no AA.
  args += " --swap_post_effect=" + std::string(s.aa == "dlaa" ? "none" : s.aa);
  // Sharpening (AMD CAS, which also shrinks a larger frame well); off and the
  // frame the screen's size: a plain copy. (FSR 1 only differs when upscaling,
  // which no preset does.)
  const RenderSize size = RenderSizes(s, display);
  if (s.sharpen == 0 && size.draw_w == size.out_w && size.draw_h == size.out_h) {
    args += " --present_effect=bilinear";
  } else {
    std::snprintf(buf, sizeof(buf), " --present_effect=cas --present_cas_additional_sharpness=%.1f", s.sharpen / 100.0);
    args += buf;
  }
  if (s.aa == "dlaa") args += " --dlss_mode=dlaa";
  args += " --framerate=" + std::to_string(fps) + " --mnk_mode=true";
  // Frame generation: its motion comes from the single-pass frame, and both of
  // each frame's presents wait for vsync.
  const bool frame_generation = s.fps_choice == kFrameGenFps;
  if (frame_generation) args += " --frame_generation=true";
  args += std::string(" --gpu_tile_collapse=") +
          (s.single_pass || s.aa == "dlaa" || frame_generation ? "true" : "false");
  // The player's own music from the music folder next to the game.
  args += std::string(" --user_music=") + (s.music == "mine" ? "on" : s.music == "system" ? "system" : "off");
  // UI sizes (the game's UI patch scales each scene from its own anchors).
  if (s.ui_size != 100) args += " --ui_size=" + std::to_string(s.ui_size);
  for (const UiSizeKind& k : UiSizeKinds()) {
    if (s.*k.value != 100) args += std::string(" --ui_size_") + k.id + "=" + std::to_string(s.*k.value);
  }
  if (s.music_volume != 100) {
    args += " --user_music_volume=" + std::to_string(s.music_volume) + " --title_music_volume=" +
            std::to_string(s.music_volume);
  }
  if (!s.screen_fog) args += " --screen_fog=false";
  if (!s.falling_snow) args += " --falling_snow=false";
  if (!s.board_spray) args += " --board_spray=false";
  if (!s.sharp_ui) args += " --sharp_ui=false";
  if (s.reamped_logo) args += " --title_logo=true";
  if (!s.music_dir.empty() && s.music == "mine") args += " --user_music_dir=\"" + s.music_dir + "\"";
  if (!s.pad_map.empty()) args += " --pad_button_map=" + s.pad_map;
  for (const auto& [id, keys] : s.keys) args += " --keybind_" + id + "=" + (keys.empty() ? "\"\"" : keys);
  // Anisotropic filtering: 16x (free at 60 on the Deck); 8x on the Deck above
  // 60, where the GPU is near its limit (16x cost ~5 FPS on the 1% low at 90).
  args += std::string(" --anisotropic_override=") + (s.deck && (fps == 0 || fps > 60) ? "4" : "5");
  args += " --log_file=\"" + JoinPath(dist_dir, "amped3.log") + "\"";
  // Above 60 the game picks tearing on by default; only override when turned off.
  if ((high_fps && !s.allow_tearing) || frame_generation) {
    args += " --vsync=true --d3d12_allow_variable_refresh_rate_and_tearing=false";
  }
  // The launcher's own arguments go to the game (play_diag.bat, the Deck's run.sh).
  for (const std::string& a : launcher_args) args += " " + QuoteArg(a);
  return args;
}

std::string BuildCommandLine(const std::string& exe, const std::string& args) {
  std::string cmd = "\"" + exe + "\"";
  if (!args.empty()) cmd += " " + args;
  return cmd;
}

std::string QuoteArg(const std::string& arg) {
  if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
  std::string out = "\"";
  size_t slashes = 0;
  for (char c : arg) {
    if (c == '\\') {
      ++slashes;
      continue;
    }
    if (c == '"') out.append(slashes * 2 + 1, '\\');
    else out.append(slashes, '\\');
    slashes = 0;
    out += c;
  }
  out.append(slashes * 2, '\\');
  out += "\"";
  return out;
}

Item InitialFocus(bool settings_exist, bool folder_valid) {
  if (!folder_valid) return Item::GameFolder;
  return settings_exist ? Item::Ride : Item::Resolution;
}

bool RideEnabled(bool folder_valid) { return folder_valid; }

bool IsDeck(bool under_wine, const DisplayInfo& display) {
  return under_wine && display.width == 1280 && display.height == 800;
}

std::string JoinPath(const std::string& dir, const std::string& name) {
  if (dir.empty()) return name;
  if (IsSep(dir.back())) return dir + name;
  return dir + "\\" + name;
}

std::string ParentDir(const std::string& path) {
  std::string p = TrimPath(path);
  while (p.size() > 1 && IsSep(p.back()) && !(p.size() == 3 && p[1] == ':')) p.pop_back();
  if (p.size() <= 3 && p.size() >= 2 && p[1] == ':') return "";  // X: or X:\ -> drive list
  size_t pos = p.find_last_of("\\/");
  if (pos == std::string::npos) return "";
  std::string parent = p.substr(0, pos);
  if (parent.size() == 2 && parent[1] == ':') return parent + "\\";
  // \\server\share is as far up as a network path goes.
  if (p.rfind("\\\\", 0) == 0 && std::count(parent.begin(), parent.end(), '\\') < 3) return "";
  return parent;
}

std::string TrimPath(const std::string& path) { return Trim(path); }

std::string WineHomeDir(const std::string& home_env, const std::string& user_env) {
  if (!home_env.empty() && home_env[0] == '/') {
    std::string out = "Z:";
    for (char c : home_env) out += c == '/' ? '\\' : c;
    while (out.size() > 3 && out.back() == '\\') out.pop_back();
    return out;
  }
  if (!user_env.empty()) return "Z:\\home\\" + user_env;
  return "Z:\\";
}

bool GameFolderValid(const std::string& path) {
  std::string p = TrimPath(path);
  return !p.empty() && sys::FileExists(JoinPath(p, "default.xex"));
}

bool IsTestedGameVersion(const std::string& path) {
  // Amped 3 (USA, NTSC-U) default.xex, title 545407D4: the one ReAmped 3's
  // code is built from.
  const std::string xex = JoinPath(TrimPath(path), "default.xex");
  return sys::FileSize(xex) == 3497984 &&
         sys::Sha256File(xex) == "5a1a241af5a915ad135602ca6e6140926aa8e0cf803e7df15a87884fcd7d7b7a";
}

std::string AutoDetectGameDir(const std::string& dist_dir) {
  std::string dir = JoinPath(dist_dir, "game");
  return GameFolderValid(dir) ? dir : "";
}

}  // namespace launcher
