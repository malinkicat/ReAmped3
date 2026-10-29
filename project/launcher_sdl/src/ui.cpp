// The launcher's screen: drawn with Dear ImGui's draw lists on SDL's renderer,
// driven by a controller, the keyboard or the mouse. One main screen (game
// folder, resolution, frame rate, toggles, image, Quit / RIDE) and a folder
// picker. Look and layout: the "Night session" theme of the earlier WPF
// launcher, whose starting layout came from Table-Tennis-Rockstar-PC
// (github.com/davidptrssvea/Table-Tennis-Rockstar-PC, MIT; see
// THIRD_PARTY_NOTICES.md).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "logic.h"
#include "system.h"

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
constexpr ImU32 kMuted = Rgb(0x7F97B3);
constexpr ImU32 kText = Rgb(0xE6EEF6);
constexpr ImU32 kPillText = Rgb(0xA9BBD0);
constexpr ImU32 kPlain = Rgb(0x1C2A40);
constexpr ImU32 kGo = Rgb(0xFF7A29);
constexpr ImU32 kGoHover = Rgb(0xFF8F4D);
constexpr ImU32 kError = Rgb(0xFF6B6B);
constexpr ImU32 kWhite = Rgb(0xFFFFFF);

// The layout is in design units (the WPF window: 560 wide), scaled to the
// window.
constexpr float kDesignW = 560.0f;
constexpr float kDesignH = 664.0f;
constexpr float kLeft = 26.0f;
constexpr float kRight = kDesignW - 26.0f;

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

// Held D-pad / stick direction with auto-repeat.
struct Repeat {
  int dir = -1;  // Act::Up..Right, -1 none
  uint64_t next = 0;
};

struct App {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  ImFont* body = nullptr;
  ImFont* display = nullptr;
  float s = 1.0f;   // design unit -> window pixels
  float ox = 0.0f;  // left edge of the content column
  float width = 0.0f;

  bool deck = false;
  bool wine = false;
  std::string dist_dir;
  bool settings_exist = false;
  Settings st;
  std::vector<int> fps_choices;
  DisplayInfo screen;
  Item focus = Item::Resolution;
  std::string status;
  bool status_error = false;
  bool pad_hints = false;

  bool picker = false;
  std::string picker_dir;
  std::vector<PickerEntry> entries;
  int sel = 0;
  int first_row = 0;
  bool picker_error = false;

  bool launching = false;
  sys::Process game;
  uint64_t launch_deadline = 0;
  uint64_t next_poll = 0;
  bool quit = false;

  std::vector<SDL_Gamepad*> pads;
  Repeat dpad;
  Repeat stick;
  std::vector<Act> acts;
  bool mouse_moved = false;
};

// ---- small helpers -------------------------------------------------------

ImVec2 P(const App& a, float x, float y) { return ImVec2(a.ox + x * a.s, y * a.s); }
Rect R(const App& a, float x0, float y0, float x1, float y1) {
  return {a.ox + x0 * a.s, y0 * a.s, a.ox + x1 * a.s, y1 * a.s};
}
ImDrawList* DL() { return ImGui::GetBackgroundDrawList(); }

// Text width in design units, measured at the drawn size.
float TextW(const App& a, ImFont* f, float size, const std::string& t) {
  return f->CalcTextSizeA(size * a.s, FLT_MAX, 0.0f, t.c_str()).x / a.s;
}

void Text(App& a, ImFont* f, float size, float x, float y, ImU32 col, const std::string& t, float wrap = 0.0f) {
  DL()->AddText(f, size * a.s, P(a, x, y), col, t.c_str(), nullptr, wrap * a.s);
}
// Semi-bold look for small labels (only Regular and ExtraBold Italic ship).
void BoldText(App& a, float size, float x, float y, ImU32 col, const std::string& t) {
  ImVec2 p = P(a, x, y);
  DL()->AddText(a.body, size * a.s, p, col, t.c_str());
  DL()->AddText(a.body, size * a.s, ImVec2(p.x + std::max(0.6f, 0.45f * a.s), p.y), col, t.c_str());
}
void Label(App& a, float y, const std::string& t) { BoldText(a, 10.5f, kLeft + 2, y, kMuted, t); }

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

bool Hover(const App& a, const Rect& r) { return !a.launching && r.Contains(ImGui::GetIO().MousePos); }
bool Clicked(const App& a, const Rect& r) { return Hover(a, r) && ImGui::IsMouseClicked(ImGuiMouseButton_Left); }

