// ui_pack.cpp - See ui_pack.h. The same steps as tools/ui_pack.py.

#include "ui_pack.h"

#include "logic.h"
#include "system.h"

#include <miniz.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_WINDOWS_UTF8
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>
#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include "../../src/title_logo.h"

#include <filesystem>

namespace uipack {
namespace {

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

struct Format {
  int bytes_per_block, block;
};

// Xenos texture formats (fetch constant dword 1, bits 0-5) the UI uses.
bool FormatOf(uint32_t fmt, Format& out) {
  switch (fmt) {
    case 2: out = {1, 1}; return true;    // 8
    case 4: out = {2, 1}; return true;    // 5_6_5
    case 6: out = {4, 1}; return true;    // 8_8_8_8
    case 10: out = {2, 1}; return true;   // 8_8
    case 15: out = {2, 1}; return true;   // 4_4_4_4
    case 18: out = {8, 4}; return true;   // DXT1
    case 19: out = {16, 4}; return true;  // DXT2_3
    case 20: out = {16, 4}; return true;  // DXT4_5
  }
  return false;
}

struct Header {
  uint32_t data_offset, fmt, endian, width, height, pitch;
  bool tiled;
};

// XPR2 files (the menus' packages), or bare XTX textures (a D3D texture
// header, then its data: the loading screens).
bool ReadHeader(const uint8_t* xpr, size_t size, Header& h) {
  uint32_t fetch;
  if (size >= 0x48 && std::memcmp(xpr, "XPR2", 4) == 0) {
    fetch = 0x3C;
    h.data_offset = 12 + Be32(xpr + 4);  // the header size leaves out the first 12 bytes
  } else if (size >= 40 && Be32(xpr) == 0x00030001) {
    fetch = 0x10;
    h.data_offset = 40;
  } else {
    return false;
  }
  const uint32_t d0 = Be32(xpr + fetch), d1 = Be32(xpr + fetch + 4), d2 = Be32(xpr + fetch + 8);
  h.fmt = d1 & 0x3F;
  h.endian = (d1 >> 6) & 3;
  h.tiled = (d0 & 0x80000000u) != 0;
  h.width = (d2 & 0x1FFF) + 1;
  h.height = ((d2 >> 13) & 0x1FFF) + 1;
  h.pitch = ((d0 >> 22) & 0x1FF) * 32;
  return h.data_offset <= size;
}

void Swap(std::vector<uint8_t>& d, uint32_t endian) {
  if (endian == 1) {  // 8in16
    for (size_t i = 0; i + 1 < d.size(); i += 2) std::swap(d[i], d[i + 1]);
  } else if (endian == 2) {  // 8in32
    for (size_t i = 0; i + 3 < d.size(); i += 4) {
      std::swap(d[i], d[i + 3]);
      std::swap(d[i + 1], d[i + 2]);
    }
  } else if (endian == 3) {  // 16in32
    for (size_t i = 0; i + 3 < d.size(); i += 4) {
      std::swap(d[i], d[i + 2]);
      std::swap(d[i + 1], d[i + 3]);
    }
  }
}

void Rgb565(uint16_t c, uint8_t* out) {
  out[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
  out[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
  out[2] = uint8_t((c & 31) * 255 / 31);
}

// One 4x4 block of DXT1 colour (little-endian after the swap) into rgba
// (stride: the image width), with alpha when `alpha1` (DXT1's own).
void ColourBlock(const uint8_t* b, uint8_t* rgba, int stride, bool alpha1) {
  const uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
  uint8_t p[4][4];
  Rgb565(c0, p[0]);
  Rgb565(c1, p[1]);
  p[0][3] = p[1][3] = p[2][3] = p[3][3] = 255;
  if (c0 > c1 || !alpha1) {
    for (int i = 0; i < 3; ++i) {
      p[2][i] = uint8_t((2 * p[0][i] + p[1][i]) / 3);
      p[3][i] = uint8_t((p[0][i] + 2 * p[1][i]) / 3);
    }
  } else {
    for (int i = 0; i < 3; ++i) p[2][i] = uint8_t((p[0][i] + p[1][i]) / 2);
    p[3][0] = p[3][1] = p[3][2] = p[3][3] = 0;
  }
  const uint32_t bits = uint32_t(b[4]) | uint32_t(b[5]) << 8 | uint32_t(b[6]) << 16 | uint32_t(b[7]) << 24;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      const uint8_t* c = p[(bits >> (2 * (y * 4 + x))) & 3];
      uint8_t* o = rgba + (size_t(y) * stride + x) * 4;
      o[0] = c[0], o[1] = c[1], o[2] = c[2];
      if (alpha1) o[3] = c[3];
    }
  }
}

void ExplicitAlpha(const uint8_t* b, uint8_t* rgba, int stride) {  // DXT3
  for (int i = 0; i < 16; ++i) {
    const int nibble = (b[i / 2] >> ((i & 1) * 4)) & 15;
    rgba[(size_t(i / 4) * stride + i % 4) * 4 + 3] = uint8_t(nibble * 17);
  }
}

void InterpolatedAlpha(const uint8_t* b, uint8_t* rgba, int stride) {  // DXT5
  uint8_t a[8];
  a[0] = b[0], a[1] = b[1];
  if (a[0] > a[1]) {
    for (int i = 1; i < 7; ++i) a[i + 1] = uint8_t(((7 - i) * a[0] + i * a[1]) / 7);
  } else {
    for (int i = 1; i < 5; ++i) a[i + 1] = uint8_t(((5 - i) * a[0] + i * a[1]) / 5);
    a[6] = 0, a[7] = 255;
  }
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
  for (int i = 0; i < 16; ++i) rgba[(size_t(i / 4) * stride + i % 4) * 4 + 3] = a[(bits >> (3 * i)) & 7];
}

void Jpg(const Image& image, const std::string& path, std::string& error) {
  std::string data;
  stbi_write_jpg_to_func([](void* out, void* bytes, int size) {
    static_cast<std::string*>(out)->append(static_cast<const char*>(bytes), size_t(size));
  }, &data, image.width, image.height, 4, image.rgba.data(), 92);
  if (data.empty() || !sys::WriteFile(path, data)) error = "can't write " + path;
}

void Png(const Image& image, const std::string& path, std::string& error) {
  size_t size = 0;
  void* png = tdefl_write_image_to_png_file_in_memory_ex(image.rgba.data(), image.width, image.height, 4, &size, 6,
                                                         MZ_FALSE);
  if (!png || !sys::WriteFile(path, std::string(static_cast<const char*>(png), size))) {
    error = "can't write " + path;
  }
  mz_free(png);
}

bool EndsWithNoCase(const std::string& s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suffix[i]) return false;
  }
  return true;
}

