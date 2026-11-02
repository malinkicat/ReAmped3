// ui_patch.cpp - The "ReAmped 3" item in the game's own Options screen
//
// At start, before the game data is mounted, writes patched copies of the
// player's UI packages into <user data>\cache\ui_overlay, which the runtime
// reads in place of the originals (--game_data_overlay); the game folder is
// never changed and no game files ship with ReAmped 3.
//
// - GuiPackages\*.rui are ZIPs of big-endian Lua 5.0 chunks (the scenes) and
//   string tables. In-ride Global Options (Game.rui GlobalOptionsMenu2): item 2,
//   Cheat Codes, always disabled while riding, becomes "ReAmped 3": enabled,
//   A runs $Select(ReAmped3) (options_hooks.cpp opens the menu).
// - Front end (Shell.rui LoadCharacter, where Start goes): Y runs
//   PushScene('ReAmped3') ($Select only works on list items), and the
//   button legend says so.
// - Base.rui StringTables\<lang>\StringTable.dat gain ReAmped3_Options.
// - resources.dir (a ZIP at the game root) indexes each package's entries, so
//   it's re-indexed for the changed packages.
// Python version and format notes: tools/rui_patch.py, tools/lua50.py.

#include "ui_patch.h"

#include "title_logo.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <array>
#include <cstring>

#include <miniz.h>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG  // just its zlib decoder
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "../thirdparty/stb/stb_image.h"

REXCVAR_DEFINE_BOOL(ui_patch, true, "UI", "Add ReAmped 3 to the game's own Options screen");
// Off since 2026-11-02 (user), as is quick save below: neither is a save state.
REXCVAR_DEFINE_BOOL(quick_save_menus, false, "Game",
                    "Back in the pause screen and the options pages saves progress (the game's own save) "
                    "and goes straight back to the ride. Off: Back does what the game does (Quit ReAmped 3 "
                    "still saves: --quick_save)");
// Quick load: it isn't a save state, only the last save
// reloaded through the main menu, which the pause menu already offers.
REXCVAR_DEFINE_BOOL(quick_load, false, "Game",
                    "LB twice on the pause screen loads the last save (through the game's own path: "
                    "leave the mountain without saving, load, Hit the Slopes). Off: not offered");
REXCVAR_DEFINE_BOOL(title_logo, false, "UI",
                    "The ReAmped 3 logo on the title screen and the start-up loading screen (drawn from the game's "
                    "own logo); off: Amped 3's");
REXCVAR_DEFINE_BOOL(sharp_ui, true, "UI",
                    "Use the sharp UI pack (textures/replace next to the game: the menus' and HUD's art "
                    "enlarged 4x from the originals) when it's there");
// UI sizes, in percent: everything (ui_size: 100 = the Xbox 360's
// proportions at any resolution) times each kind's own. Each scene grows from
// its own anchors (a help tip from its corner, a menu from the centre).
REXCVAR_DEFINE_INT32(ui_size, 100, "UI", "Size of the menus and HUD, in percent (100: as on the Xbox 360)").range(50, 100);
REXCVAR_DEFINE_INT32(ui_size_menus, 100, "UI", "Size of the menus, in percent of ui_size").range(50, 100);
REXCVAR_DEFINE_INT32(ui_size_hud, 100, "UI", "Size of the riding HUD (score, meters, timers), in percent of ui_size").range(50, 150);
REXCVAR_DEFINE_INT32(ui_size_tips, 100, "UI", "Size of the help tips, in percent of ui_size").range(50, 150);
REXCVAR_DEFINE_INT32(ui_size_music, 100, "UI", "Size of the radio / music popup, in percent of ui_size").range(50, 150);
REXCVAR_DEFINE_BOOL(ui_size_debug_no_insert, false, "Debug", "UI sizes: never insert a LOADK (those nodes keep their size)");
REXCVAR_DEFINE_STRING(ui_size_debug_scenes, "", "Debug",
                      "UI sizes: only scenes whose names contain one of these (',' between them)");
REXCVAR_DEFINE_INT32(ui_size_legends, 100, "UI", "Size of the menus' button legends, in percent of the menus' size").range(50, 150);
REXCVAR_DECLARE(std::string, game_data_root);
REXCVAR_DECLARE(std::string, user_data_root);

namespace amped3::ui_patch {
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
constexpr int kVersion = 29;  // bump when the patches change (29: quick save / load off)

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }
void PutBe32(Bytes& b, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) b.push_back(uint8_t(v >> s));
}
void SetBe32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (24 - 8 * i));
}
void SetLe32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}

uint32_t Crc32(const Bytes& data) {
  static uint32_t table[256];
  if (!table[1]) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
  }
  uint32_t crc = ~0u;
  for (uint8_t b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

Bytes StoredDeflate(const Bytes& raw);

// A deflate stream (miniz): patched entries stay about their original size
// (stored, a package with many changed scenes grows past what the game
// copes with: an uncompressed ~1 MB package crashed the boot).
Bytes Deflate(const Bytes& raw) {
  size_t size = 0;
  void* data = tdefl_compress_mem_to_heap(raw.data(), raw.size(), &size, TDEFL_DEFAULT_MAX_PROBES);
  if (!data) return StoredDeflate(raw);
  Bytes out(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
  mz_free(data);
  return out;
}

// A deflate stream of stored blocks (any inflater reads it).
Bytes StoredDeflate(const Bytes& raw) {
  Bytes out;
  size_t i = 0;
  do {
    const size_t n = std::min<size_t>(65535, raw.size() - i);
    out.push_back(i + n >= raw.size() ? 1 : 0);
    out.push_back(uint8_t(n));
    out.push_back(uint8_t(n >> 8));
    out.push_back(uint8_t(~n));
    out.push_back(uint8_t(~n >> 8));
    out.insert(out.end(), raw.begin() + i, raw.begin() + i + n);
    i += n;
  } while (i < raw.size());
  return out;
}

using Patch = std::function<bool(Bytes&)>;

// CRC and sizes into a local header and its central directory record.
void SetSizes(Bytes& lhead, Bytes& fixed, const Bytes& raw, const Bytes& body) {
  uint8_t fields[12];
  SetLe32(fields, Crc32(raw));
  SetLe32(fields + 4, uint32_t(body.size()));
  SetLe32(fields + 8, uint32_t(raw.size()));
  std::memcpy(&lhead[14], fields, 12);
  std::memcpy(&fixed[16], fields, 12);
}
// New entries: name -> (template entry, patch of the template's contents).
using Adds = std::map<std::string, std::pair<std::string, Patch>>;

// The ZIP with some entries' contents replaced (stored deflate), every other
// byte as it was, and `adds` appended with their templates' headers. Fails if
// a patched or template entry is missing or a patch fails.
std::optional<Bytes> RebuildZip(const Bytes& data, const std::map<std::string, Patch>& patches,
                                const Adds& adds = {}) {
  if (data.size() < 22) return std::nullopt;
  size_t eocd = data.size() - 22;
  while (eocd > 0 && Le32(&data[eocd]) != 0x06054B50) --eocd;
  if (Le32(&data[eocd]) != 0x06054B50) return std::nullopt;
  const uint16_t count = Le16(&data[eocd + 10]);
  size_t pos = Le32(&data[eocd + 16]);
  Bytes out, central;
  size_t patched = 0;
  struct Template {
    std::string name;
    const Patch* patch;
    Bytes lhead, fixed, raw;
  };
  std::vector<Template> templates;
  for (uint16_t e = 0; e < count; ++e) {
    if (pos + 46 > data.size() || Le32(&data[pos]) != 0x02014B50) return std::nullopt;
    Bytes fixed(data.begin() + pos, data.begin() + pos + 46);
    const uint16_t nlen = Le16(&fixed[28]), xlen = Le16(&fixed[30]), clen = Le16(&fixed[32]);
    const std::string name(reinterpret_cast<const char*>(&data[pos + 46]), nlen);
    const size_t local = Le32(&fixed[42]);
    const uint32_t csize = Le32(&fixed[20]), usize = Le32(&fixed[24]);
    if (local + 30 > data.size()) return std::nullopt;
    const size_t head = 30 + Le16(&data[local + 26]) + Le16(&data[local + 28]);
    if (local + head + csize > data.size()) return std::nullopt;
    Bytes lhead(data.begin() + local, data.begin() + local + head);
    Bytes body(data.begin() + local + head, data.begin() + local + head + csize);
    auto inflate = [&](Bytes& raw) {
      raw.resize(usize);
      return stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(raw.data()), int(usize),
                                              reinterpret_cast<const char*>(body.data()), int(csize)) == int(usize);
    };
    for (const auto& [add_name, add] : adds) {
      if (add.first != name) continue;
      Bytes raw;
      if (!inflate(raw)) return std::nullopt;
      templates.push_back({add_name, &add.second, lhead, fixed, std::move(raw)});
    }
    if (auto it = patches.find(name); it != patches.end()) {
      Bytes raw;
      if (!inflate(raw) || !it->second(raw)) {
        REXLOG_WARN("Amped3: UI patch: {} failed", name);
        return std::nullopt;
      }
      body = Deflate(raw);
      SetSizes(lhead, fixed, raw, body);
      ++patched;
    }
    SetLe32(&fixed[42], uint32_t(out.size()));
    out.insert(out.end(), lhead.begin(), lhead.end());
    out.insert(out.end(), body.begin(), body.end());
    central.insert(central.end(), fixed.begin(), fixed.end());
    central.insert(central.end(), data.begin() + pos + 46, data.begin() + pos + 46 + nlen + xlen + clen);
    pos += 46 + nlen + xlen + clen;
  }
  if (patched != patches.size() || templates.size() != adds.size()) return std::nullopt;
  for (auto& t : templates) {
    if (!(*t.patch)(t.raw)) {
      REXLOG_WARN("Amped3: UI patch: {} failed", t.name);
      return std::nullopt;
    }
    const Bytes body = Deflate(t.raw);
    Bytes lhead(t.lhead.begin(), t.lhead.begin() + 26);
    lhead.push_back(uint8_t(t.name.size()));
    lhead.push_back(uint8_t(t.name.size() >> 8));
    lhead.push_back(0);
    lhead.push_back(0);
    lhead.insert(lhead.end(), t.name.begin(), t.name.end());
    Bytes fixed = t.fixed;
    SetSizes(lhead, fixed, t.raw, body);
    fixed[28] = uint8_t(t.name.size());
    fixed[29] = uint8_t(t.name.size() >> 8);
    std::fill(fixed.begin() + 30, fixed.begin() + 34, 0);
    SetLe32(&fixed[42], uint32_t(out.size()));
    out.insert(out.end(), lhead.begin(), lhead.end());
    out.insert(out.end(), body.begin(), body.end());
    central.insert(central.end(), fixed.begin(), fixed.end());
    central.insert(central.end(), t.name.begin(), t.name.end());
  }
  Bytes end(data.begin() + eocd, data.end());
  const uint16_t total = uint16_t(count + adds.size());
  end[8] = end[10] = uint8_t(total);
  end[9] = end[11] = uint8_t(total >> 8);
  SetLe32(&end[12], uint32_t(central.size()));
  SetLe32(&end[16], uint32_t(out.size()));
  out.insert(out.end(), central.begin(), central.end());
  out.insert(out.end(), end.begin(), end.end());
  return out;
}

// The names of a ZIP's entries (central directory order).
std::vector<std::string> EntryNames(const Bytes& data) {
  std::vector<std::string> names;
  if (data.size() < 22) return names;
  size_t eocd = data.size() - 22;
  while (eocd > 0 && Le32(&data[eocd]) != 0x06054B50) --eocd;
  size_t pos = Le32(&data[eocd + 16]);
  for (uint16_t e = Le16(&data[eocd + 10]); e > 0 && pos + 46 <= data.size(); --e) {
    const uint16_t nlen = Le16(&data[pos + 28]), xlen = Le16(&data[pos + 30]), clen = Le16(&data[pos + 32]);
    if (pos + 46 + nlen > data.size()) break;
    names.emplace_back(reinterpret_cast<const char*>(&data[pos + 46]), nlen);
    pos += 46 + nlen + xlen + clen;
  }
  return names;
}

// One entry's contents (nullopt if missing).
std::optional<Bytes> ReadEntry(const Bytes& data, const std::string& want) {
  if (data.size() < 22) return std::nullopt;
  size_t eocd = data.size() - 22;
  while (eocd > 0 && Le32(&data[eocd]) != 0x06054B50) --eocd;
  size_t pos = Le32(&data[eocd + 16]);
  for (uint16_t e = Le16(&data[eocd + 10]); e > 0 && pos + 46 <= data.size(); --e) {
    const uint16_t nlen = Le16(&data[pos + 28]), xlen = Le16(&data[pos + 30]), clen = Le16(&data[pos + 32]);
    if (std::string(reinterpret_cast<const char*>(&data[pos + 46]), nlen) == want) {
      const size_t local = Le32(&data[pos + 42]);
      const uint32_t csize = Le32(&data[pos + 20]), usize = Le32(&data[pos + 24]);
      if (local + 30 > data.size()) return std::nullopt;
      const size_t head = 30 + Le16(&data[local + 26]) + Le16(&data[local + 28]);
      if (local + head + csize > data.size()) return std::nullopt;
      Bytes raw(usize);
      if (stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(raw.data()), int(usize),
                                           reinterpret_cast<const char*>(&data[local + head]), int(csize)) != int(usize)) {
        return std::nullopt;
      }
      return raw;
    }
    pos += 46 + nlen + xlen + clen;
  }
  return std::nullopt;
}

