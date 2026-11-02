// The sharp UI pack, made on the player's PC from their own game files: every
// UI texture of Amped 3 (GuiPackages\*.ruirr, ZIPs of XPR2 textures, and the
// loading screens, loadingscreen.res, XTX textures) decoded,
// enlarged with Real-ESRGAN (tools\esrgan next to the launcher) and saved as
// <textures>\replace\<key>.png, which the game swaps in for the original
// (project/src/ui_patch.cpp SharpUiFolder). ReAmped 3 ships no game art: this
// is the C++ form of tools/ui_pack.py, run by the launcher.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace uipack {

// Bump when the pack's content changes (format, models, keys): rebuilds it.
constexpr int kVersion = 7;
// The oldest pack still used as it is: a pack is made once and kept (it takes
// minutes, up to 17 on the Steam Deck); raise this only for a change that
// must reach existing packs.
constexpr int kMinVersion = 2;  // 2: the loading screens too; 3: them at 2x; 4: button prompts not via the upscaler; 5: the radio's pixel font; 6: clean button edges, the fonts' buttons from the button sheet; 7: brighter A/B/X/Y

struct Options {
  std::string game_dir;      // the extracted Amped 3 folder (default.xex)
  std::string textures_dir;  // gets replace\ (the pack), pack.txt and work\ (while building)
  std::string esrgan_dir;    // realesrgan-ncnn-vulkan.exe and models\ (with a trailing backslash or not)
  // 4 (PCs: sharp at 4K), or 2 (Steam Deck: the 4x images halved, a quarter
  // of the memory, still sharp at 1280x800). The loading screens are 2x on
  // both.
  int scale = 4;
};

enum class Stage { kReading, kUpscalingArt, kUpscalingPhotos, kFinishing };

// Read by the UI while Build runs.
struct Progress {
  std::atomic<int> stage{int(Stage::kReading)};
  std::atomic<int> done{0};   // textures enlarged so far
  std::atomic<int> total{0};  // textures in the pack (known after kReading)
};

// realesrgan-ncnn-vulkan.exe and both models (art, photo) are in esrgan_dir.
bool UpscalerPresent(const std::string& esrgan_dir);
// The pack exists and was made by this version, at this scale, from these
// game files (their GuiPackages\*.ruirr names, sizes and times).
bool UpToDate(const Options& o);

// Makes the pack: work\ holds the originals and the upscaler's output (kept
// when cancelled, so the next build carries on), then replace.new\ is filled
// and renamed to replace\, so the game never sees half a pack. false with
// `error` (short, for the player) when it can't; cancel stops it (the
// upscaler is killed).
bool Build(const Options& o, Progress& progress, const std::atomic<bool>& cancel, std::string& error);

// The title screen's REAMPED3 logo (the game's own logo, edited as in game:
// project/src/title_logo.cpp) as out_png, for the launcher's header: from the
// pack's enlargement when it's there. ReAmped 3 ships no game art: made here
// from the player's files.
bool WriteLauncherLogo(const Options& o, const std::string& out_png);

// ---- the pure parts (checked by --selftest) ----------------------------

struct Image {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;  // width * height * 4
};

// XGAddress2DTiledOffset: element index of (x, y) in a tiled surface `width`
// elements wide (blocks for compressed formats), bpb bytes per element.
uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t width, uint32_t bpb);
// The runtime's replacement key: XXH3-64 (16 lowercase hex digits) of the
// XPR's base level as stored (tiled, whole 32x32-block tiles). "" if not a
// texture this reads.
std::string TextureKey(const uint8_t* xpr, size_t size);
// The base level as RGBA, as tools/xpr_extract.py decodes it.
bool DecodeXpr(const uint8_t* xpr, size_t size, Image& out, std::string& error);
// Halves an image (2x2 boxes, colour weighted by alpha so edges don't darken).
Image Half(const Image& in);
// The stamp file's text for these game files (pack.txt).
std::string StampText(int scale, const std::vector<std::string>& lines);

}  // namespace uipack