std::string BaseName(const std::string& entry) {
  const size_t slash = entry.find_last_of("/\\");
  std::string name = slash == std::string::npos ? entry : entry.substr(slash + 1);
  return name.substr(0, name.size() - 4);  // ".xpr" / ".xtx"
}

std::string PackagesDir(const std::string& game_dir) { return launcher::JoinPath(game_dir, "GuiPackages"); }

// The .ruirr files (ZIPs of the UI's textures), sorted.
std::vector<std::string> Packages(const std::string& game_dir) {
  std::vector<std::string> files, out;
  sys::ListFiles(PackagesDir(game_dir), files);
  for (const std::string& f : files) {
    if (EndsWithNoCase(f, ".ruirr")) out.push_back(f);
  }
  std::sort(out.begin(), out.end());
  return out;
}

// The archives with UI textures: the menus' packages (XPR2 textures) and the
// loading screens (XTX).
struct Source {
  std::string path;
  bool screens;  // the loading screens
};
std::vector<Source> Sources(const std::string& game_dir) {
  std::vector<Source> out;
  for (const std::string& f : Packages(game_dir)) out.push_back({launcher::JoinPath(PackagesDir(game_dir), f), false});
  const std::string screens = launcher::JoinPath(game_dir, "loadingscreen.res");
  if (sys::FileExists(screens)) out.push_back({screens, true});
  return out;
}

std::vector<std::string> StampLines(const std::string& game_dir) {
  std::vector<std::string> lines;
  for (const Source& src : Sources(game_dir)) {
    const size_t slash = src.path.find_last_of("\\/");
    lines.push_back(src.path.substr(slash + 1) + " " + std::to_string(sys::FileSize(src.path)) + " " +
                    std::to_string(sys::FileTime(src.path)));
  }
  return lines;
}

// The three kinds, each upscaled in its own folder: drawn art, icons and
// fonts (the art model keeps them cleanest); the menus' mountain photos and
// the loading screens (the general model keeps their detail); the loading
// screens are opaque full-screen pictures, kept as JPEG (a tenth of a PNG)
// at 2x (Build).
// kIcon: flat graphic sheets (the button prompts): enlarged here without the
// upscaler, which makes their letters and rims wobble (EnlargeIcon).
// kPixel: pixel fonts (the radio's LCD text): each pixel made 4x4, exactly
// the original, only sharp.
enum Kind { kArt, kPhoto, kScreen, kIcon, kPixel, kKinds };
Kind KindOf(const std::string& name, bool screens) {
  // The loading screens' dancer and its "loading" label are cut-outs drawn
  // over the card: art with transparency (PNG), not opaque pictures (JPEG
  // turned their clear parts black, the label's box over the dancer).
  if (screens && (name.rfind("loading_dancer", 0) == 0 || name.rfind("load_poop", 0) == 0)) return kArt;
  if (screens) return kScreen;
  if (name == "XMRadio_supernatural_18") return kPixel;
  if (name == "Xbox360buttons" || name == "ArrowDPad") return kIcon;
  return name.rfind("Background", 0) == 0 ? kPhoto : kArt;
}
// The Steam Deck's 2x pack uses the art model throughout: about four times
// lighter than the general one on a GPU an eighth of a desktop's, and the
// difference doesn't show at 1280x800.
const char* ModelFor(Kind kind, int scale) {
  return kind == kArt || scale == 2 ? "realesrgan-x4plus-anime" : "realesrgan-x4plus";
}
const char* ExtensionFor(Kind kind) { return kind == kScreen ? ".jpg" : ".png"; }

std::string Quote(const std::string& s) { return "\"" + s + "\""; }