// --- Lua 5.0 chunks (big-endian; int, size_t, Instruction 4 bytes; number 8)

struct LuaFunction {
  std::optional<std::string> source;
  int32_t line = 0;
  uint8_t info[4] = {};  // nups, numparams, is_vararg, maxstacksize
  std::vector<int32_t> lineinfo;
  struct LocVar {
    std::optional<std::string> name;
    int32_t start, end;
  };
  std::vector<LocVar> locvars;
  std::vector<std::optional<std::string>> upvalues;
  // u16string: the game's UTF-16 string constants (type 9).
  std::vector<std::variant<std::monostate, double, std::string, std::u16string>> consts;
  std::vector<LuaFunction> protos;
  std::vector<uint32_t> code;
};

constexpr uint8_t kLuaHeader[] = {0x1B, 'L', 'u', 'a', 0x50, 0, 4, 4, 4, 6, 8, 9, 9, 8};
constexpr uint32_t kMaxStack = 250;  // RK operands >= this are constants
enum : uint32_t { kLoadBool = 2, kSetTable = 9 };

struct LuaReader {
  const Bytes& d;
  size_t o;
  bool ok = true;
  bool Need(size_t n) { return ok = ok && o + n <= d.size(); }
  int32_t Int() { if (!Need(4)) return 0; o += 4; return int32_t(Be32(&d[o - 4])); }
  uint8_t Byte() { if (!Need(1)) return 0; return d[o++]; }
  std::optional<std::string> String() {
    const uint32_t n = uint32_t(Int());
    if (!n || !Need(n)) return std::nullopt;
    o += n;
    return std::string(reinterpret_cast<const char*>(&d[o - n]), n - 1);
  }
  int32_t Count() {
    const int32_t n = Int();
    if (n < 0 || size_t(n) > d.size()) ok = false;
    return ok ? n : 0;
  }
  LuaFunction Function() {
    LuaFunction f;
    f.source = String();
    f.line = Int();
    for (auto& b : f.info) b = Byte();
    for (int32_t i = Count(); ok && i > 0; --i) f.lineinfo.push_back(Int());
    for (int32_t i = Count(); ok && i > 0; --i) {
      auto name = String();
      const int32_t a = Int(), b = Int();
      f.locvars.push_back({name, a, b});
    }
    for (int32_t i = Count(); ok && i > 0; --i) f.upvalues.push_back(String());
    for (int32_t i = Count(); ok && i > 0; --i) {
      const uint8_t t = Byte();
      if (t == 3 && Need(8)) {
        uint64_t bits = uint64_t(Be32(&d[o])) << 32 | Be32(&d[o + 4]);
        o += 8;
        double v;
        std::memcpy(&v, &bits, 8);
        f.consts.emplace_back(v);
      } else if (t == 4) {
        f.consts.emplace_back(String().value_or(""));
      } else if (t == 0) {
        f.consts.emplace_back(std::monostate{});
      } else if (t == 9) {  // count of code units (with the NUL), big-endian
        const int32_t n = Count();
        if (!n || !Need(size_t(n) * 2)) {
          ok = false;
          break;
        }
        std::u16string w;
        for (int32_t c = 0; c + 1 < n; ++c) w.push_back(char16_t(d[o + 2 * c] << 8 | d[o + 2 * c + 1]));
        o += size_t(n) * 2;
        f.consts.emplace_back(std::move(w));
      } else {
        ok = false;
      }
    }
    for (int32_t i = Count(); ok && i > 0; --i) f.protos.push_back(Function());
    for (int32_t i = Count(); ok && i > 0; --i) f.code.push_back(uint32_t(Int()));
    return f;
  }
};

void LuaWrite(Bytes& out, const LuaFunction& f) {
  auto str = [&](const std::optional<std::string>& s) {
    if (!s) return PutBe32(out, 0);
    PutBe32(out, uint32_t(s->size() + 1));
    out.insert(out.end(), s->begin(), s->end());
    out.push_back(0);
  };
  str(f.source);
  PutBe32(out, uint32_t(f.line));
  out.insert(out.end(), f.info, f.info + 4);
  PutBe32(out, uint32_t(f.lineinfo.size()));
  for (int32_t v : f.lineinfo) PutBe32(out, uint32_t(v));
  PutBe32(out, uint32_t(f.locvars.size()));
  for (const auto& l : f.locvars) {
    str(l.name);
    PutBe32(out, uint32_t(l.start));
    PutBe32(out, uint32_t(l.end));
  }
  PutBe32(out, uint32_t(f.upvalues.size()));
  for (const auto& u : f.upvalues) str(u);
  PutBe32(out, uint32_t(f.consts.size()));
  for (const auto& k : f.consts) {
    if (const double* v = std::get_if<double>(&k)) {
      out.push_back(3);
      uint64_t bits;
      std::memcpy(&bits, v, 8);
      PutBe32(out, uint32_t(bits >> 32));
      PutBe32(out, uint32_t(bits));
    } else if (const std::string* s = std::get_if<std::string>(&k)) {
      out.push_back(4);
      str(*s);
    } else if (const std::u16string* w = std::get_if<std::u16string>(&k)) {
      out.push_back(9);
      PutBe32(out, uint32_t(w->size() + 1));
      for (char16_t c : *w) out.insert(out.end(), {uint8_t(c >> 8), uint8_t(c)});
      out.insert(out.end(), {0, 0});
    } else {
      out.push_back(0);
    }
  }
  PutBe32(out, uint32_t(f.protos.size()));
  for (const auto& p : f.protos) LuaWrite(out, p);
  PutBe32(out, uint32_t(f.code.size()));
  for (uint32_t c : f.code) PutBe32(out, c);
}

// Edits a chunk's main function; false if it isn't a chunk we can read.
bool EditLua(Bytes& data, const std::function<bool(LuaFunction&)>& edit) {
  constexpr size_t kHead = sizeof(kLuaHeader) + 8;  // + the test number
  if (data.size() < kHead || std::memcmp(data.data(), kLuaHeader, sizeof(kLuaHeader))) return false;
  LuaReader r{data, kHead};
  LuaFunction f = r.Function();
  if (!r.ok || r.o != data.size() || !edit(f)) return false;
  Bytes out(data.begin(), data.begin() + kHead);
  LuaWrite(out, f);
  data = std::move(out);
  return true;
}

