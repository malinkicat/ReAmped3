// radio_text.cpp - The game's own radio popup (XMRadio: the XM2go's LCD with
// artist, song and genre) shows your music and the other player's tracks.
//
// The radio scene object (vtable 0x82083830, set up by sub_8253DC28) keeps
// its text controls at +152 (artist), +156 (genre) and +160 (song). Each text
// control holds its text at +0x34 and +0x54 as one reference-counted wide
// string: the characters, with a 16-byte header before them (+0 allocator,
// +4 length, +8 capacity, +12 references). The scene's update
// (sub_8253DFE0, every frame on the game thread) shows the popup when +137
// is set while it's hidden (+140 == 2) and popups are on (+136).
//
// A track is shown by giving the controls our own strings (guest memory,
// references never reaching 0, so the game never frees them) and setting
// +137. While your music plays, the game's own (muted) song changes don't
// show: its strings are swapped back for ours and its popup request dropped.

#include "radio_text.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

REXCVAR_DEFINE_BOOL(game_radio_popup, true, "Audio",
                    "Your music's and the streaming app's tracks in the game's own radio popup, while "
                    "riding only (off the mountain a track waits for the next ride); off: our banner, "
                    "anywhere");

bool Amped3_OnMountain();  // save_hooks.cpp

REX_EXTERN(__imp__sub_8253DC28);
REX_EXTERN(__imp__sub_8253DFE0);

namespace {

constexpr uint32_t kSceneVtable = 0x82083830;
constexpr uint32_t kTextVtable = 0x82054B08;
// A string's references: never 0, whatever the game releases.
constexpr uint32_t kPinned = 0x10000000;
// The LCD's widest line (the game cuts the song to 18 characters + "...").
constexpr uint32_t kChars = 21;

std::atomic<uint32_t> g_scene{0};

std::mutex g_mutex;
bool g_pending = false;
bool g_have_track = false;
std::string g_artist, g_title, g_genre;
// Our strings (artist, genre, song), allocated once.
uint32_t g_strings[3] = {};

// Your music or the other player is what's heard (rex/audio/user_music.h,
// looked up by name: the installed SDK's import library predates it).
bool UserMusicPlaying() {
  static const auto playing = [] {
    HMODULE runtime = GetModuleHandleA("rexruntime.dll");
    return runtime ? reinterpret_cast<int32_t (*)()>(GetProcAddress(runtime, "ReXUserMusicPlaying")) : nullptr;
  }();
  return playing && playing();
}

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void SetBe32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v); }

// ASCII for the LCD font: UTF-8 decoded, accented Latin letters without
// their accents, anything else '?'.
std::string Ascii(const std::string& s) {
  static const char* kLatin1 =  // U+00C0..U+00FF
      "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs" "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
  std::string out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = s[i];
    const int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
    uint32_t code = extra == 0 ? c : c & (0x3F >> extra);
    for (int k = 1; k <= extra && i + k < s.size(); ++k) code = code << 6 | (s[i + k] & 0x3F);
    i += 1 + extra;
    out += code < 0x80 ? char(code) : code >= 0xC0 && code <= 0xFF ? kLatin1[code - 0xC0] : '?';
  }
  return out;
}

// Cut to the LCD's width the way the game does.
std::string Fit(const std::string& s) {
  const std::string a = Ascii(s);
  return a.size() <= kChars ? a : a.substr(0, kChars - 3) + "...";
}

// Points a text control at our string, written with the text.
void SetText(uint8_t* base, uint32_t scene, uint32_t control, uint32_t& string, const std::string& text) {
  if (!control || Be32(base + control) != kTextVtable || Be32(base + control + 0x2C) != scene) return;
  const uint32_t current = Be32(base + control + 0x34);
  if (!current) return;
  if (!string) {
    const uint32_t header = REX_KERNEL_MEMORY()->SystemHeapAlloc(16 + (kChars + 1) * 2);
    if (!header) return;
    std::memcpy(base + header, base + current - 16, 4);  // the game's allocator
    SetBe32(base + header + 8, kChars);
    SetBe32(base + header + 12, kPinned);
    string = header + 16;
  }
  const std::string t = Fit(text);
  uint8_t* chars = base + string;
  for (size_t i = 0; i < t.size(); ++i) {
    chars[i * 2] = 0;
    chars[i * 2 + 1] = uint8_t(t[i]);
  }
  chars[t.size() * 2] = chars[t.size() * 2 + 1] = 0;
  SetBe32(base + string - 12, uint32_t(t.size()));
  SetBe32(base + control + 0x34, string);
  SetBe32(base + control + 0x54, string);
}

}  // namespace

// The radio scene set up (each ride).
extern "C" REX_FUNC(sub_8253DC28) {
  const uint32_t self = ctx.r3.u32;
  __imp__sub_8253DC28(ctx, base);
  g_scene = self;
}

// The radio scene's update: our track's text, and its popup.
extern "C" REX_FUNC(sub_8253DFE0) {
  const uint32_t self = ctx.r3.u32;
  if (self == g_scene) {
    std::lock_guard lock(g_mutex);
    if (Be32(base + self) == kSceneVtable && g_have_track && (g_pending || UserMusicPlaying())) {
      const uint32_t controls[3] = {Be32(base + self + 152), Be32(base + self + 156), Be32(base + self + 160)};
      // The game's own song change while ours plays: our text back, no popup.
      bool game_text = false;
      for (int i = 0; i < 3; ++i) {
        if (g_strings[i] && controls[i] && Be32(base + controls[i] + 0x34) != g_strings[i]) game_text = true;
      }
      if (game_text && !g_pending) base[self + 137] = 0;
      if (g_pending || game_text) {
        SetText(base, self, controls[0], g_strings[0], g_artist);
        SetText(base, self, controls[1], g_strings[1], g_genre);
        SetText(base, self, controls[2], g_strings[2], g_title);
      }
      if (g_pending) base[self + 137] = 1;
      g_pending = false;
    }
  }
  __imp__sub_8253DFE0(ctx, base);
}

namespace amped3::radio_text {

void ShowTrack(const std::string& artist, const std::string& title, const std::string& source) {
  std::lock_guard lock(g_mutex);
  g_artist = artist.empty() ? source : artist;
  g_title = title;
  g_genre = source;
  g_have_track = g_pending = true;
  REXLOG_INFO("Amped3: radio popup: {} - {} ({})", g_artist, g_title, g_genre);
}

bool Available() {
  // On the mountain the radio scene is there; it shows the track once it
  // updates again (after a wipeout or the pause menu).
  return REXCVAR_GET(game_radio_popup) && g_scene && Amped3_OnMountain();
}

}  // namespace amped3::radio_text