Image EnlargePixels(const Image& in) {
  Image out;
  out.width = in.width * 4;
  out.height = in.height * 4;
  out.rgba.resize(size_t(out.width) * out.height * 4);
  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < out.width; ++x) {
      std::memcpy(&out.rgba[(size_t(y) * out.width + x) * 4], &in.rgba[(size_t(y / 4) * in.width + x / 4) * 4], 4);
    }
  }
  return out;
}

// 4x for flat art: bicubic (Catmull-Rom) on premultiplied colour, then each
// edge made crisp again: alpha through a steep ramp (~1 px wide), and the
// colour snapped to the sheet's flat colours, blending only the two either
// side of an edge. No halos (an unsharp mask left white fringes) and no
// wobble (the upscaler's).
Image EnlargeIcon(const Image& in) {
  const int w = in.width, h = in.height, W = w * 4, H = h * 4;
  std::vector<float> src(size_t(w) * h * 4);
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    const float a = in.rgba[i * 4 + 3] / 255.0f;
    for (int c = 0; c < 3; ++c) src[i * 4 + c] = in.rgba[i * 4 + c] / 255.0f * a;
    src[i * 4 + 3] = a;
  }
  auto cubic = [](float t) {
    t = std::fabs(t);
    return t < 1 ? 1.5f * t * t * t - 2.5f * t * t + 1 : t < 2 ? -0.5f * t * t * t + 2.5f * t * t - 4 * t + 2 : 0.0f;
  };
  // Separable: rows then columns.
  std::vector<float> mid(size_t(W) * h * 4), big(size_t(W) * H * 4);
  for (int y = 0; y < h; ++y) {
    for (int X = 0; X < W; ++X) {
      const float sx = (X + 0.5f) / 4 - 0.5f;
      const int x0 = int(std::floor(sx));
      float acc[4] = {}, wsum = 0;
      for (int k = -1; k <= 2; ++k) {
        const float wt = cubic(sx - float(x0 + k));
        const int xx = std::clamp(x0 + k, 0, w - 1);
        for (int c = 0; c < 4; ++c) acc[c] += wt * src[(size_t(y) * w + xx) * 4 + c];
        wsum += wt;
      }
      for (int c = 0; c < 4; ++c) mid[(size_t(y) * W + X) * 4 + c] = acc[c] / wsum;
    }
  }
  for (int Y = 0; Y < H; ++Y) {
    const float sy = (Y + 0.5f) / 4 - 0.5f;
    const int y0 = int(std::floor(sy));
    for (int X = 0; X < W; ++X) {
      float acc[4] = {}, wsum = 0;
      for (int k = -1; k <= 2; ++k) {
        const float wt = cubic(sy - float(y0 + k));
        const int yy = std::clamp(y0 + k, 0, h - 1);
        for (int c = 0; c < 4; ++c) acc[c] += wt * mid[(size_t(yy) * W + X) * 4 + c];
        wsum += wt;
      }
      for (int c = 0; c < 4; ++c) big[(size_t(Y) * W + X) * 4 + c] = std::clamp(acc[c] / wsum, 0.0f, 1.0f);
    }
  }
  // A small blur (sigma 2 px at the new size) before the edges are made
  // crisp again: the source's block-compression steps along each outline
  // become smooth curves instead of a wobble.
  {
    constexpr float kSigma = 2.0f;
    constexpr int kR = 7;
    float kernel[2 * kR + 1], ksum = 0;
    for (int i = -kR; i <= kR; ++i) ksum += kernel[i + kR] = std::exp(-0.5f * (i / kSigma) * (i / kSigma));
    for (float& v : kernel) v /= ksum;
    std::vector<float> tmp(big.size());
    for (int Y = 0; Y < H; ++Y) {
      for (int X = 0; X < W; ++X) {
        float acc[4] = {};
        for (int i = -kR; i <= kR; ++i) {
          const int xx = std::clamp(X + i, 0, W - 1);
          for (int c = 0; c < 4; ++c) acc[c] += kernel[i + kR] * big[(size_t(Y) * W + xx) * 4 + c];
        }
        for (int c = 0; c < 4; ++c) tmp[(size_t(Y) * W + X) * 4 + c] = acc[c];
      }
    }
    for (int Y = 0; Y < H; ++Y) {
      for (int X = 0; X < W; ++X) {
        float acc[4] = {};
        for (int i = -kR; i <= kR; ++i) {
          const int yy = std::clamp(Y + i, 0, H - 1);
          for (int c = 0; c < 4; ++c) acc[c] += kernel[i + kR] * tmp[(size_t(yy) * W + X) * 4 + c];
        }
        for (int c = 0; c < 4; ++c) big[(size_t(Y) * W + X) * 4 + c] = acc[c];
      }
    }
  }
  // The button sheet's flat colours (black, white, Y, A, X, B).
  static const float kPalette[][3] = {{0, 0, 0},
                                      {1, 1, 1},
                                      {1, 220 / 255.0f, 0},
                                      {109 / 255.0f, 142 / 255.0f, 0},
                                      {0, 151 / 255.0f, 198 / 255.0f},
                                      {1, 0, 0}};
  // What each is drawn as: the face buttons brighter and more vivid than the
  // sheet's olive A and teal X (the user: "dull"), in the Xbox 360's hues.
  static const float kPaletteOut[][3] = {{0, 0, 0},
                                         {1, 1, 1},
                                         {1, 215 / 255.0f, 0},
                                         {100 / 255.0f, 200 / 255.0f, 30 / 255.0f},
                                         {30 / 255.0f, 140 / 255.0f, 1},
                                         {1, 50 / 255.0f, 40 / 255.0f}};
  auto ramp = [](float x) {
    const float t = std::clamp((x - 0.5f) / 0.25f + 0.5f, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
  };
  Image out;
  out.width = W;
  out.height = H;
  out.rgba.resize(size_t(W) * H * 4);
  for (size_t i = 0; i < size_t(W) * H; ++i) {
    const float a = big[i * 4 + 3];
    float rgb[3];
    for (int c = 0; c < 3; ++c) rgb[c] = a > 1e-4f ? std::min(1.0f, big[i * 4 + c] / a) : 0.0f;
    // The two nearest flat colours.
    int n0 = 0, n1 = 1;
    float d0 = 1e9f, d1 = 1e9f;
    for (int k = 0; k < 6; ++k) {
      float d = 0;
      for (int c = 0; c < 3; ++c) d += (rgb[c] - kPalette[k][c]) * (rgb[c] - kPalette[k][c]);
      if (d < d0) {
        d1 = d0, n1 = n0;
        d0 = d, n0 = k;
      } else if (d < d1) {
        d1 = d, n1 = k;
      }
    }
    // Close to the palette: a crisp edge between the two; else (gradients,
    // other art) the smooth colour.
    if (d0 < 0.25f * 0.25f) {
      float v[3], vv = 0, proj = 0;
      for (int c = 0; c < 3; ++c) {
        v[c] = kPalette[n1][c] - kPalette[n0][c];
        vv += v[c] * v[c];
        proj += (rgb[c] - kPalette[n0][c]) * v[c];
      }
      const float t = ramp(std::clamp(vv > 1e-6f ? proj / vv : 0.0f, 0.0f, 1.0f));
      for (int c = 0; c < 3; ++c) rgb[c] = kPaletteOut[n0][c] + (kPaletteOut[n1][c] - kPaletteOut[n0][c]) * t;
    }
    for (int c = 0; c < 3; ++c) out.rgba[i * 4 + c] = uint8_t(std::lround(rgb[c] * 255));
    out.rgba[i * 4 + 3] = uint8_t(std::lround(ramp(a) * 255));
  }
  return out;
}