uint32_t Op(uint32_t i) { return i & 0x3F; }
uint32_t A(uint32_t i) { return i >> 24; }
uint32_t B(uint32_t i) { return (i >> 15) & 0x1FF; }
uint32_t C(uint32_t i) { return (i >> 6) & 0x1FF; }
uint32_t Encode(uint32_t op, uint32_t a, uint32_t b, uint32_t c) { return op | c << 6 | b << 15 | a << 24; }

bool IsConst(const LuaFunction& f, uint32_t rk, const char* s) {
  if (rk < kMaxStack || rk - kMaxStack >= f.consts.size()) return false;
  const auto* v = std::get_if<std::string>(&f.consts[rk - kMaxStack]);
  return v && *v == s;
}

// RK operand for a string constant (added if missing). An RK operand has 9
// bits: constants past 511 - kMaxStack can't be one (nullopt; a LOADK can).
std::optional<uint32_t> Const(LuaFunction& f, const std::string& s) {
  for (size_t i = 0; i < f.consts.size(); ++i) {
    if (const auto* v = std::get_if<std::string>(&f.consts[i]); v && *v == s) {
      if (i + kMaxStack > 511) break;  // (an index past it encoded as RK spills into B)
      return uint32_t(i + kMaxStack);
    }
  }
  if (f.consts.size() >= 511 - kMaxStack) return std::nullopt;
  f.consts.emplace_back(s);
  return uint32_t(f.consts.size() - 1 + kMaxStack);
}

// sceneData: the node whose TextID is `from` shows `to`.
bool SwapTextId(LuaFunction& f, const char* from, const char* to) {
  for (uint32_t& ins : f.code) {
    if (Op(ins) == kSetTable && IsConst(f, B(ins), "TextID") && IsConst(f, C(ins), from)) {
      const auto k = Const(f, to);
      if (!k) return false;
      ins = Encode(kSetTable, A(ins), B(ins), *k);
      return true;
    }
  }
  return false;
}

// scene: ButtonList item `index` (1-based, in its Items list) is enabled and
// A runs $Select(ReAmped3).
bool EnableReAmpedItem(LuaFunction& f, size_t index, const char* command = "$Select(ReAmped3)") {
  size_t seen = 0;
  for (size_t pc = 1; pc < f.code.size(); ++pc) {
    const uint32_t ins = f.code[pc];
    if (Op(ins) != kSetTable || !IsConst(f, B(ins), "Enabled") || ++seen != index) continue;
    const uint32_t load = f.code[pc - 1];
    if (Op(load) != kLoadBool || A(load) != C(ins) || f.lineinfo.size() != f.code.size()) return false;
    const auto key = Const(f, "AButton"), value = Const(f, command);
    if (!key || !value) return false;
    f.code[pc - 1] = Encode(kLoadBool, A(load), 1, 0);
    f.code.insert(f.code.begin() + pc + 1, Encode(kSetTable, A(ins), *key, *value));
    f.lineinfo.insert(f.lineinfo.begin() + pc + 1, f.lineinfo[pc]);
    return true;
  }
  return false;
}

std::vector<size_t> SetTablePcs(const LuaFunction& f, const char* key, const char* value = nullptr) {
  std::vector<size_t> out;
  for (size_t pc = 0; pc < f.code.size(); ++pc) {
    const uint32_t ins = f.code[pc];
    if (Op(ins) == kSetTable && IsConst(f, B(ins), key) && (!value || IsConst(f, C(ins), value))) out.push_back(pc);
  }
  return out;
}

bool SetValue(LuaFunction& f, size_t pc, const std::string& value) {
  const auto k = Const(f, value);
  if (!k) return false;
  f.code[pc] = Encode(Op(f.code[pc]), A(f.code[pc]), B(f.code[pc]), *k);
  return true;
}

void Insert(LuaFunction& f, size_t pc, uint32_t ins) {
  f.code.insert(f.code.begin() + pc, ins);
  f.lineinfo.insert(f.lineinfo.begin() + pc, f.lineinfo[std::min(pc, f.lineinfo.size() - 1)]);
}

enum : uint32_t { kNewTable = 10 };

// --- UI sizes: scenes scaled from their anchors ---------------------------
//
// A sceneData is a node tree; Anchor nodes (Anchor_BottomSafeLeftSafe,
// Anchor_Center, ...) pin what hangs under them to a screen corner, edge or
// the centre. Scaling an anchor node grows its contents from that point. A
// node's PositioningInfo is big-endian floats: position x y z, scale x y z,
// .... Big scenes (over ~250 constants) set names and values through
// registers (LOADK, then SETTABLE t[k] = r); small ones with constants.

enum : uint32_t { kLoadK = 1 };
uint32_t Bx(uint32_t i) { return (i >> 6) & 0x3FFFF; }

// The string a SETTABLE at pc stores (a constant operand, or the register a
// LOADK filled since the table was made), and where that constant is used.
struct StoredString {
  std::string value;
  size_t at;      // the SETTABLE, or the LOADK that loaded the register
  bool via_loadk;
};
std::optional<StoredString> StoredStringAt(const LuaFunction& f, size_t pc) {
  const uint32_t ins = f.code[pc], rk = C(ins);
  if (rk >= kMaxStack) {
    if (rk - kMaxStack >= f.consts.size()) return std::nullopt;
    const auto* v = std::get_if<std::string>(&f.consts[rk - kMaxStack]);
    if (!v) return std::nullopt;
    return StoredString{*v, pc, false};
  }
  for (size_t p = pc; p-- > 0;) {
    const uint32_t i = f.code[p];
    if (A(i) != rk) continue;
    if (Op(i) != kLoadK) return std::nullopt;  // the register was set some other way
    if (Bx(i) >= f.consts.size()) return std::nullopt;
    const auto* v = std::get_if<std::string>(&f.consts[Bx(i)]);
    if (!v) return std::nullopt;
    return StoredString{*v, p, true};
  }
  return std::nullopt;
}

// May insert an instruction at where.at (later pcs move one on).
bool StoreString(LuaFunction& f, const StoredString& where, const std::string& value) {
  if (!where.via_loadk && SetValue(f, where.at, value)) return true;
  size_t k = f.consts.size();
  for (size_t i = 0; i < f.consts.size(); ++i) {
    if (const auto* v = std::get_if<std::string>(&f.consts[i]); v && *v == value) k = i;
  }
  if (k == f.consts.size()) f.consts.emplace_back(value);
  const uint32_t ins = f.code[where.at];
  if (where.via_loadk) {
    f.code[where.at] = Op(ins) | uint32_t(k) << 6 | A(ins) << 24;
    return true;
  }
  // Too many constants for a constant operand: loaded into the register
  // above the node's table first. A node's fields are set in name order, so
  // by PositioningInfo its Children list (built there) is already stored;
  // the game's own code reuses that register the same way.
  const uint32_t scratch = A(ins) + 1;
  if (REXCVAR_GET(ui_size_debug_no_insert) || scratch >= f.info[3] || f.lineinfo.size() != f.code.size()) return false;
  Insert(f, where.at, kLoadK | uint32_t(k) << 6 | scratch << 24);
  f.code[where.at + 1] = Encode(kSetTable, A(ins), B(ins), scratch);
  return true;
}

// Each node (table register) whose `key` is set to `value`: the SETTABLE's pc.
std::vector<size_t> NodesWhere(const LuaFunction& f, const char* key, const std::function<bool(const std::string&)>& value) {
  std::vector<size_t> out;
  for (size_t pc = 0; pc < f.code.size(); ++pc) {
    const uint32_t ins = f.code[pc];
    if (Op(ins) != kSetTable || !IsConst(f, B(ins), key)) continue;
    const auto v = StoredStringAt(f, pc);
    if (v && value(v->value)) out.push_back(pc);
  }
  return out;
}

// The node set at `pc` (any of its fields): its PositioningInfo's scale x and
// y multiplied by `scale`.
bool ScaleNode(LuaFunction& f, size_t pc, double scale) {
  const uint32_t table = A(f.code[pc]);
  auto scale_info = [&](size_t p) {
    const auto info = StoredStringAt(f, p);
    if (!info || info->value.size() < 24) return false;
    std::string scaled = info->value;
    for (int i = 3; i < 5; ++i) {
      uint32_t bits = Be32(reinterpret_cast<const uint8_t*>(scaled.data()) + i * 4);
      float v;
      std::memcpy(&v, &bits, 4);
      v = float(v * scale);
      std::memcpy(&bits, &v, 4);
      for (int b = 0; b < 4; ++b) scaled[size_t(i * 4 + b)] = char(bits >> (24 - 8 * b));
    }
    return StoreString(f, *info, scaled);
  };
  // PositioningInfo is set before or after the field found, between the
  // table's NEWTABLE and the next one into the same register.
  for (size_t p = pc; p-- > 0;) {
    const uint32_t ins = f.code[p];
    if (Op(ins) == kNewTable && A(ins) == table) break;
    if (Op(ins) == kSetTable && A(ins) == table && IsConst(f, B(ins), "PositioningInfo")) return scale_info(p);
  }
  for (size_t p = pc + 1; p < f.code.size(); ++p) {
    const uint32_t ins = f.code[p];
    if (Op(ins) == kNewTable && A(ins) == table) break;
    if (Op(ins) == kSetTable && A(ins) == table && IsConst(f, B(ins), "PositioningInfo")) return scale_info(p);
  }
  return false;
}

