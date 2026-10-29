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
  if (deck) return {30, 40, 60, 90, 0, kCustomFps};
  return {60, kAutoFps, 30, 90, 100, 120, 144, 0, kCustomFps};
}

const std::vector<std::string>& ResolutionValues() {
  static const std::vector<std::string> v = {"720p", "1080p", "1440p", "4k", "native"};
  return v;
}
const std::vector<std::string>& AAValues() {
  static const std::vector<std::string> v = {"none", "fxaa", "fxaa_extreme"};
  return v;
}
const std::vector<std::string>& PostValues() {
  static const std::vector<std::string> v = {"fsr", "cas", "bilinear"};
  return v;
}

Settings DefaultSettings(bool deck) {
  Settings s;
  s.resolution = deck ? "native" : "4k";
  return s;
}

Settings ParseSettings(const std::string* text, bool deck) {
  Settings s = DefaultSettings(deck);
  int framerate = 60;
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
      else if (key == "resolution") s.resolution = Contains(ResolutionValues(), val) ? val : "4k";
      else if (key == "framerate") framerate = Lower(val) == "auto" ? kAutoFps : ParseInt(val);
      else if (key == "allow_tearing") s.allow_tearing = ParseBool(val);
      else if (key == "fullscreen") s.fullscreen = ParseBool(val);
      else if (key == "aa") s.aa = Contains(AAValues(), val) ? val : AAValues()[0];
      else if (key == "post") s.post = Contains(PostValues(), val) ? val : PostValues()[0];
      else if (key == "wide_fill") s.wide_fill = ParseBool(val);
      if (end == all.size()) break;
    }
  }
  std::vector<int> choices = FpsChoices(deck);
  if (std::find(choices.begin(), choices.end(), framerate) != choices.end()) {
    s.fps_choice = framerate;
  } else if (framerate == kAutoFps) {
    s.fps_choice = 60;  // no Auto on the Deck (its panel is the display)
  } else {
    s.fps_choice = kCustomFps;
    s.custom_fps = framerate;
  }
  return s;
}

std::string FormatSettings(const Settings& s) {
  auto b = [](bool v) { return std::string(v ? "true" : "false"); };
  std::string fps = s.fps_choice == kAutoFps ? "auto" : std::to_string(SelectedFps(s, 0));
  std::string out;
  out += "game_path=" + TrimPath(s.game_path) + "\r\n";
  out += "resolution=" + s.resolution + "\r\n";
  out += "framerate=" + fps + "\r\n";
  out += "allow_tearing=" + b(s.allow_tearing) + "\r\n";
  out += "fullscreen=" + b(s.fullscreen) + "\r\n";
  out += "aa=" + s.aa + "\r\n";
  out += "post=" + s.post + "\r\n";
  out += "wide_fill=" + b(s.wide_fill) + "\r\n";
  return out;
}

int ChooseInternalScale(int display_height) {
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
  if (s.fps_choice >= 0) return s.fps_choice;
  return std::min(1000, std::max(30, s.custom_fps));
}

std::string BuildGameArgs(const Settings& s, const std::string& dist_dir, const DisplayInfo& display,
                          const std::vector<std::string>& launcher_args) {
  // The guest renders 1280x720; the scale multiplies the internal resolution.
  int width = 1280, height = 720, scale = 1;
  if (s.resolution == "1080p") {
    width = 1920; height = 1080;
  } else if (s.resolution == "1440p") {
    width = 2560; height = 1440; scale = 2;
  } else if (s.resolution == "4k") {
    width = 3840; height = 2160; scale = 3;
  } else if (s.resolution == "native") {
    if (display.width > 0 && display.height > 0) {
      width = display.width;
      height = display.height;
      scale = ChooseInternalScale(display.height);
    }
  }
  int fps = SelectedFps(s, display.hz);
  bool high_fps = fps == 0 || fps > 60;
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                " --resolution_scale=%d --draw_resolution_scale_x=%d --draw_resolution_scale_y=%d"
                " --window_width=%d --window_height=%d --fullscreen=%s",
                scale, scale, scale, width, height, s.fullscreen ? "true" : "false");
  std::string args = "--game_data_root=\"" + TrimPath(s.game_path) + "\"" + buf;
  args += " --swap_post_effect=" + s.aa + " --present_effect=" + s.post;
  args += " --framerate=" + std::to_string(fps) + " --mnk_mode=true";
  args += " --log_file=\"" + JoinPath(dist_dir, "amped3.log") + "\"";
  // Above 60 the game picks tearing on by default; only override when turned off.
  if (high_fps && !s.allow_tearing) {
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

std::string AutoDetectGameDir(const std::string& dist_dir) {
  std::string dir = JoinPath(dist_dir, "game");
  return GameFolderValid(dir) ? dir : "";
}

}  // namespace launcher