// The fonts' button glyphs (BronzeBigFont_30 and NewAgency_18 carry A, B, X,
// Y, Back, Start, the guide button, the D-pad, the sticks, bumpers and
// triggers as tiny glossy glyphs the upscaler can't make clean): replaced in
// the 4x font by the same buttons from the game's own button sheet
// (Xbox360buttons, enlarged by EnlargeIcon), fitted to each glyph's box.
struct Box {
  int x0, y0, x1, y1;  // inclusive, in the original's pixels
};
enum Button { kA, kB, kX, kY, kBack, kStart, kGuide, kDpad, kL, kR, kLB, kRB, kLT, kRT, kButtons };
// Xbox360buttons (512x512).
constexpr Box kSheet[kButtons] = {
    {140, 394, 248, 501}, {385, 394, 493, 501}, {261, 394, 369, 501}, {16, 394, 124, 501}, {261, 280, 380, 375},
    {389, 280, 508, 376}, {17, 269, 125, 377},  {136, 269, 244, 377}, {46, 20, 153, 127},  {45, 148, 152, 255},
    {182, 9, 327, 112},   {342, 9, 487, 112},   {342, 120, 461, 264}, {209, 120, 329, 265}};
// BronzeBigFont_30 (512x512) and NewAgency_18 (256x512).
constexpr Box kBronze[kButtons] = {
    {22, 6, 54, 37},    {58, 6, 89, 37},    {92, 6, 124, 37},   {127, 6, 158, 37},  {161, 8, 196, 36},
    {199, 8, 233, 36},  {236, 6, 267, 38},  {271, 6, 303, 38},  {306, 6, 338, 37},  {341, 6, 373, 37},
    {376, 6, 418, 36},  {421, 6, 464, 36},  {467, 1, 502, 43},  {0, 47, 36, 89}};
constexpr Box kNewAgency[kButtons] = {
    {15, 8, 36, 29},    {39, 8, 60, 29},    {63, 8, 83, 29},    {86, 8, 107, 29},   {110, 9, 133, 28},
    {136, 9, 159, 28},  {162, 8, 182, 29},  {186, 8, 206, 29},  {209, 8, 229, 29},  {232, 8, 253, 29},
    {0, 44, 28, 64},    {31, 44, 59, 64},   {62, 40, 85, 69},   {88, 40, 111, 69}};