// The scene grown (or shrunk) from its anchors by `scale`, its button legend
// (the ButtonLegend node, inside the scene's anchor) by `legend` on top. The
// nodes are done last to first (a change can insert an instruction). False
// if a node couldn't be changed (the others are).
bool ScaleScene(LuaFunction& f, double scale, double legend, int* failed = nullptr) {
  std::vector<std::pair<size_t, double>> nodes;
  if (std::abs(scale - 1.0) > 1e-4) {
    for (size_t pc : NodesWhere(f, "Name", [](const std::string& v) { return v.rfind("Anchor_", 0) == 0; })) {
      nodes.emplace_back(pc, scale);
    }
  }
  if (std::abs(legend - 1.0) > 1e-4) {
    for (size_t pc : NodesWhere(f, "Name", [](const std::string& v) { return v == "ButtonLegend"; })) {
      nodes.emplace_back(pc, legend);
    }
  }
  std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  int bad = 0;
  for (const auto& [pc, s] : nodes) bad += !ScaleNode(f, pc, s);
  if (failed) *failed = bad;
  return true;
}

enum class UiClass { kNone, kMenus, kHud, kTips, kMusic };

// What a scene is, from its name (GuiPackages/*.rui Scenes/<name>.sceneData).
UiClass ClassOf(const std::string& scene) {
  if (scene == "HUD_HelpText") return UiClass::kTips;
  if (scene == "XMRadio") return UiClass::kMusic;
  // Full-screen layers keep covering the screen; templates are scaled by the
  // scenes that use them.
  if (scene == "HUD_Fade" || scene == "BackgroundPlane" || scene.rfind("TMP_", 0) == 0) return UiClass::kNone;
  static const char* const kHud[] = {
      "AwesMeter", "CamcorderOverlay", "CameraHud", "CountDownTimer", "GasGauge", "HudFlyinTextMenu",
      "ImpressTimerHud", "IndieBalanceMeter", "KudosChallengeHud", "NewScoreHUD", "PreCameraReticle", "ProHud",
      "RunLineName", "SleddingHud", "SnowBallSplat", "TargetRetical", "TimerAll", "VerticalBalanceMeter",
      "Challenge_Impress_Analog_Timer", "Challenge_Impress_Timer", "Challenge_Impress_TricksLiked"};
  if (scene.rfind("HUD_", 0) == 0) return UiClass::kHud;
  for (const char* name : kHud) {
    if (scene == name) return UiClass::kHud;
  }
  return UiClass::kMenus;
}

int ClassSize(UiClass c) {
  switch (c) {
    case UiClass::kMenus: return REXCVAR_GET(ui_size_menus);
    case UiClass::kHud: return REXCVAR_GET(ui_size_hud);
    case UiClass::kTips: return REXCVAR_GET(ui_size_tips);
    case UiClass::kMusic: return REXCVAR_GET(ui_size_music);
    default: return 100;
  }
}

// Every scene of a package scaled by its class (and the master size), added
// to `patches` (after a patch already there for the same scene).
void AddSizePatches(const Bytes& zip, std::map<std::string, Patch>& patches) {
  for (const std::string& name : EntryNames(zip)) {
    constexpr std::string_view kPrefix = "Scenes/", kSuffix = ".sceneData";
    if (name.rfind(kPrefix, 0) != 0 || name.size() <= kPrefix.size() + kSuffix.size() ||
        name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
      continue;
    }
    const std::string scene = name.substr(kPrefix.size(), name.size() - kPrefix.size() - kSuffix.size());
    const UiClass c = ClassOf(scene);
    if (c == UiClass::kNone) continue;
    if (const std::string only = REXCVAR_GET(ui_size_debug_scenes); !only.empty()) {
      bool listed = false;
      for (size_t pos = 0; pos <= only.size();) {
        size_t next = only.find(',', pos);
        if (next == std::string::npos) next = only.size();
        if (next > pos && scene.find(only.substr(pos, next - pos)) != std::string::npos) listed = true;
        pos = next + 1;
      }
      if (!listed) continue;
    }
    const double scale = REXCVAR_GET(ui_size) / 100.0 * ClassSize(c) / 100.0;
    const double legend = c == UiClass::kMenus ? REXCVAR_GET(ui_size_legends) / 100.0 : 1.0;
    if (std::abs(scale - 1.0) < 1e-4 && std::abs(legend - 1.0) < 1e-4) continue;
    Patch before;
    if (auto it = patches.find(name); it != patches.end()) before = it->second;
    patches[name] = [before, scale, legend, name](Bytes& d) {
      if (before && !before(d)) return false;
      int failed = 0;
      if (!EditLua(d, [&](LuaFunction& f) { return ScaleScene(f, scale, legend, &failed); })) {
        REXLOG_WARN("Amped3: UI sizes: {} kept its size", name);
      } else if (failed) {
        REXLOG_WARN("Amped3: UI sizes: {}: {} node(s) kept their size", name, failed);
      }
      return true;
    };
  }
}

// The game-styled ReAmped 3 screens: GlobalOptionsMenu2 cloned as new scenes
// (the game builds an unknown scene name with its generic scene class). Four
// rows: the top two are labels (the first shows its value in its label), the
// bottom two have the game's arrows and value lists. Row actions are
// $Select(<name>): another of our scenes, or ReAmped3_<command> (letters and _
// only), run by options_hooks.cpp, which also rewrites the row and value texts
// in memory. Text ids: <prefix>_Title, _Row1-4, _Val3, _Val4.
struct NativePage {
  const char* scene;
  const char* prefix;
  const char* rows[16][3];  // A, Left, Right
  // More than 4: a scrolling list (an ItemMapping on the list, as the game's
  // On / Off value lists have) of <prefix>_Row1..n, values in the row texts
  // (the value fields are hidden).
  int items = 4;
  // false: no value fields (also for a 4-row page).
  bool values = true;
  // Rows after these are blank and disabled (a page of 3).
  int enabled_rows = 4;
};
constexpr int kAchievementRows = 15;
constexpr int kBackupRows = 10;
// The tree (reviewed 2026-10-31): a hub of links, each
// page at most 4 rows; lists only where the content is a list.
// Pause > Options opens it (the user's layout, 2026-11-01):
//   Options: Audio > / Display > / Extras > / Quit ReAmped 3
//   Audio: Game audio > (the game's own) / Source / Music volume / Next track
//   Display: UI size / Effects > / Frame rate <60> / Anti-aliasing <SMAA>
//   Effects: Distance fog / Falling snow / Board spray / HUD
//   Extras: the game's own Global Options scene, retitled: Achievements > /
//     Save backups > / Hints <On> / Media Callouts <On> (its own toggles)
constexpr NativePage kNativePages[] = {
    {"ReAmped3",
     "ReAmped3",
     {{"ReAmped3Mus"}, {"ReAmped3Disp"}, {"GlobalOptionsMenu2"}, {"ReAmped3_quit"}},
     4,
     false},
    {"ReAmped3Disp",
     "ReAmped3D",
     {{"ReAmped3_uisize_next", "ReAmped3_uisize_prev", "ReAmped3_uisize_next"},
      {"ReAmped3Fx"},
      {"ReAmped3_fps_next", "ReAmped3_fps_prev", "ReAmped3_fps_next"},
      {"ReAmped3_aa_next", "ReAmped3_aa_prev", "ReAmped3_aa_next"}}},
    {"ReAmped3Fx",
     "ReAmped3E",
     {{"ReAmped3_fog_next", "ReAmped3_fog_prev", "ReAmped3_fog_next"},
      {"ReAmped3_snow_next", "ReAmped3_snow_prev", "ReAmped3_snow_next"},
      {"ReAmped3_spray_next", "ReAmped3_spray_prev", "ReAmped3_spray_next"},
      {"ReAmped3_hud_next", "ReAmped3_hud_next", "ReAmped3_hud_next"}},
     4,
     false},
    {"ReAmped3Mus",
     "ReAmped3U",
     {{"AudioAndMusicOptions2"},
      {"ReAmped3_music_next", "ReAmped3_music_prev", "ReAmped3_music_next"},
      {"ReAmped3_volume_next", "ReAmped3_volume_prev", "ReAmped3_volume_next"},
      {"ReAmped3_skip_next", "ReAmped3_skip_prev", "ReAmped3_skip_next"}},
     4,
     false},
    // Achievements: each row a name and its progress (options_hooks.cpp).
    {"ReAmped3Ach",
     "ReAmped3A",
     {{"ReAmped3_ach_one"}, {"ReAmped3_ach_two"}, {"ReAmped3_ach_three"}, {"ReAmped3_ach_four"},
      {"ReAmped3_ach_five"}, {"ReAmped3_ach_six"}, {"ReAmped3_ach_seven"}, {"ReAmped3_ach_eight"},
      {"ReAmped3_ach_nine"}, {"ReAmped3_ach_ten"}, {"ReAmped3_ach_eleven"}, {"ReAmped3_ach_twelve"},
      {"ReAmped3_ach_thirteen"}, {"ReAmped3_ach_fourteen"}, {"ReAmped3_ach_fifteen"}},
     kAchievementRows},
    // The save backups, newest first: A restores one at the next start.
    {"ReAmped3Bak",
     "ReAmped3B",
     {{"ReAmped3_backup_one"}, {"ReAmped3_backup_two"}, {"ReAmped3_backup_three"}, {"ReAmped3_backup_four"},
      {"ReAmped3_backup_five"}, {"ReAmped3_backup_six"}, {"ReAmped3_backup_seven"}, {"ReAmped3_backup_eight"},
      {"ReAmped3_backup_nine"}, {"ReAmped3_backup_ten"}},
     kBackupRows},
};

// RK operand for a number constant (added if missing).
std::optional<uint32_t> ConstNumber(LuaFunction& f, double n) {
  for (size_t i = 0; i < f.consts.size(); ++i) {
    if (const auto* v = std::get_if<double>(&f.consts[i]); v && *v == n) {
      if (i + kMaxStack > 511) break;
      return uint32_t(i + kMaxStack);
    }
  }
  if (f.consts.size() >= 511 - kMaxStack) return std::nullopt;
  f.consts.emplace_back(n);
  return uint32_t(f.consts.size() - 1 + kMaxStack);
}

enum : uint32_t { kSetList = 35 };  // this game's Lua numbers its opcodes its own way