std::string FpsLabel(const App& a, int v) {
  if (v == kAutoFps) return a.screen.hz > 0 ? "Auto: match the display (" + std::to_string(a.screen.hz) + " Hz)" : "Auto: match the display";
  if (v == 0) return "Unlimited";
  if (v == kCustomFps) return "Custom";
  if (v == 30) return "30 FPS (original)";
  return std::to_string(v) + " FPS";
}
std::string ResLabel(const App& a, const std::string& v) {
  if (v == "720p") return "720p";
  if (v == "1080p") return "1080p";
  if (v == "1440p") return "1440p";
  if (v == "4k") return "4K";
  if (a.screen.width > 0) return "Match the display (" + std::to_string(a.screen.width) + "x" + std::to_string(a.screen.height) + ")";
  return "Match the display";
}
std::string AALabel(const std::string& v) {
  if (v == "fxaa") return "Anti-aliasing: FXAA";
  if (v == "fxaa_extreme") return "Anti-aliasing: FXAA Extreme";
  return "Anti-aliasing: none";
}
std::string PostLabel(const std::string& v) {
  if (v == "cas") return "Scaling: AMD CAS";
  if (v == "bilinear") return "Scaling: bilinear";
  return "Scaling: AMD FSR 1";
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

void SaveSettings(App& a) { sys::WriteFile(a.dist_dir + "settings.ini", FormatSettings(a.st)); }

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

void OpenPicker(App& a) {
  std::string start;
  std::string cur = TrimPath(a.st.game_path);
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
    a.focus = Item::GameFolder;
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
  a.st.game_path = a.picker_dir;
  a.picker = false;
  a.status.clear();
  a.focus = InitialFocus(a.settings_exist, FolderValid(a));
}

// ---- launching ---------------------------------------------------------

void Launch(App& a) {
  if (!RideEnabled(FolderValid(a))) {
    a.status = "Choose your Amped 3 folder";
    a.status_error = true;
    a.focus = Item::GameFolder;
    return;
  }
  SaveSettings(a);
  std::string exe = a.dist_dir + "amped3.exe";
  if (!sys::FileExists(exe)) {
    a.status = "amped3.exe is missing at: " + exe;
    a.status_error = true;
    return;
  }
  std::string args = BuildGameArgs(a.st, a.dist_dir, a.screen, sys::LauncherArgs());
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
  if (sys::BringGameWindowToFront(a.game) || sys::ProcessExited(a.game) || now > a.launch_deadline) a.quit = true;
}

// ---- input -------------------------------------------------------------

std::vector<Item> NavOrder(const App& a) {
  std::vector<Item> v = {Item::GameFolder, Item::Resolution, Item::FrameRate};
  if (a.st.fps_choice == kCustomFps) v.push_back(Item::CustomFps);
  v.insert(v.end(), {Item::Fullscreen, Item::Tearing, Item::AA, Item::Scaling, Item::Ride});
  return v;
}

void MoveFocus(App& a, int delta) {
  Item cur = a.focus == Item::Quit ? Item::Ride : a.focus;
  std::vector<Item> v = NavOrder(a);
  auto it = std::find(v.begin(), v.end(), cur);
  int i = it == v.end() ? 0 : int(it - v.begin());
  i = std::clamp(i + delta, 0, int(v.size()) - 1);
  a.focus = v[i];
}

void Change(App& a, int delta, bool wrap) {
  switch (a.focus) {
    case Item::Resolution: Step(ResolutionValues(), a.st.resolution, delta, wrap); break;
    case Item::FrameRate: Step(a.fps_choices, a.st.fps_choice, delta, wrap); break;
    case Item::CustomFps: a.st.custom_fps = std::clamp(std::clamp(a.st.custom_fps, 30, 1000) + delta, 30, 1000); break;
    case Item::AA: Step(AAValues(), a.st.aa, delta, wrap); break;
    case Item::Scaling: Step(PostValues(), a.st.post, delta, wrap); break;
    case Item::Quit: if (delta > 0) a.focus = Item::Ride; break;
    case Item::Ride: if (delta < 0) a.focus = Item::Quit; break;
    default: break;
  }
}

void Activate(App& a) {
  switch (a.focus) {
    case Item::GameFolder: OpenPicker(a); break;
    case Item::Fullscreen: a.st.fullscreen = !a.st.fullscreen; break;
    case Item::Tearing: a.st.allow_tearing = !a.st.allow_tearing; break;
    case Item::Quit: SaveSettings(a); a.quit = true; break;
    case Item::Ride: Launch(a); break;
    case Item::CustomFps: break;
    default: Change(a, 1, true); break;
  }
}

void HandleMain(App& a, Act act) {
  switch (act) {
    case Act::Up: MoveFocus(a, -1); break;
    case Act::Down: MoveFocus(a, 1); break;
    case Act::Left: Change(a, -1, false); break;
    case Act::Right: Change(a, 1, false); break;
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
    case Act::Cancel: a.picker = false; a.focus = Item::GameFolder; break;
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
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: a.quit = true; break;
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
      bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
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
    case SDL_EVENT_MOUSE_MOTION: a.mouse_moved = true; a.pad_hints = false; break;
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
  float h = 108.0f * a.s;
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
  float size = 38.0f;
  float x = kLeft, y = 22.0f;
  Text(a, a.display, size, x, y, kWhite, "Re");
  x += TextW(a, a.display, size, "Re");
  Text(a, a.display, size, x, y, kCyan, "Amped");
  x += TextW(a, a.display, size, "Amped");
  Text(a, a.display, size, x, y, kWhite, " 3");
  BoldText(a, 10.5f, kLeft + 2, 70.0f, kMuted, "PC EDITION");
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

void DrawHints(App& a, float y) {
  float x = kLeft + 2;
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
    Hint(a, x, y, "Enter", "Select", false);
  }
}

// A focusable item: mouse hover moves the focus to it.
void Track(App& a, Item item, const Rect& r) {
  if (a.mouse_moved && Hover(a, r)) a.focus = item;
}

// A choice row: value and arrows; click the arrows to step, elsewhere for the
// next value.
void ChoiceRow(App& a, Item item, float y, const std::string& text, float text_x = kLeft + 12) {
  Rect r = R(a, kLeft, y, kRight, y + 34);
  Rect left = R(a, kRight - 62, y, kRight - 32, y + 34);
  Rect right = R(a, kRight - 32, y, kRight, y + 34);
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
    Change(a, Clicked(a, left) ? -1 : 1, !Clicked(a, left) && !Clicked(a, right));
  }
}

void CheckRow(App& a, Item item, float y, const std::string& text, bool& value) {
  float w = 16 + 8 + TextW(a, a.body, 12.5f, text);
  Rect r = R(a, kLeft, y, kLeft + w, y + 22);
  Track(a, item, r);
  Rect box = R(a, kLeft, y + 3, kLeft + 16, y + 19);
  bool hover = Hover(a, r);
  Box(box, value ? kCyan : kField, value || hover ? kCyan : kBorder, 4, a.s);
  if (value) Tick(ImVec2(box.x0, box.y0), 16 * a.s, kNavy, a.s);
  Text(a, a.body, 12.5f, kLeft + 24, y + 3, kText, text);
  if (a.focus == item) Ring(R(a, kLeft - 2, y, kLeft + w + 4, y + 22), a.s);
  if (hover) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (Clicked(a, r)) {
    a.focus = item;
    value = !value;
  }
}

void DrawMain(App& a) {
  bool valid = FolderValid(a);

  // Game folder: the path and Browse (A opens the picker).
  Label(a, 124, "GAME FOLDER");
  {
    Rect row = R(a, kLeft, 144, kRight, 178);
    Rect field = R(a, kLeft, 144, kRight - 94, 178);
    Rect browse = R(a, kRight - 86, 144, kRight, 178);
    Track(a, Item::GameFolder, row);
    bool focused = a.focus == Item::GameFolder;
    Box(field, kField, Hover(a, field) ? kCyan : kBorder, 6, a.s);
    std::string path = TrimPath(a.st.game_path);
    float max_w = kRight - 94 - kLeft - 20;
    std::string shown = path;
    while (!shown.empty() && TextW(a, a.body, 12.0f, shown) > max_w) DropFirstChar(shown);
    if (shown != path) shown = "..." + shown;
    Text(a, a.body, 12.0f, kLeft + 10, 153, path.empty() ? kMuted : kText, path.empty() ? "No folder chosen" : shown);
    Box(browse, kPlain, Hover(a, browse) ? kCyan : kBorder, 6, a.s);
    float bw = TextW(a, a.body, 13.0f, "Browse");
    BoldText(a, 13.0f, kRight - 43 - bw / 2, 152, kPillText, "Browse");
    if (focused) Ring(row, a.s);
    if (Hover(a, row)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (Clicked(a, row)) {
      a.focus = Item::GameFolder;
      OpenPicker(a);
    }
    if (path.empty()) Text(a, a.body, 11.5f, kLeft + 2, 183, kError, "Pick your extracted Amped 3 folder (the one with default.xex).");
    else if (!valid) Text(a, a.body, 11.5f, kLeft + 2, 183, kError, "No default.xex in this folder.");
  }

  // Resolution pills.
  Label(a, 210, "RESOLUTION");
  {
    float x = kLeft;
    float y = 230;
    Rect all = R(a, kLeft, y, kRight, y + 34);
    Track(a, Item::Resolution, all);
    float right_edge = kLeft;
    for (const std::string& v : ResolutionValues()) {
      std::string t = ResLabel(a, v);
      float w = TextW(a, a.body, 12.5f, t) + 26;
      Rect r = R(a, x, y, x + w, y + 34);
      bool on = a.st.resolution == v;
      bool hover = Hover(a, r);
      Box(r, on ? kCyan : kField, on || hover ? kCyan : kBorder, 6, a.s);
      BoldText(a, 12.5f, x + 13, y + 8.5f, on ? kNavy : kPillText, t);
      if (hover) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      if (Clicked(a, r)) {
        a.focus = Item::Resolution;
        a.st.resolution = v;
      }
      right_edge = x + w;
      x += w + 6;
    }
    if (a.focus == Item::Resolution) Ring(R(a, kLeft, y, right_edge, y + 34), a.s);
  }

  // Frame rate, then the custom cap or a hint.
  Label(a, 280, "FRAME RATE");
  ChoiceRow(a, Item::FrameRate, 300, FpsLabel(a, a.st.fps_choice));
  if (a.st.fps_choice == kCustomFps) {
    ChoiceRow(a, Item::CustomFps, 342, "Custom cap: " + std::to_string(std::clamp(a.st.custom_fps, 30, 1000)) + " FPS");
  } else {
    std::string hint;
    if (a.deck && a.st.fps_choice == 40) hint = "Set the Deck's refresh rate to 40 Hz in Quick Access for smooth 40.";
    else if (a.deck && a.st.fps_choice == 90) hint = "Smoothest in light scenes; dips in busy areas.";
    else if (a.deck) hint = "Game speed is right at every setting.";
    else hint = "Game speed is right at every setting. Auto caps just under your display's refresh rate for G-Sync / FreeSync.";
    Text(a, a.body, 11.5f, kLeft + 2, 341, kMuted, hint, kRight - kLeft - 4);
  }

  // Toggles.
  CheckRow(a, Item::Fullscreen, 388, "Fullscreen", a.st.fullscreen);
  CheckRow(a, Item::Tearing, 414, "Allow tearing / G-Sync / FreeSync (above 60)", a.st.allow_tearing);

  // Image.
  Label(a, 452, "IMAGE");
  ChoiceRow(a, Item::AA, 472, AALabel(a.st.aa));
  ChoiceRow(a, Item::Scaling, 514, PostLabel(a.st.post));

  // Bottom bar: status, Quit (saves), RIDE.
  {
    float y = 570;
    float ride_w = TextW(a, a.display, 20.0f, "RIDE") + 52;
    float quit_w = TextW(a, a.body, 13.0f, "Quit") + 32;
    Rect ride = R(a, kRight - ride_w, y, kRight, y + 40);
    Rect quit = R(a, kRight - ride_w - 10 - quit_w, y, kRight - ride_w - 10, y + 40);
    Track(a, Item::Ride, ride);
    Track(a, Item::Quit, quit);
    bool enabled = RideEnabled(valid);
    Box(quit, kPlain, Hover(a, quit) ? kCyan : kBorder, 6, a.s);
    BoldText(a, 13.0f, kRight - ride_w - 10 - quit_w + 16, y + 12, kPillText, "Quit");
    ImU32 go = Hover(a, ride) && enabled ? kGoHover : kGo;
    if (!enabled) go = (go & 0x00FFFFFF) | (128u << 24);
    Box(ride, go, 0, 6, a.s);
    Text(a, a.display, 20.0f, kRight - ride_w + 26, y + 8, enabled ? kWhite : IM_COL32(255, 255, 255, 128), "RIDE");
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
    }
    Text(a, a.body, 12.0f, kLeft + 2, y + (status.size() > 40 ? 4 : 12), error ? kError : kCyan, status,
         kRight - ride_w - 20 - quit_w - kLeft - 2);
  }
}

void DrawPicker(App& a) {
  Label(a, 124, "CHOOSE YOUR AMPED 3 FOLDER");
  // The current folder, with a tick when it holds default.xex.
  bool here_valid = !a.picker_dir.empty() && GameFolderValid(a.picker_dir);
  Rect cur = R(a, kLeft, 144, kRight, 178);
  Box(cur, kField, here_valid ? kCyan : kBorder, 6, a.s);
  std::string dir = a.picker_dir.empty() ? "Drives" : a.picker_dir;
  float avail = kRight - kLeft - 24 - (here_valid ? 130 : 0);
  std::string shown = dir;
  while (shown.size() > 1 && TextW(a, a.body, 12.0f, "..." + shown) > avail) DropFirstChar(shown);
  if (shown != dir) shown = "..." + shown;
  Text(a, a.body, 12.0f, kLeft + 10, 153, kText, shown);
  if (here_valid) {
    Tick(P(a, kRight - 126, 153), 16 * a.s, kCyan, a.s);
    BoldText(a, 11.5f, kRight - 106, 154, kCyan, "default.xex found");
  }
  const char* note = a.picker_error ? "This folder can't be opened."
                     : here_valid   ? "This is it: choose this folder."
                                    : "Open the folder that holds default.xex, then choose it.";
  Text(a, a.body, 11.5f, kLeft + 2, 183, a.picker_error ? kError : here_valid ? kCyan : kMuted, note);

  // The list.
  const float row_h = 30.0f, top = 206.0f;
  const int rows = PickerRows();
  int n = int(a.entries.size());
  if (a.sel < a.first_row) a.first_row = a.sel;
  if (a.sel >= a.first_row + rows) a.first_row = a.sel - rows + 1;
  Rect list = R(a, kLeft, top, kRight, top + rows * row_h);
  float wheel = ImGui::GetIO().MouseWheel;
  if (Hover(a, list) && wheel != 0.0f) {
    a.first_row = std::clamp(a.first_row - int(wheel * 3), 0, std::max(0, n - rows));
    a.sel = std::clamp(a.sel, a.first_row, std::min(n - 1, a.first_row + rows - 1));
  }
  Box(list, kField, kBorder, 6, a.s);
  for (int i = a.first_row; i < std::min(n, a.first_row + rows); ++i) {
    float y = top + (i - a.first_row) * row_h;
    Rect r = R(a, kLeft + 3, y + 2, kRight - 3, y + row_h - 1);
    const PickerEntry& e = a.entries[i];
    bool hover = Hover(a, r);
    if (a.mouse_moved && hover) a.sel = i;
    if (i == a.sel) {
      Box(r, kBorder, kCyan, 5, a.s);
    }
    std::string label = e.up ? ".. (up one folder)" : e.label;
    Text(a, a.body, 12.5f, kLeft + 14, y + 7, e.up ? kMuted : kText, label);
    if (e.has_xex) Tick(P(a, kRight - 28, y + 6), 16 * a.s, kCyan, a.s);
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
  if (n == 0) Text(a, a.body, 12.5f, kLeft + 14, top + 7, kMuted, "(empty)");

  // Buttons: Cancel, Up, Choose this folder.
  float y = 570;
  float choose_w = TextW(a, a.body, 13.0f, "Choose this folder") + 40;
  Rect choose = R(a, kRight - choose_w, y, kRight, y + 40);
  float cancel_w = TextW(a, a.body, 13.0f, "Cancel") + 32;
  Rect cancel = R(a, kLeft, y, kLeft + cancel_w, y + 40);
  float up_w = TextW(a, a.body, 13.0f, "Up") + 32;
  Rect up = R(a, kRight - choose_w - 10 - up_w, y, kRight - choose_w - 10, y + 40);
  bool can_choose = !a.picker_dir.empty();
  Box(cancel, kPlain, Hover(a, cancel) ? kCyan : kBorder, 6, a.s);
  BoldText(a, 13.0f, kLeft + 16, y + 12, kPillText, "Cancel");
  Box(up, kPlain, Hover(a, up) ? kCyan : kBorder, 6, a.s);
  BoldText(a, 13.0f, kRight - choose_w - 10 - up_w + 16, y + 12, kPillText, "Up");
  ImU32 go = can_choose ? (Hover(a, choose) ? kGoHover : kGo) : (kGo & 0x00FFFFFF) | (128u << 24);
  Box(choose, go, 0, 6, a.s);
  BoldText(a, 13.0f, kRight - choose_w + 20, y + 12, can_choose ? kWhite : IM_COL32(255, 255, 255, 128), "Choose this folder");
  if (Hover(a, cancel) || Hover(a, up) || Hover(a, choose)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (Clicked(a, cancel)) {
    a.picker = false;
    a.focus = Item::GameFolder;
  } else if (Clicked(a, up)) {
    if (!a.picker_dir.empty()) PickerUp(a);
  } else if (Clicked(a, choose)) {
    PickerChoose(a);
  }
}

void Frame(App& a) {
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGuiIO& io = ImGui::GetIO();
  a.width = io.DisplaySize.x;
  a.s = std::min(io.DisplaySize.x / kDesignW, io.DisplaySize.y / kDesignH);
  a.ox = std::floor((io.DisplaySize.x - kDesignW * a.s) / 2);
  if (!a.launching) {
    for (Act act : a.acts) {
      if (a.picker) HandlePicker(a, act);
      else HandleMain(a, act);
      if (a.launching || a.quit) break;
    }
  }
  a.acts.clear();
  DrawHeader(a);
  if (a.picker) DrawPicker(a);
  else DrawMain(a);
  DrawHints(a, 628);
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
  // REAMPED3_LAUNCHER_DECK=1 (fullscreen) or =window (1280x800 window): the
  // Deck's layout and frame-rate list on any PC, for checking them.
  std::string deck_env = sys::EnvVar("REAMPED3_LAUNCHER_DECK");
  bool deck_window = deck_env == "window";
  if (!deck_env.empty()) a.deck = true;
  a.fps_choices = FpsChoices(a.deck);

  std::string ini;
  a.settings_exist = sys::ReadFile(a.dist_dir + "settings.ini", ini);
  a.st = ParseSettings(a.settings_exist ? &ini : nullptr, a.deck);
  if (!FolderValid(a)) {
    std::string found = AutoDetectGameDir(a.dist_dir);
    if (!found.empty()) a.st.game_path = found;
  }
  a.focus = InitialFocus(a.settings_exist, FolderValid(a));

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
    // Fixed-size window, scaled for the display, never taller than it.
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && kDesignH * cs > usable.h * 0.95f) {
      cs = usable.h * 0.95f / kDesignH;
    }
    w = int(std::lround(kDesignW * cs));
    h = int(std::lround(kDesignH * cs));
  }
  a.window = SDL_CreateWindow("ReAmped 3", w, h, flags);
  if (!a.window) return 1;
  a.renderer = SDL_CreateRenderer(a.window, nullptr);
  if (!a.renderer) return 1;
  SDL_SetRenderVSync(a.renderer, 1);
  SDL_SetWindowPosition(a.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  // Exo 2 from the fonts folder next to the launcher (else the built-in font).
  std::string fonts = a.dist_dir + "fonts\\";
  if (sys::FileExists(fonts + "Exo2-Regular.ttf")) a.body = io.Fonts->AddFontFromFileTTF((fonts + "Exo2-Regular.ttf").c_str(), 16.0f);
  if (!a.body) a.body = io.Fonts->AddFontDefaultVector();
  if (sys::FileExists(fonts + "Exo2-ExtraBoldItalic.ttf")) a.display = io.Fonts->AddFontFromFileTTF((fonts + "Exo2-ExtraBoldItalic.ttf").c_str(), 38.0f);
  if (!a.display) a.display = a.body;
  io.FontDefault = a.body;
  ImGui_ImplSDL3_InitForSDLRenderer(a.window, a.renderer);
  ImGui_ImplSDLRenderer3_Init(a.renderer);

  if (!FolderValid(a)) OpenPicker(a);
  a.pad_hints = a.deck || SDL_HasGamepad();

  Frame(a);
  SDL_ShowWindow(a.window);
  SDL_RaiseWindow(a.window);
  int fast_frames = 3;
  while (!a.quit) {
    bool busy = a.launching || a.dpad.dir >= 0 || a.stick.dir >= 0 || fast_frames > 0;
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

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  for (SDL_Gamepad* g : a.pads) SDL_CloseGamepad(g);
  SDL_DestroyRenderer(a.renderer);
  SDL_DestroyWindow(a.window);
  SDL_Quit();
  // Under Proton the launcher stays (without a window) until the game exits,
  // so `proton run` and Steam see one program from start to quit.
  if (a.launching && a.wine) sys::WaitForExit(a.game);
  sys::CloseProcess(a.game);
  return 0;
}