// font: a 4x font sheet (original w x h times 4); sheet: the 4x button sheet.
void PasteButtons(Image& font, const Image& sheet, const Box* glyphs) {
  constexpr int k = 4;
  for (int b = 0; b < kButtons; ++b) {
    const Box& g = glyphs[b];
    const Box& s = kSheet[b];
    // The glyph's box (and a pixel around it, the glossy rim's soft edge)
    // cleared.
    const int cx0 = std::max(0, (g.x0 - 1) * k), cy0 = std::max(0, (g.y0 - 1) * k);
    const int cx1 = std::min(font.width, (g.x1 + 2) * k), cy1 = std::min(font.height, (g.y1 + 2) * k);
    for (int y = cy0; y < cy1; ++y) std::memset(&font.rgba[(size_t(y) * font.width + cx0) * 4], 0, size_t(cx1 - cx0) * 4);
    // The sheet's button fitted into the glyph's box (its shape kept),
    // centred, by area averaging on premultiplied colour.
    const float sw = float((s.x1 - s.x0 + 1) * k), sh = float((s.y1 - s.y0 + 1) * k);
    const float gw = float((g.x1 - g.x0 + 1) * k), gh = float((g.y1 - g.y0 + 1) * k);
    const float f = std::min(gw / sw, gh / sh);
    const int dw = std::max(1, int(std::lround(sw * f))), dh = std::max(1, int(std::lround(sh * f)));
    const int dx = g.x0 * k + int(std::lround((gw - dw) / 2)), dy = g.y0 * k + int(std::lround((gh - dh) / 2));
    for (int y = 0; y < dh; ++y) {
      const int sy0 = s.y0 * k + int(y / f), sy1 = std::max(sy0 + 1, s.y0 * k + int((y + 1) / f));
      for (int x = 0; x < dw; ++x) {
        const int sx0 = s.x0 * k + int(x / f), sx1 = std::max(sx0 + 1, s.x0 * k + int((x + 1) / f));
        float acc[4] = {};
        int n = 0;
        for (int yy = sy0; yy < std::min(sy1, sheet.height); ++yy) {
          for (int xx = sx0; xx < std::min(sx1, sheet.width); ++xx) {
            const uint8_t* p = &sheet.rgba[(size_t(yy) * sheet.width + xx) * 4];
            const float a = p[3] / 255.0f;
            for (int c = 0; c < 3; ++c) acc[c] += p[c] / 255.0f * a;
            acc[3] += a;
            ++n;
          }
        }
        const int ox = dx + x, oy = dy + y;
        if (!n || ox < 0 || oy < 0 || ox >= font.width || oy >= font.height) continue;
        uint8_t* o = &font.rgba[(size_t(oy) * font.width + ox) * 4];
        const float a = acc[3] / n;
        for (int c = 0; c < 3; ++c) o[c] = uint8_t(std::lround(a > 1e-4f ? std::min(1.0f, acc[c] / acc[3]) * 255 : 0.0f));
        o[3] = uint8_t(std::lround(a * 255));
      }
    }
  }
}

}  // namespace

Image Half(const Image& in) {
  Image out;
  out.width = std::max(1, in.width / 2);
  out.height = std::max(1, in.height / 2);
  out.rgba.resize(size_t(out.width) * out.height * 4);
  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < out.width; ++x) {
      // Colour weighted by alpha, so transparent pixels' colour doesn't
      // darken the edges.
      uint32_t sum[3] = {}, alpha = 0;
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const int sx = std::min(x * 2 + dx, in.width - 1), sy = std::min(y * 2 + dy, in.height - 1);
          const uint8_t* p = &in.rgba[(size_t(sy) * in.width + sx) * 4];
          for (int c = 0; c < 3; ++c) sum[c] += uint32_t(p[c]) * p[3];
          alpha += p[3];
        }
      }
      uint8_t* o = &out.rgba[(size_t(y) * out.width + x) * 4];
      for (int c = 0; c < 3; ++c) o[c] = uint8_t(alpha ? sum[c] / alpha : 0);
      o[3] = uint8_t(alpha / 4);
    }
  }
  return out;
}

uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t width, uint32_t bpb) {
  const uint32_t aligned = (width + 31) & ~31u;
  const uint32_t log_bpp = (bpb >> 2) + ((bpb >> 1) >> (bpb >> 2));
  const uint32_t macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7);
  const uint32_t micro = ((x & 7) + ((y & 6) << 2)) << log_bpp;
  const uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4);
  return (((offset & ~511u) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
          (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >>
         log_bpp;
}

std::string TextureKey(const uint8_t* xpr, size_t size) {
  Header h;
  Format f;
  if (!ReadHeader(xpr, size, h) || !FormatOf(h.fmt, f)) return {};
  const uint32_t bw = (h.width + f.block - 1) / f.block, bh = (h.height + f.block - 1) / f.block;
  const size_t bytes = size_t((bw + 31) & ~31u) * ((bh + 31) & ~31u) * f.bytes_per_block;
  if (h.data_offset + bytes > size) return {};
  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx",
                static_cast<unsigned long long>(XXH3_64bits(xpr + h.data_offset, bytes)));
  return hex;
}

bool DecodeXpr(const uint8_t* xpr, size_t size, Image& out, std::string& error) {
  Header h;
  Format f;
  if (!ReadHeader(xpr, size, h)) {
    error = "not an XPR2 or XTX texture";
    return false;
  }
  if (!FormatOf(h.fmt, f)) {
    error = "format " + std::to_string(h.fmt);
    return false;
  }
  std::vector<uint8_t> data(xpr + h.data_offset, xpr + size);
  Swap(data, h.endian);
  const uint32_t bpb = uint32_t(f.bytes_per_block);
  const uint32_t bw = (h.width + f.block - 1) / f.block, bh = (h.height + f.block - 1) / f.block;
  const uint32_t pw = std::max(h.pitch / f.block, bw);
  // Untiled blocks, row by row.
  std::vector<uint8_t> blocks(size_t(bw) * bh * bpb);
  for (uint32_t y = 0; y < bh; ++y) {
    for (uint32_t x = 0; x < bw; ++x) {
      const size_t src = size_t(h.tiled ? TiledOffset(x, y, pw, bpb) : y * pw + x) * bpb;
      if (src + bpb <= data.size()) std::memcpy(&blocks[(size_t(y) * bw + x) * bpb], &data[src], bpb);
    }
  }
  const int iw = int(bw * f.block), ih = int(bh * f.block);
  std::vector<uint8_t> rgba(size_t(iw) * ih * 4, 255);
  for (uint32_t y = 0; y < bh; ++y) {
    for (uint32_t x = 0; x < bw; ++x) {
      const uint8_t* b = &blocks[(size_t(y) * bw + x) * bpb];
      uint8_t* o = &rgba[(size_t(y) * f.block * iw + size_t(x) * f.block) * 4];
      switch (h.fmt) {
        case 18: ColourBlock(b, o, iw, true); break;
        case 19: ColourBlock(b + 8, o, iw, false), ExplicitAlpha(b, o, iw); break;
        case 20: ColourBlock(b + 8, o, iw, false), InterpolatedAlpha(b, o, iw); break;
        case 6:
          // Amped's 8888 UI textures (grey art) keep alpha where ARGB order
          // puts blue: matched against the runtime's own dump of RadialTubes.
          o[0] = b[1], o[1] = b[2], o[2] = b[0], o[3] = b[3];
          break;
        case 15: {
          const uint16_t p = uint16_t(b[0] | b[1] << 8);
          o[0] = uint8_t(((p >> 8) & 15) * 17), o[1] = uint8_t(((p >> 4) & 15) * 17), o[2] = uint8_t((p & 15) * 17);
          o[3] = uint8_t((p >> 12) * 17);
          break;
        }
        case 4: Rgb565(uint16_t(b[0] | b[1] << 8), o); break;
        case 10: o[0] = o[1] = o[2] = b[0], o[3] = b[1]; break;
        default: o[0] = o[1] = o[2] = b[0]; break;
      }
    }
  }
  // Cropped to the texture's size.
  out.width = int(h.width);
  out.height = int(h.height);
  out.rgba.resize(size_t(out.width) * out.height * 4);
  for (int y = 0; y < out.height; ++y) {
    std::memcpy(&out.rgba[size_t(y) * out.width * 4], &rgba[size_t(y) * iw * 4], size_t(out.width) * 4);
  }
  return true;
}

std::string StampText(int scale, const std::vector<std::string>& lines) {
  std::string text = "ReAmped 3 sharp UI pack " + std::to_string(kVersion) + " x" + std::to_string(scale) + "\n";
  for (const std::string& l : lines) text += l + "\n";
  return text;
}

bool UpscalerPresent(const std::string& esrgan_dir) {
  if (!sys::FileExists(launcher::JoinPath(esrgan_dir, "realesrgan-ncnn-vulkan.exe"))) return false;
  for (const char* model : {"realesrgan-x4plus-anime", "realesrgan-x4plus"}) {
    if (!sys::FileExists(launcher::JoinPath(launcher::JoinPath(esrgan_dir, "models"), std::string(model) + ".bin"))) {
      return false;
    }
  }
  return true;
}

bool UpToDate(const Options& o) {
  // Made from these game files (names and sizes: copying the game elsewhere
  // changes its files' times, not their contents), at this scale, by any
  // version since kMinVersion.
  std::string stamp;
  if (!sys::DirExists(launcher::JoinPath(o.textures_dir, "replace")) ||
      !sys::ReadFile(launcher::JoinPath(o.textures_dir, "pack.txt"), stamp)) {
    return false;
  }
  auto without_times = [](const std::string& text) {
    // "ReAmped 3 sharp UI pack N xS" (version dropped), then "name size time" lines (time dropped).
    std::string out;
    size_t pos = 0;
    for (bool first = true; pos < text.size(); first = false) {
      size_t end = text.find('\n', pos);
      if (end == std::string::npos) end = text.size();
      std::string line = text.substr(pos, end - pos);
      pos = end + 1;
      if (first) {
        const size_t x = line.rfind(" x");
        out += x == std::string::npos ? line : line.substr(x);
      } else {
        const size_t last = line.rfind(' ');
        out += last == std::string::npos ? line : line.substr(0, last);
      }
      out += '\n';
    }
    return out;
  };
  const std::string prefix = "ReAmped 3 sharp UI pack ";
  if (stamp.compare(0, prefix.size(), prefix) != 0 || std::atoi(stamp.c_str() + prefix.size()) < kMinVersion) {
    return false;
  }
  return without_times(stamp) == without_times(StampText(o.scale, StampLines(o.game_dir)));
}

bool Build(const Options& o, Progress& progress, const std::atomic<bool>& cancel, std::string& error) {
  using launcher::JoinPath;
  const std::string work = JoinPath(o.textures_dir, "work");
  const std::string in[kKinds] = {JoinPath(work, "in_art"), JoinPath(work, "in_photo"), JoinPath(work, "in_screen"),
                                  JoinPath(work, "in_icon"), JoinPath(work, "in_pixel")};
  const std::string done[kKinds] = {JoinPath(work, "out_art"), JoinPath(work, "out_photo"),
                                    JoinPath(work, "out_screen"), JoinPath(work, "out_icon"),
                                    JoinPath(work, "out_pixel")};
  sys::MakeDir(o.textures_dir);
  // The upscaled images in work\ carry on a cancelled build, but only for
  // the same game files: other files (another copy of the game) start over.
  const std::string work_stamp = JoinPath(work, "stamp.txt");
  const std::string stamp = StampText(o.scale, StampLines(o.game_dir));  // the scale picks the models
  std::string old_stamp;
  if (!sys::ReadFile(work_stamp, old_stamp) || old_stamp != stamp) sys::RemoveTree(work);
  sys::MakeDir(work);
  sys::WriteFile(work_stamp, stamp);
  for (const std::string& d : in) {
    sys::RemoveTree(d);
    sys::MakeDir(d);
  }
  for (const std::string& d : done) sys::MakeDir(d);

  // 1. Each texture once: its key, and the original as PNG unless the
  // upscaler already did it (a cancelled build carries on).
  progress.stage = int(Stage::kReading);
  struct Entry {
    std::string key;
    Kind kind;
  };
  std::map<std::string, Entry> entries;  // name -> key, kind
  int waiting[kKinds] = {};
  for (const Source& src : Sources(o.game_dir)) {
    std::string zip;
    if (!sys::ReadFile(src.path, zip)) continue;
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, zip.data(), zip.size(), 0)) continue;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive); ++i) {
      if (cancel) break;
      char entry[512];
      mz_zip_reader_get_filename(&archive, i, entry, sizeof(entry));
      if (!EndsWithNoCase(entry, src.screens ? ".xtx" : ".xpr")) continue;
      const std::string name = BaseName(entry);
      if (entries.count(name)) continue;
      size_t size = 0;
      auto* xpr = static_cast<uint8_t*>(mz_zip_reader_extract_to_heap(&archive, i, &size, 0));
      if (!xpr) continue;
      const std::string key = TextureKey(xpr, size);
      Image image;
      std::string decode_error;
      const Kind kind = KindOf(name, src.screens);
      if (!key.empty() && DecodeXpr(xpr, size, image, decode_error)) {
        entries[name] = {key, kind};
        if (kind == kIcon || kind == kPixel) {
          sys::MakeDir(done[kind]);
          std::string png_error;
          Png(kind == kIcon ? EnlargeIcon(image) : EnlargePixels(image), JoinPath(done[kind], name + ".png"), png_error);
        } else if (!sys::FileExists(JoinPath(done[kind], name + ".png"))) {
          std::string png_error;
          Png(image, JoinPath(in[kind], name + ".png"), png_error);
          if (png_error.empty()) ++waiting[kind];
        }
      }
      mz_free(xpr);
    }
    mz_zip_reader_end(&archive);
  }
  if (cancel) {
    error = "cancelled";
    return false;
  }
  if (entries.empty()) {
    error = "no UI textures found in GuiPackages";
    return false;
  }
  progress.total = int(entries.size());
  progress.done = int(entries.size()) - waiting[kArt] - waiting[kPhoto] - waiting[kScreen];

  // 2. The upscaler, once per kind, over its folder (it needs the output
  // folder to exist). Progress: the files it has written so far.
  const std::string exe = JoinPath(o.esrgan_dir, "realesrgan-ncnn-vulkan.exe");
  for (int k = 0; k < kKinds; ++k) {
    const Kind kind = Kind(k);
    if (!waiting[kind]) continue;
    progress.stage = int(kind == kArt ? Stage::kUpscalingArt : Stage::kUpscalingPhotos);
    std::vector<std::string> already;
    sys::ListFiles(done[kind], already);
    const int before = progress.done, start_files = int(already.size());
    const std::string command = Quote(exe) + " -i " + Quote(in[kind]) + " -o " + Quote(done[kind]) + " -n " +
                                ModelFor(kind, o.scale) + " -f png";
    int exit_code = 0;
    std::string run_error;
    const bool ran = sys::RunHidden(
        exe, command, o.esrgan_dir,
        [&] {
          std::vector<std::string> files;
          sys::ListFiles(done[kind], files);
          progress.done = before + std::max(0, int(files.size()) - start_files);
          return !cancel.load();
        },
        exit_code, run_error);
    if (!ran || exit_code != 0) {
      error = cancel ? "cancelled"
                     : (!ran ? run_error : "the upscaler failed (code " + std::to_string(exit_code) + "; it needs a Vulkan GPU)");
      return false;
    }
  }

  // 3. replace.new\<key>.png / .jpg, then renamed to replace\ in one step.
  progress.stage = int(Stage::kFinishing);
  const std::string fresh = JoinPath(o.textures_dir, "replace.new");
  sys::RemoveTree(fresh);
  if (!sys::MakeDir(fresh)) {
    error = "can't create " + fresh;
    return false;
  }
  int copied = 0;
  for (const auto& [name, e] : entries) {
    // The upscaler writes PNG (its -f jpg names files .jpg.png and writes
    // PNG): the loading screens are made JPEG here.
    const std::string src = JoinPath(done[e.kind], name + ".png");
    const std::string dst = JoinPath(fresh, e.key + ExtensionFor(e.kind));
    // The loading screens are kept at 2x on every machine (~0.8 GB decoded,
    // not 3 GB: they're only up while loading); the rest at the pack's scale.
    const bool halve = o.scale == 2 || e.kind == kScreen;
    // The fonts' button glyphs: the game's button sheet's, clean.
    const Box* buttons = name == "BronzeBigFont_30" ? kBronze : name == "NewAgency_18" ? kNewAgency : nullptr;
    if (buttons) {
      Image font, sheet;
      int channels = 0;
      const std::string sheet_png = JoinPath(done[kIcon], "Xbox360buttons.png");
      uint8_t* f = stbi_load(src.c_str(), &font.width, &font.height, &channels, 4);
      uint8_t* b = stbi_load(sheet_png.c_str(), &sheet.width, &sheet.height, &channels, 4);
      // Only for the sizes the tables are for (the game's own files at 4x).
      if (f && b && sheet.width == 2048 && sheet.height == 2048 && font.height == 2048 &&
          font.width == (buttons == kBronze ? 2048 : 1024)) {
        font.rgba.assign(f, f + size_t(font.width) * font.height * 4);
        sheet.rgba.assign(b, b + size_t(sheet.width) * sheet.height * 4);
        PasteButtons(font, sheet, buttons);
        std::string write_error;
        Png(halve ? Half(font) : font, dst, write_error);
        if (write_error.empty()) {
          ++copied;
          stbi_image_free(f);
          stbi_image_free(b);
          continue;
        }
      }
      if (f) stbi_image_free(f);
      if (b) stbi_image_free(b);
    }
    if (!halve) {
      std::string data;
      if (sys::ReadFile(src, data) && sys::WriteFile(dst, data)) ++copied;
      continue;
    }
    Image big;
    int channels = 0;
    if (uint8_t* pixels = stbi_load(src.c_str(), &big.width, &big.height, &channels, 4)) {
      big.rgba.assign(pixels, pixels + size_t(big.width) * big.height * 4);
      stbi_image_free(pixels);
      const Image& out = halve ? Half(big) : big;
      std::string write_error;
      if (e.kind == kScreen) {
        Jpg(out, dst, write_error);
      } else {
        Png(out, dst, write_error);
      }
      if (write_error.empty()) ++copied;
    }
  }
  if (copied < int(entries.size()) * 9 / 10) {
    error = "only " + std::to_string(copied) + " of " + std::to_string(entries.size()) + " textures were enlarged";
    sys::RemoveTree(fresh);
    return false;
  }
  // Swapped in: the old pack moved aside first and back if the new one can't
  // take its place (never left without one), then removed.
  const std::string current = JoinPath(o.textures_dir, "replace");
  const std::string old = JoinPath(o.textures_dir, "replace.old");
  sys::RemoveTree(old);
  const bool had_current = sys::DirExists(current);
  if (had_current && !sys::RenameDir(current, old)) {
    error = "can't move the old pack aside (" + current + " in use?)";
    sys::RemoveTree(fresh);
    return false;
  }
  if (!sys::RenameDir(fresh, current)) {
    if (had_current) sys::RenameDir(old, current);
    error = "can't rename " + fresh;
    return false;
  }
  sys::RemoveTree(old);
  sys::WriteFile(JoinPath(o.textures_dir, "pack.txt"), StampText(o.scale, StampLines(o.game_dir)));
  // work\ only carries a cancelled build on (~1 GB of 4x images): a new pack
  // comes from other game files or another scale, which start over anyway.
  sys::RemoveTree(work);
  progress.done = progress.total.load();
  return true;
}