// The main list's items 5..page.items, every item's text from its TextID
// (<prefix>_Row<n>) through an ItemMapping, so the list scrolls through its
// four cells.
bool AddListItems(LuaFunction& f, const NativePage& page) {
  const char* keys[3] = {"AButton", "Left", "Right"};
  const std::string prefix = page.prefix;
  const auto items_pcs = SetTablePcs(f, "Items");
  if (items_pcs.empty()) return false;
  const size_t items_pc = items_pcs[0];
  const uint32_t info = A(f.code[items_pc]), list = C(f.code[items_pc]);
  const size_t setlist = items_pc - 1;
  if (Op(f.code[setlist]) != kSetList || A(f.code[setlist]) != list || ((f.code[setlist] >> 6) & 0x3FFFF) != 3) return false;
  const auto enabled = SetTablePcs(f, "Enabled");
  const auto text_id = Const(f, "TextID"), enabled_key = Const(f, "Enabled");
  if (!text_id || !enabled_key) return false;
  // The first four items' texts.
  for (int row = 3; row >= 0; --row) {
    const auto value = Const(f, prefix + "_Row" + std::to_string(row + 1));
    if (!value) return false;
    Insert(f, enabled[row] + 1, Encode(kSetTable, A(f.code[enabled[row]]), *text_id, *value));
  }
  // The new items, in the registers after the fourth.
  size_t pc = SetTablePcs(f, "Items")[0] - 1;
  const uint32_t first = list + 5, temp = list + 1 + uint32_t(page.items);
  std::vector<uint32_t> code;
  for (int n = 5; n <= page.items; ++n) {
    const uint32_t r = first + uint32_t(n - 5);
    const auto text = Const(f, prefix + "_Row" + std::to_string(n));
    if (!text) return false;
    code.push_back(Encode(kNewTable, r, 0, 3));
    code.push_back(Encode(kLoadBool, temp, 1, 0));
    code.push_back(Encode(kSetTable, r, *enabled_key, temp));
    code.push_back(Encode(kSetTable, r, *text_id, *text));
    for (int i = 0; i < 3; ++i) {
      if (!page.rows[n - 1][i]) continue;
      const auto key = Const(f, keys[i]), action = Const(f, std::string("$Select(") + page.rows[n - 1][i] + ")");
      if (!key || !action) return false;
      code.push_back(Encode(kSetTable, r, *key, *action));
    }
  }
  for (size_t i = 0; i < code.size(); ++i) Insert(f, pc + i, code[i]);
  pc += code.size();
  f.code[pc] = kSetList | uint32_t(page.items - 1) << 6 | list << 24;
  f.info[3] = uint8_t(std::max<uint32_t>(f.info[3], temp + 1));
  // info.ItemMapping = {{Control = "Text.textID", SourceAttribute = "TextID"}}
  const auto mapping_key = Const(f, "ItemMapping"), control = Const(f, "Control"),
             control_value = Const(f, "Text.textID"), source = Const(f, "SourceAttribute"), one = ConstNumber(f, 1.0);
  if (!mapping_key || !control || !control_value || !source || !one) return false;
  const uint32_t t = list, m = list + 1;
  const uint32_t mapping[] = {Encode(kNewTable, t, 0, 0),         Encode(kNewTable, m, 0, 2),
                              Encode(kSetTable, m, *control, *control_value), Encode(kSetTable, m, *source, *text_id),
                              Encode(kSetTable, t, *one, m),       Encode(kSetTable, info, *mapping_key, t)};
  const size_t after = pc + 2;  // after SETTABLE info['Items'] = list
  for (size_t i = 0; i < std::size(mapping); ++i) Insert(f, after + i, mapping[i]);
  return true;
}

bool NativeScene(LuaFunction& f, const NativePage& page) {
  if (f.lineinfo.size() != f.code.size()) return false;
  const std::string prefix = page.prefix;
  bool renamed = false;
  for (auto& k : f.consts) {
    if (auto* v = std::get_if<std::string>(&k); v && *v == "GlobalOptionsMenu2") *v = page.scene, renamed = true;
  }
  // The value lists: Hints' (first) shows <prefix>_Val3, Media Callouts' Val4.
  auto offs = SetTablePcs(f, "TextID", "OnOff_Button_Off"), ons = SetTablePcs(f, "TextID", "OnOff_Button_On");
  if (!renamed || offs.size() != 2 || ons.size() != 2) return false;
  if (!SetValue(f, offs[0], prefix + "_Val3") || !SetValue(f, ons[0], prefix + "_Val3") ||
      !SetValue(f, offs[1], prefix + "_Val4") || !SetValue(f, ons[1], prefix + "_Val4")) {
    return false;
  }
  // B: the options screen's Back action calls its class's SaveOptionValues,
  // which our scenes (the generic scene class) don't have, so it never pops.
  const auto backs = SetTablePcs(f, "BButton", "$Back");
  if (backs.size() != 1 || !SetValue(f, backs[0], "PopScene()")) return false;
  // Back: quick save and straight back to the game (save_hooks.cpp saves).
  if (REXCVAR_GET(quick_save_menus)) {
    const auto key = Const(f, "Back"), value = Const(f, "PushScene('ReAmped3_resume')");
    if (!key || !value) return false;
    Insert(f, backs[0] + 1, Encode(kSetTable, A(f.code[backs[0]]), *key, *value));
  }
  const auto enabled = SetTablePcs(f, "Enabled");
  if (enabled.size() != 4) return false;
  for (int row = 3; row >= 0; --row) {
    const size_t pc = enabled[row];
    const uint32_t a = A(f.code[pc]), load = f.code[pc - 1];
    if (Op(load) != kLoadBool || A(load) != C(f.code[pc])) return false;
    f.code[pc - 1] = Encode(kLoadBool, A(load), row < page.enabled_rows ? 1 : 0, 0);
    size_t start = pc, end = pc;
    while (start > 0 && Op(f.code[start - 1]) != kNewTable) --start;
    while (end + 1 < f.code.size() && Op(f.code[end + 1]) == kSetTable && A(f.code[end + 1]) == a) ++end;
    const char* keys[3] = {"AButton", "Left", "Right"};
    auto action = [&](int i) {
      const char* name = page.rows[row][i];
      return name ? std::string("$Select(") + name + ")" : std::string();
    };
    bool present[3] = {};
    for (size_t q = start; q <= end; ++q) {
      if (Op(f.code[q]) != kSetTable || A(f.code[q]) != a) continue;
      for (int i = 0; i < 3; ++i) {
        if (!IsConst(f, B(f.code[q]), keys[i])) continue;
        if (!SetValue(f, q, action(i))) return false;
        present[i] = true;
      }
    }
    for (int i = 2; i >= 0; --i) {
      if (!page.rows[row][i] || present[i]) continue;
      const auto key = Const(f, keys[i]);
      const auto value = Const(f, action(i));
      if (!key || !value) return false;
      Insert(f, pc + 1, Encode(kSetTable, a, *key, *value));
    }
  }
  return page.items <= 4 || AddListItems(f, page);
}

// `two_rows`: only the title and the top two rows (the game's own Global
// Options as our Extras page: its Hints and Media Callouts rows stay).
bool OptionsSceneTexts(LuaFunction& f, const std::string& prefix, bool two_rows) {
  if (f.lineinfo.size() != f.code.size()) return false;
  for (size_t pc : SetTablePcs(f, "TextID", "Options_Title")) {
    if (!SetValue(f, pc, prefix + "_Title")) return false;
  }
  for (const auto& [from, to] : {std::pair{"Options_Button02", "_Row2"}, std::pair{"Options_Button03", "_Row3"},
                                 std::pair{"Options_Button05", "_Row4"}}) {
    if (two_rows && std::string(to) != "_Row2") continue;
    const auto pcs = SetTablePcs(f, "TextID", from);
    if (pcs.empty() || !SetValue(f, pcs[0], prefix + to)) return false;
  }
  // The top row's label has no TextID (the template's default): one into its
  // empty ChangedAttributes table.
  const auto names = SetTablePcs(f, "Name", "TMP_Button_OptionsSecond:Menu");
  if (names.empty()) return false;
  size_t pc = names[0];
  while (pc > 0 && !(Op(f.code[pc - 1]) == kSetTable && IsConst(f, B(f.code[pc - 1]), "ChangedAttributes"))) --pc;
  if (pc == 0) return false;
  --pc;
  const uint32_t attrs = C(f.code[pc]), tmp = attrs + 1;
  const auto text_id = Const(f, "TextID"), row1 = Const(f, prefix + "_Row1"), key = Const(f, "Menu|Color_Text|Text");
  if (attrs >= kMaxStack || tmp + 1 >= f.info[3] || !text_id || !row1 || !key) return false;
  Insert(f, pc, Encode(kSetTable, attrs, *key, tmp));
  Insert(f, pc, Encode(kSetTable, tmp, *text_id, *row1));
  Insert(f, pc, Encode(kNewTable, tmp, 0, 1));
  return true;
}

bool NativeSceneData(LuaFunction& f, const NativePage& page) {
  if (!OptionsSceneTexts(f, page.prefix, false)) return false;
  // A list page has no values: its value fields and their arrows hidden.
  if (page.items > 4 || !page.values) {
    for (const char* node : {"OnOffHints", "OnOffCallouts"}) {
      const auto named = SetTablePcs(f, "Name", node);
      if (named.size() != 1) return false;
      const uint32_t reg = A(f.code[named[0]]);
      size_t q = named[0];
      while (q < f.code.size() && !(Op(f.code[q]) == kSetTable && A(f.code[q]) == reg && IsConst(f, B(f.code[q]), "Visible"))) ++q;
      if (q >= f.code.size() || Op(f.code[q - 1]) != kLoadBool || A(f.code[q - 1]) != C(f.code[q])) return false;
      f.code[q - 1] = Encode(kLoadBool, A(f.code[q - 1]), 0, 0);
    }
  }
  return true;
}

// --- StringTable.dat: u32 font count, 48-byte font names, u32 count, u32
// record offsets (sorted by id), u32 (file size - 0xFC), then records: u8 id
// length, id, NUL, pad to an even file offset, u16 length, UTF-16BE text, u8
// font, f32 scale.
// `text` gets the current text (null if the id is new) and returns the new
// one, or nullopt to leave the table as it is.
using StringEdit = std::function<std::optional<std::u16string>(const std::u16string*)>;
bool SetString(Bytes& d, const std::string& id, const StringEdit& edit) {
  if (d.size() < 4) return false;
  const size_t top = 4 + size_t(Be32(d.data())) * 48;
  if (top + 4 > d.size()) return false;
  const uint32_t count = Be32(&d[top]);
  const size_t base = top + 8 + size_t(count) * 4;
  if (base > d.size()) return false;
  struct Rec {
    std::string id;
    Bytes rest;  // u16 length .. scale
  };
  std::vector<Rec> recs;
  std::optional<std::u16string> old_text;
  Bytes old_tail;
  for (uint32_t i = 0; i < count; ++i) {
    size_t p = base + Be32(&d[top + 4 + i * 4]);
    if (p >= d.size()) return false;
    const size_t n = d[p];
    size_t q = p + 2 + n;
    q += q & 1;
    if (q + 2 > d.size()) return false;
    const size_t m = size_t(d[q]) << 8 | d[q + 1];
    const size_t end = q + 2 + 2 * m + 5;
    if (end > d.size()) return false;
    Rec r{std::string(reinterpret_cast<const char*>(&d[p + 1]), n), Bytes(d.begin() + q, d.begin() + end)};
    if (r.id == id) {
      std::u16string old;
      for (size_t c = 0; c < m; ++c) old.push_back(char16_t(d[q + 2 + 2 * c] << 8 | d[q + 3 + 2 * c]));
      old_text = std::move(old);
      old_tail.assign(d.begin() + q + 2 + 2 * m, d.begin() + end);  // font, scale
      continue;
    }
    recs.push_back(std::move(r));
  }
  const auto text = edit(old_text ? &*old_text : nullptr);
  if (!text) return true;
  Rec add{id, {}};
  add.rest.push_back(uint8_t(text->size() >> 8));
  add.rest.push_back(uint8_t(text->size()));
  for (char16_t c : *text) {
    add.rest.push_back(uint8_t(c >> 8));
    add.rest.push_back(uint8_t(c));
  }
  if (old_tail.size() == 5) {
    add.rest.insert(add.rest.end(), old_tail.begin(), old_tail.end());
  } else {
    add.rest.insert(add.rest.end(), {0, 0x3F, 0x80, 0, 0});  // font 0, scale 1.0
  }
  recs.insert(std::lower_bound(recs.begin(), recs.end(), add,
                               [](const Rec& a, const Rec& b) { return a.id < b.id; }),
              std::move(add));
  const size_t new_base = top + 8 + recs.size() * 4;
  Bytes body, offsets;
  for (const Rec& r : recs) {
    PutBe32(offsets, uint32_t(body.size()));
    body.push_back(uint8_t(r.id.size()));
    body.insert(body.end(), r.id.begin(), r.id.end());
    body.push_back(0);
    if ((new_base + body.size()) & 1) body.push_back(0);
    body.insert(body.end(), r.rest.begin(), r.rest.end());
  }
  Bytes out(d.begin(), d.begin() + top);
  PutBe32(out, uint32_t(recs.size()));
  out.insert(out.end(), offsets.begin(), offsets.end());
  PutBe32(out, uint32_t(new_base + body.size() - 0xFC));
  out.insert(out.end(), body.begin(), body.end());
  d = std::move(out);
  return true;
}

// Adds `id` (an existing one stays as it is).
bool AddString(Bytes& d, const std::string& id, const std::u16string& text) {
  return SetString(d, id, [&](const std::u16string* old) -> std::optional<std::u16string> {
    if (old) return std::nullopt;
    return text;
  });
}

// --- resources.dir index for one package: records of u32 0, 8-byte stamp,
// u32 compressed size, u32 size, u32 local header offset, u32 flags,
// NUL-terminated name; then u32 record count.
// `added`: new entry -> the entry whose record it copies.
bool Reindex(Bytes& index, const Bytes& package, const std::map<std::string, std::string>& added = {}) {
  std::map<std::string, std::array<uint32_t, 3>> entries;
  size_t eocd = package.size() - 22;
  while (eocd > 0 && Le32(&package[eocd]) != 0x06054B50) --eocd;
  size_t pos = Le32(&package[eocd + 16]);
  for (uint16_t e = Le16(&package[eocd + 10]); e > 0; --e) {
    const uint16_t nlen = Le16(&package[pos + 28]), xlen = Le16(&package[pos + 30]), clen = Le16(&package[pos + 32]);
    entries[std::string(reinterpret_cast<const char*>(&package[pos + 46]), nlen)] = {
        Le32(&package[pos + 20]), Le32(&package[pos + 24]), Le32(&package[pos + 42])};
    pos += 46 + nlen + xlen + clen;
  }
  size_t p = 0;
  std::map<std::string, Bytes> records;
  while (p + 28 <= index.size()) {
    const auto nul = std::find(index.begin() + p + 28, index.end(), 0);
    if (nul == index.end()) return false;
    const std::string name(index.begin() + p + 28, nul);
    const auto it = entries.find(name);
    if (it == entries.end()) return false;
    for (int i = 0; i < 3; ++i) SetBe32(&index[p + 12 + 4 * i], it->second[i]);
    records[name] = Bytes(index.begin() + p, index.begin() + p + 28);
    p = size_t(nul - index.begin()) + 1;
  }
  if (p + 4 != index.size()) return false;
  const uint32_t count = Be32(&index[p]);
  index.resize(p);
  for (const auto& [name, from] : added) {
    const auto rec = records.find(from);
    const auto entry = entries.find(name);
    if (rec == records.end() || entry == entries.end()) return false;
    Bytes r = rec->second;
    for (int i = 0; i < 3; ++i) SetBe32(&r[12 + 4 * i], entry->second[i]);
    r.insert(r.end(), name.begin(), name.end());
    r.push_back(0);
    index.insert(index.end(), r.begin(), r.end());
  }
  PutBe32(index, count + uint32_t(added.size()));
  return true;
}

std::optional<Bytes> ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return Bytes(std::istreambuf_iterator<char>(in), {});
}

bool WriteFile(const fs::path& path, const Bytes& data) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary);
    if (!out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()))) return false;
  }
  fs::rename(tmp, path, ec);
  return !ec;
}

// Our strings. The row and value texts are rewritten in memory with the
// current settings (options_hooks.cpp, same texts and lengths): padded with
// spaces to the longest value, values centred.
const std::pair<const char*, const char16_t*> kStrings[] = {
    {"ReAmped3_Options", u"ReAmped 3"},
    {"ReAmped3_Title", u"Options"},
    {"ReAmped3_Row1", u"Audio"},
    {"ReAmped3_Row2", u"Display"},
    {"ReAmped3_Row3", u"Extras"},
    {"ReAmped3_Row4", u"Quit ReAmped 3"},
    {"ReAmped3_Val3", u" "},
    {"ReAmped3_Val4", u" "},
    // Live texts (options_hooks.cpp GuestText: same initial texts and lengths).
    {"ReAmped3D_Title", u"Display"},
    {"ReAmped3D_Row1", u"UI size: 100%             "},
    {"ReAmped3D_Row2", u"Effects"},
    {"ReAmped3D_Row3", u"Frame rate"},
    {"ReAmped3D_Row4", u"Anti-aliasing"},
    {"ReAmped3D_Val3", u" 60 "},
    {"ReAmped3D_Val4", u" SMAA "},
    {"ReAmped3E_Title", u"Effects"},
    {"ReAmped3E_Row1", u"Distance fog: On "},
    {"ReAmped3E_Row2", u"Falling snow: On "},
    {"ReAmped3E_Row3", u"Board spray: On "},
    {"ReAmped3E_Row4", u"HUD: Shown "},
    {"ReAmped3E_Val3", u" "},
    {"ReAmped3E_Val4", u" "},
    {"ReAmped3U_Title", u"Audio"},
    {"ReAmped3U_Row1", u"Game audio"},
    {"ReAmped3U_Row2", u"Source: Game soundtrack "},
    {"ReAmped3U_Row3", u"Music volume: 100%      "},
    {"ReAmped3U_Row4", u"Next track"},
    {"ReAmped3U_Val3", u" "},
    {"ReAmped3U_Val4", u" "},
    {"ReAmped3X_Title", u"Extras"},
    {"ReAmped3X_Row1", u"Achievements"},
    {"ReAmped3X_Row2", u"Save backups"},
    {"ReAmped3X_Row3", u"Hints"},
    {"ReAmped3X_Row4", u" "},
    {"ReAmped3X_Val3", u" "},
    {"ReAmped3X_Val4", u" "},
    {"ReAmped3A_Title", u"Achievements"},
    {"ReAmped3A_Val3", u" "},
    {"ReAmped3A_Val4", u" "},
    {"ReAmped3B_Title", u"Save backups"},
    {"ReAmped3B_Val3", u" "},
    {"ReAmped3B_Val4", u" "},
};

// The achievements and backups pages' rows: unique placeholders ("#a01#",
// "#b01#" padded to 30), rewritten before shown (options_hooks.cpp).
std::u16string RowPlaceholder(char16_t kind, int n) {
  std::u16string text = u"#" + std::u16string(1, kind) + char16_t(u'0' + n / 10) + char16_t(u'0' + n % 10) + u"#";
  text.resize(30, u' ');
  return text;
}