bool WriteLauncherLogo(const Options& o, const std::string& out_png) {
  // AmpedBoxLogo.xpr from GuiPackages\Shell.ruirr (the title screen's logo).
  std::string shell;
  for (const std::string& f : Packages(o.game_dir)) {
    if (EndsWithNoCase(f, "shell.ruirr") && f.size() == 11) shell = launcher::JoinPath(PackagesDir(o.game_dir), f);
  }
  std::string zip;
  if (shell.empty() || !sys::ReadFile(shell, zip)) return false;
  mz_zip_archive archive{};
  if (!mz_zip_reader_init_mem(&archive, zip.data(), zip.size(), 0)) return false;
  std::vector<uint8_t> xpr;
  for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive) && xpr.empty(); ++i) {
    char entry[512];
    mz_zip_reader_get_filename(&archive, i, entry, sizeof(entry));
    if (!EndsWithNoCase(entry, "ampedboxlogo.xpr")) continue;
    size_t size = 0;
    if (auto* data = static_cast<uint8_t*>(mz_zip_reader_extract_to_heap(&archive, i, &size, 0))) {
      xpr.assign(data, data + size);
      mz_free(data);
    }
  }
  mz_zip_reader_end(&archive);
  if (xpr.empty()) return false;
  const auto u8 = [](const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); };
  return amped3::title_logo::WriteLogoPng(xpr, u8(launcher::JoinPath(o.textures_dir, "replace")), u8(out_png));
}

}  // namespace uipack