// scene: the controls table whose A button runs `a_command` also runs
// `command` on `button` (e.g. Load a Rider: Y opens ReAmped 3).
bool AddButton(LuaFunction& f, const char* a_command, const char* button, const char* command,
               const char* match_key = "AButton") {
  const auto pcs = SetTablePcs(f, match_key, a_command);
  if (pcs.size() != 1 || !SetTablePcs(f, button).empty()) return false;
  const auto key = Const(f, button), value = Const(f, command);
  if (!key || !value) return false;
  Insert(f, pcs[0] + 1, Encode(kSetTable, A(f.code[pcs[0]]), *key, *value));
  return true;
}

// The game's texts about the console, its memory units and Xbox Live, said
// for a PC (English).
const std::pair<const char*, const char16_t*> kPcStrings[] = {
    {"SaveNotification", u"Saving progress. Do not close ReAmped 3 or turn off your PC."},
    {"Options_SaveNotification", u"Saving option values. Do not close ReAmped 3 or turn off your PC."},
    {"ErrorPop_DiskAccess_LowSpace",
     u"Your PC doesn't have enough free disk space to save games.  You need to free {0} more blocks."},
    {"MissingSaveDevice", u"Your saved character couldn't be found."},
    {"LoadCharacter_MURemovedMessage", u"Saved Data Is Missing"},
    {"PopUpMessage_Menu_A_ONLY_Legend", u"Press \t to continue"},
    {"Reward_achieve", u"New Achievement!"},
    {"Reward_achieve_desc", u"Achievements are in the ReAmped 3 menu: Extras, Achievements"},
    {"StartScreen_NoProfile", u"You are not signed in.  Your progress will not be saved.  Do you want to continue?"},
    {"ErrorPop_NetworkCannotAccessService", u"Online play is not available."},
    {"ErrorPop_NetworkConnectionLost", u"Online play is not available."},
    {"ErrorPop_NetworkDuplicateLogon", u"Online play is not available."},
    {"ErrorPop_NetworkUpdateRequired", u"Online play is not available."},
    {"RiderInfoNotOnlineMsg", u"Leaderboards are not available."},
    {"Legend_XBOXLive_TextNormal", u" Online"},
};

// Load a Rider's button legends ("A Select  RB Delete Rider") gain the Y
// button: 0x0D is the Y glyph in the game's fonts.
constexpr const char* kLoadRiderLegends[] = {"LoadCharacter_HelpText", "LoadCharacter_HelpText2",
                                             "LoadCharacter_HelpTextB", "LoadCharacter_HelpText2B",
                                             "Options_HelpText"};  // the main menu's Options
constexpr char16_t kLegendY[] = u" \x0DReAmped 3";

// Quick save: Back in a menu (the pause screen, the Options page, our pages)
// runs ReAmped3_resume[_pause]: options_hooks.cpp saves and backs out page by
// page to the game (save_hooks.cpp). While riding, Back stays the game's (its
// tip; reset when stuck). The legends say so (0x0E is the Back glyph).
constexpr const char* kQuickSaveLegends[] = {"RadialMenu_IntroButtonLegend", "RadialMenu_BuildButtonLegend",
                                             "RadialMenu_RestartRunButtonLegend", "Options_HelpText2"};
constexpr char16_t kLegendQuickSave[] = u"  \x0E Quick save";
// Quick load, the pause screen only (LB, glyph 0x14): the last save back.
constexpr const char* kQuickLoadLegends[] = {"RadialMenu_IntroButtonLegend", "RadialMenu_BuildButtonLegend",
                                             "RadialMenu_RestartRunButtonLegend"};
constexpr char16_t kLegendQuickLoad[] = u"  \x14 Quick load";

// Patched files under `out`; false (and nothing used) on any error.
bool Build(const fs::path& game, const fs::path& out) {
  const auto game_rui = ReadFile(game / "GuiPackages" / "Game.rui");
  const auto base_rui = ReadFile(game / "GuiPackages" / "Base.rui");
  const auto shell_rui = ReadFile(game / "GuiPackages" / "Shell.rui");
  const auto resources = ReadFile(game / "resources.dir");
  if (!game_rui || !base_rui || !shell_rui || !resources) {
    REXLOG_WARN("Amped3: UI patch: the game's UI packages weren't found");
    return false;
  }
  std::map<std::string, Patch> game_patches = {
      // The game's in-ride Global Options becomes our Extras page:
      // Achievements and Save backups on its top rows (Audio, and Cheat
      // Codes - always disabled while riding), its own Hints and Media
      // Callouts below.
      {"Scenes/GlobalOptionsMenu2.sceneData",
       [](Bytes& d) { return EditLua(d, [](LuaFunction& f) { return OptionsSceneTexts(f, "ReAmped3X", true); }); }},
      {"Scenes/GlobalOptionsMenu2.scene",
       [](Bytes& d) {
         return EditLua(d, [](LuaFunction& f) {
           const auto audio = SetTablePcs(f, "AButton", "$Select(AudioAndMusicOptions2)");
           return audio.size() == 1 && SetValue(f, audio[0], "$Select(ReAmped3Ach)") &&
                  EnableReAmpedItem(f, 2, "$Select(ReAmped3Bak)") &&
                  (!REXCVAR_GET(quick_save_menus) || AddButton(f, "$Back", "Back", "PushScene('ReAmped3_resume')", "BButton"));
         });
       }},
      // The pause screen: Back saves and unpauses.
      {"Scenes/RadialMenu.scene",
       [](Bytes& d) {
         return EditLua(d, [](LuaFunction& f) {
           // Options: our Options page (Audio / Display / Extras / Quit).
           const auto options = SetTablePcs(f, "AButton", "$Options");
           if (options.size() != 1 || !SetValue(f, options[0], "PushScene('ReAmped3')")) return false;
           return (!REXCVAR_GET(quick_save_menus) ||
                   AddButton(f, "$Unpause", "Back", "PushScene('ReAmped3_resume_pause')", "BButton")) &&
                  (!REXCVAR_GET(quick_load) ||
                   AddButton(f, "$Unpause", "WhiteButton", "PushScene('ReAmped3_load_pause')", "BButton"));
         });
       }},
  };
  // The game-styled ReAmped 3 scenes, listed in the package.
  game_patches["Game.package"] = [](Bytes& d) {
    const std::string from = "\t\t\"GlobalOptionsMenu2\",\r\n";
    const auto it = std::search(d.begin(), d.end(), from.begin(), from.end());
    if (it == d.end()) return false;
    std::string add;
    for (const NativePage& page : kNativePages) add += std::string("\t\t\"") + page.scene + "\",\r\n";
    d.insert(it + from.size(), add.begin(), add.end());
    return true;
  };
  Adds game_adds;
  for (const NativePage& page : kNativePages) {
    const std::string to = std::string("Scenes/") + page.scene;
    game_adds[to + ".scene"] = {"Scenes/GlobalOptionsMenu2.scene",
                                [&page](Bytes& d) { return EditLua(d, [&](LuaFunction& f) { return NativeScene(f, page); }); }};
    game_adds[to + ".sceneData"] = {
        "Scenes/GlobalOptionsMenu2.sceneData",
        [&page](Bytes& d) { return EditLua(d, [&](LuaFunction& f) { return NativeSceneData(f, page); }); }};
    game_adds[to + ".anims"] = {"Scenes/GlobalOptionsMenu2.anims", [](Bytes&) { return true; }};
    game_adds[to + ".actions"] = {"Scenes/GlobalOptionsMenu2.actions", [](Bytes&) { return true; }};
  }
  // UI sizes: the game's scenes, and our pages (menus, made from its options page).
  AddSizePatches(*game_rui, game_patches);
  if (const double menus = REXCVAR_GET(ui_size) / 100.0 * REXCVAR_GET(ui_size_menus) / 100.0,
      legends = REXCVAR_GET(ui_size_legends) / 100.0;
      std::abs(menus - 1.0) > 1e-4 || std::abs(legends - 1.0) > 1e-4) {
    for (auto& [name, add] : game_adds) {
      if (name.size() < 10 || name.compare(name.size() - 10, 10, ".sceneData") != 0) continue;
      add.second = [made = add.second, menus, legends](Bytes& d) {
        if (!made(d)) return false;
        EditLua(d, [&](LuaFunction& f) { return ScaleScene(f, menus, legends); });
        return true;
      };
    }
  }
  std::map<std::string, std::string> game_added;
  for (const auto& [name, add] : game_adds) game_added[name] = add.first;
  // English only: Base.rui grows by each patched table (stored, not
  // compressed), and at ~1 MB the game fails to start.
  std::map<std::string, Patch> base_patches;
  base_patches["StringTables/eng/StringTable.dat"] = [](Bytes& d) {
    for (const auto& [id, text] : kStrings) {
      if (!AddString(d, id, text)) return false;
    }
    for (int n = 1; n <= kAchievementRows; ++n) {
      if (!AddString(d, "ReAmped3A_Row" + std::to_string(n), RowPlaceholder(u'a', n))) return false;
    }
    for (int n = 1; n <= kBackupRows; ++n) {
      if (!AddString(d, "ReAmped3B_Row" + std::to_string(n), RowPlaceholder(u'b', n))) return false;
    }
    for (const char* id : kQuickLoadLegends) {
      if (!REXCVAR_GET(quick_load)) break;
      if (!SetString(d, id, [](const std::u16string* old) -> std::optional<std::u16string> {
            if (!old) return std::nullopt;
            return *old + kLegendQuickLoad;
          })) {
        return false;
      }
    }
    for (const char* id : kQuickSaveLegends) {
      if (!REXCVAR_GET(quick_save_menus)) break;
      if (!SetString(d, id, [](const std::u16string* old) -> std::optional<std::u16string> {
            if (!old) return std::nullopt;
            return *old + kLegendQuickSave;
          })) {
        return false;
      }
    }
    for (const auto& [id, text] : kPcStrings) {
      if (!SetString(d, id, [text = text](const std::u16string* old) -> std::optional<std::u16string> {
            if (!old) return std::nullopt;
            return std::u16string(text);
          })) {
        return false;
      }
    }
    for (const char* id : kLoadRiderLegends) {
      if (!SetString(d, id, [](const std::u16string* old) -> std::optional<std::u16string> {
            if (!old) return std::nullopt;
            return *old + kLegendY;
          })) {
        return false;
      }
    }
    return true;
  };
  // The front end: Y on Load a Rider (Start goes straight there) opens the
  // ReAmped 3 screen.
  std::map<std::string, Patch> shell_patches = {
      {"Scenes/LoadCharacter.scene",
       [](Bytes& d) {
         return EditLua(d, [](LuaFunction& f) { return AddButton(f, "$SelectGame", "YButton", "PushScene('ReAmped3')"); });
       }},
      // And Y on the main menu's Options.
      {"Scenes/GlobalOptionsMenu.scene",
       [](Bytes& d) {
         return EditLua(d, [](LuaFunction& f) {
           return AddButton(f, "$Back", "YButton", "PushScene('ReAmped3')", "BButton");
         });
       }},
  };
  AddSizePatches(*base_rui, base_patches);
  AddSizePatches(*shell_rui, shell_patches);
  const auto new_game = RebuildZip(*game_rui, game_patches, game_adds);
  const auto new_base = RebuildZip(*base_rui, base_patches);
  if (!new_game || !new_base) return false;
  // The same screens in the front end's package (Shell and Game only share
  // Base), copied from Game's, with Shell's options screen's entry headers.
  shell_patches["Shell.package"] = [](Bytes& d) {
    const std::string from = "\t\t\"GlobalOptionsMenu\",\r\n";
    const auto it = std::search(d.begin(), d.end(), from.begin(), from.end());
    if (it == d.end()) return false;
    std::string add;
    for (const NativePage& page : kNativePages) add += std::string("\t\t\"") + page.scene + "\",\r\n";
    d.insert(it + from.size(), add.begin(), add.end());
    return true;
  };
  Adds shell_adds;
  for (const auto& [name, add] : game_adds) {
    const auto ext = name.substr(name.rfind('.'));
    if (ext == ".anims" || ext == ".actions") {  // the front end's own
      shell_adds[name] = {"Scenes/GlobalOptionsMenu" + ext, [](Bytes&) { return true; }};
      continue;
    }
    auto built = ReadEntry(*new_game, name);
    if (!built) return false;
    shell_adds[name] = {"Scenes/GlobalOptionsMenu" + ext, [built = std::move(*built)](Bytes& d) {
                          d = built;
                          return true;
                        }};
  }
  std::map<std::string, std::string> shell_added;
  for (const auto& [name, add] : shell_adds) shell_added[name] = add.first;
  const auto new_shell = RebuildZip(*shell_rui, shell_patches, shell_adds);
  if (!new_shell) return false;
  auto new_resources = RebuildZip(*resources, {
      {"GuiPackages/Game.rui", [&](Bytes& d) { return Reindex(d, *new_game, game_added); }},
      {"GuiPackages/Base.rui", [&](Bytes& d) { return Reindex(d, *new_base); }},
      {"GuiPackages/Shell.rui", [&](Bytes& d) { return Reindex(d, *new_shell, shell_added); }},
  });
  if (!new_resources) return false;
  // The title logo (Shell.ruirr): optional, the rest stands without it.
  if (REXCVAR_GET(title_logo)) {
    const auto shell_ruirr = ReadFile(game / "GuiPackages" / "Shell.ruirr");
    std::optional<Bytes> new_ruirr;
    if (shell_ruirr) {
      new_ruirr = RebuildZip(*shell_ruirr, {{"Textures/AmpedBoxLogo.xpr", [](Bytes& d) { return title_logo::Patch(d); }}});
    }
    std::optional<Bytes> indexed;
    if (new_ruirr) {
      indexed = RebuildZip(*new_resources, {{"GuiPackages/Shell.ruirr", [&](Bytes& d) { return Reindex(d, *new_ruirr); }}});
    }
    if (indexed && WriteFile(out / "GuiPackages" / "Shell.ruirr", *new_ruirr)) {
      new_resources = std::move(indexed);
    } else {
      REXLOG_WARN("Amped3: UI patch: the title logo wasn't changed");
    }
  }
  return WriteFile(out / "GuiPackages" / "Game.rui", *new_game) &&
         WriteFile(out / "GuiPackages" / "Base.rui", *new_base) &&
         WriteFile(out / "GuiPackages" / "Shell.rui", *new_shell) &&
         WriteFile(out / "resources.dir", *new_resources);
}

}  // namespace

std::vector<uint8_t> OriginalTitleLogo() {
  const fs::path game = REXCVAR_GET(game_data_root);
  const auto shell = ReadFile(game / "GuiPackages" / "Shell.ruirr");
  const auto logo = shell ? ReadEntry(*shell, "Textures/AmpedBoxLogo.xpr") : std::nullopt;
  return logo ? *logo : Bytes{};
}

std::string SharpUiFolder() {
  // The texture pack (tools/ui_pack.py): the runtime swaps each UI texture
  // for its enlarged copy, found by the texture's content.
  const fs::path folder = rex::filesystem::GetExecutableFolder() / "textures";
  std::error_code ec;
  if (!REXCVAR_GET(sharp_ui) || !fs::is_directory(folder / "replace", ec)) return {};
  REXLOG_INFO("Amped3: sharp UI pack from {}", folder.string());
  // Our title logo (title_logo.cpp) is a changed texture the pack can't
  // know: made from the pack's enlargement of the original, once per pack
  // (the launcher replaces the whole folder when it makes a new one).
  const fs::path marker = folder / "replace" / "title_logo.txt";
  std::string made;
  if (std::ifstream in(marker); in) std::getline(in, made);
  const std::string want = "the sharp title logo is in this pack, version " + std::to_string(title_logo::kLogoVersion);
  if (REXCVAR_GET(ui_patch) && REXCVAR_GET(title_logo) && made != want) {
    try {
      const fs::path game = REXCVAR_GET(game_data_root);
      const auto shell = ReadFile(game / "GuiPackages" / "Shell.ruirr");
      const auto logo = shell ? ReadEntry(*shell, "Textures/AmpedBoxLogo.xpr") : std::nullopt;
      const auto started = std::chrono::steady_clock::now();
      if (logo && title_logo::WriteSharp(*logo, folder / "replace")) {
        std::ofstream(marker) << want << "\n";
        REXLOG_INFO("Amped3: sharp UI: title logo made in {} ms",
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                        .count());
      }
    } catch (const std::exception& e) {
      REXLOG_WARN("Amped3: sharp UI: title logo: {}", e.what());
    }
  }
  return folder.string();
}

void Prepare() {
  if (!REXCVAR_GET(ui_patch)) return;
  try {
    const fs::path game = REXCVAR_GET(game_data_root);
    const std::string user_root = REXCVAR_GET(user_data_root);
    const fs::path out = (user_root.empty() ? rex::filesystem::GetUserFolder() / "amped3" : fs::path(user_root)) /
                         "cache" / "ui_overlay";
    // Rebuilt when the originals or the patches change.
    std::ostringstream stamp;
    stamp << kVersion << "+sizes" << REXCVAR_GET(ui_size_debug_no_insert) << REXCVAR_GET(ui_size_debug_scenes) << REXCVAR_GET(ui_size) << '/' << REXCVAR_GET(ui_size_menus) << '/'
          << REXCVAR_GET(ui_size_hud) << '/' << REXCVAR_GET(ui_size_tips) << '/' << REXCVAR_GET(ui_size_music) << '/'
          << REXCVAR_GET(ui_size_legends) << (REXCVAR_GET(title_logo) ? "+logo" + std::to_string(title_logo::kLogoVersion) : "") << (REXCVAR_GET(quick_load) ? "+load" : "") << (REXCVAR_GET(quick_save_menus) ? "+save" : "");
    for (const char* name : {"GuiPackages/Game.rui", "GuiPackages/Base.rui", "GuiPackages/Shell.rui",
                             "GuiPackages/Shell.ruirr", "resources.dir"}) {
      std::error_code ec;
      stamp << '|' << fs::file_size(game / name, ec) << ':'
            << fs::last_write_time(game / name, ec).time_since_epoch().count();
    }
    std::string old_stamp;
    if (std::ifstream in(out / "stamp.txt"); in) std::getline(in, old_stamp);
    if (old_stamp != stamp.str()) {
      std::error_code ec;
      fs::remove_all(out, ec);
      if (!Build(game, out)) {
        fs::remove_all(out, ec);
        REXLOG_WARN("Amped3: UI patch: not applied (the game's own Options stay as they are)");
        return;
      }
      std::ofstream(out / "stamp.txt") << stamp.str() << "\n";
      REXLOG_INFO("Amped3: UI patch: written to {}", out.string());
    }
    rex::cvar::SetFlagByName("game_data_overlay", out.string());
  } catch (const std::exception& e) {
    REXLOG_WARN("Amped3: UI patch: {}", e.what());
  }
}

}  // namespace amped3::ui_patch

// radio_text.cpp: the record labels in the game's music tags
// (audio/XACTData/XACTData.res: Pyro.xap.dat, "TAGINFO:^LABEL^<label>^...").
std::vector<std::string> Amped3_MusicLabels() {
  namespace fs = std::filesystem;
  std::vector<std::string> labels;
  const fs::path game = REXCVAR_GET(game_data_root);
  const auto res = amped3::ui_patch::ReadFile(game / "audio" / "XACTData" / "XACTData.res");
  const auto dat = res ? amped3::ui_patch::ReadEntry(*res, "Pyro.xap.dat") : std::nullopt;
  if (!dat) return labels;
  const std::string text(dat->begin(), dat->end());
  const std::string key = "^LABEL^";
  for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + 1)) {
    const size_t start = at + key.size(), end = text.find('^', start);
    if (end == std::string::npos || end - start > 64) continue;
    const std::string label = text.substr(start, end - start);
    if (std::find(labels.begin(), labels.end(), label) == labels.end()) labels.push_back(label);
  }
  return labels;
}
