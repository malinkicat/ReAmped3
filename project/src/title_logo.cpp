// title_logo.cpp - The title screen's logo as REAMPED3, made at start from the
// player's own AMPED3 logo (Shell.ruirr Textures/AmpedBoxLogo.xpr): the
// original, smaller and moved right, with "RE" drawn in front in the same
// slanted blocky letters, black plate and magenta swoosh. No game art ships.
//
// Port of tools/art/reamped_title.py (the design; see its notes) and
// tools/art/xpr_texture.py (XPR2: one TX2D, DXT3, Xenos-tiled, 8-in-16
// endian). The letters are vector shapes drawn 8x oversampled, so their edges
// are as clean as the original's.

#include "title_logo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "../thirdparty/stb/stb_image.h"  // its implementation is in ui_patch.cpp (PNG only)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "../thirdparty/stb/stb_image_write.h"
#define XXH_INLINE_ALL
#include "../thirdparty/xxhash/xxhash.h"

namespace amped3::title_logo {
namespace {

using Bytes = std::vector<uint8_t>;

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

struct Texture {
  uint32_t width, height, pixels_at, data_size;
  bool tiled;
  uint32_t endian;
};

// The XPR2's one texture, if it's a DXT3 one.
bool Parse(const Bytes& data, Texture& t) {
  if (data.size() < 32 || std::memcmp(data.data(), "XPR2", 4)) return false;
  const uint32_t header_size = Be32(&data[4]), data_size = Be32(&data[8]), count = Be32(&data[12]);
  if (count != 1 || std::memcmp(&data[16], "TX2D", 4)) return false;
  const size_t tex = 12 + size_t(Be32(&data[20]));
  if (tex + 40 > data.size()) return false;
  uint32_t fetch[6];
  for (int i = 0; i < 6; ++i) fetch[i] = Be32(&data[tex + 16 + 4 * i]);
  if ((fetch[1] & 0x3F) != 0x13) return false;  // DXT3
  t.endian = (fetch[1] >> 6) & 3;
  t.width = (fetch[2] & 0x1FFF) + 1;
  t.height = ((fetch[2] >> 13) & 0x1FFF) + 1;
  t.tiled = fetch[0] & 0x80000000u;
  t.pixels_at = 12 + header_size;
  t.data_size = data_size;
  return t.width % 4 == 0 && t.height % 4 == 0 && t.pixels_at + size_t(data_size) <= data.size() &&
         size_t(t.width / 4) * (t.height / 4) * 16 <= data_size;
}

// Xenos 2D tiling: block (x, y) of a surface `aligned` blocks wide -> index.
uint32_t TiledBlock(uint32_t x, uint32_t y, uint32_t aligned, uint32_t log_bpb) {
  const uint32_t macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpb + 7);
  const uint32_t micro = ((x & 7) + ((y & 6) << 2)) << log_bpb;
  const uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpb)) + ((y & 1) << 4);
  return (((offset & ~511u) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
          (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >>
         log_bpb;
}

// Block (x, y)'s bytes in the texture data.
uint8_t* BlockAt(Bytes& data, const Texture& t, uint32_t x, uint32_t y) {
  const uint32_t bw = t.width / 4, aligned = (bw + 31) & ~31u;
  const uint32_t i = t.tiled ? TiledBlock(x, y, aligned, 4) : y * bw + x;
  return &data[t.pixels_at + size_t(i) * 16];
}

void SwapEndian(uint8_t* block, uint32_t endian) {
  if (endian == 1) {  // 8-in-16
    for (int i = 0; i < 16; i += 2) std::swap(block[i], block[i + 1]);
  }
}

struct Rgba {
  float r = 0, g = 0, b = 0, a = 0;  // 0..255, straight alpha
};

struct Image {
  uint32_t w = 0, h = 0;
  std::vector<Rgba> px;
  Rgba& at(uint32_t x, uint32_t y) { return px[size_t(y) * w + x]; }
  const Rgba& at(uint32_t x, uint32_t y) const { return px[size_t(y) * w + x]; }
};

std::array<float, 3> From565(uint16_t v) {
  return {float((v >> 11) & 31) * 255.0f / 31.0f, float((v >> 5) & 63) * 255.0f / 63.0f,
          float(v & 31) * 255.0f / 31.0f};
}

Image Decode(Bytes& data, const Texture& t) {
  Image img{t.width, t.height, std::vector<Rgba>(size_t(t.width) * t.height)};
  for (uint32_t by = 0; by < t.height / 4; ++by) {
    for (uint32_t bx = 0; bx < t.width / 4; ++bx) {
      uint8_t block[16];
      std::memcpy(block, BlockAt(data, t, bx, by), 16);
      SwapEndian(block, t.endian);
      const uint16_t c0 = uint16_t(block[8] | block[9] << 8), c1 = uint16_t(block[10] | block[11] << 8);
      const auto p0 = From565(c0), p1 = From565(c1);
      std::array<std::array<float, 3>, 4> pal = {p0, p1};
      for (int c = 0; c < 3; ++c) {
        pal[2][c] = (2 * p0[c] + p1[c]) / 3;
        pal[3][c] = (p0[c] + 2 * p1[c]) / 3;
      }
      const uint32_t bits = uint32_t(block[12]) | uint32_t(block[13]) << 8 | uint32_t(block[14]) << 16 |
                            uint32_t(block[15]) << 24;
      for (int i = 0; i < 16; ++i) {
        const auto& c = pal[(bits >> (2 * i)) & 3];
        const int alpha = (block[i / 2] >> (4 * (i & 1))) & 15;
        img.at(bx * 4 + i % 4, by * 4 + i / 4) = {c[0], c[1], c[2], float(alpha * 17)};
      }
    }
  }
  return img;
}

uint16_t To565(const std::array<float, 3>& c) {
  auto q = [](float v) { return int(std::clamp(std::round(v), 0.0f, 255.0f)); };
  return uint16_t((q(c[0]) >> 3) << 11 | (q(c[1]) >> 2) << 5 | q(c[2]) >> 3);
}

// Explicit 4-bit alpha; colour endpoints from the block's extremes along its
// main axis (alpha-weighted), 4-colour mode.
void Encode(const Image& img, Bytes& data, const Texture& t) {
  for (uint32_t by = 0; by < t.height / 4; ++by) {
    for (uint32_t bx = 0; bx < t.width / 4; ++bx) {
      uint8_t block[16] = {};
      std::array<std::array<float, 3>, 16> rgb;
      float weight[16], wsum = 0, mean[3] = {};
      for (int i = 0; i < 16; ++i) {
        const Rgba& p = img.at(bx * 4 + i % 4, by * 4 + i / 4);
        const int alpha = std::clamp(int(std::round(p.a / 17.0f)), 0, 15);
        block[i / 2] |= uint8_t(alpha << (4 * (i & 1)));
        rgb[i] = {p.r, p.g, p.b};
        weight[i] = p.a / 255.0f + 1e-3f;
        wsum += weight[i];
        for (int c = 0; c < 3; ++c) mean[c] += rgb[i][c] * weight[i];
      }
      for (float& m : mean) m /= wsum;
      float cov[3][3] = {};
      for (int i = 0; i < 16; ++i) {
        for (int a = 0; a < 3; ++a) {
          for (int b = 0; b < 3; ++b) cov[a][b] += (rgb[i][a] - mean[a]) * weight[i] * (rgb[i][b] - mean[b]);
        }
      }
      // The main axis by power iteration.
      float axis[3] = {0.577f, 0.577f, 0.577f};
      for (int iteration = 0; iteration < 16; ++iteration) {
        float next[3] = {};
        for (int a = 0; a < 3; ++a) {
          for (int b = 0; b < 3; ++b) next[a] += cov[a][b] * axis[b];
        }
        const float length = std::sqrt(next[0] * next[0] + next[1] * next[1] + next[2] * next[2]);
        if (length < 1e-6f) break;
        for (int a = 0; a < 3; ++a) axis[a] = next[a] / length;
      }
      float lo = 1e30f, hi = -1e30f;
      for (int i = 0; i < 16; ++i) {
        const float proj = (rgb[i][0] - mean[0]) * axis[0] + (rgb[i][1] - mean[1]) * axis[1] +
                           (rgb[i][2] - mean[2]) * axis[2];
        lo = std::min(lo, proj);
        hi = std::max(hi, proj);
      }
      uint16_t c0 = To565({mean[0] + axis[0] * hi, mean[1] + axis[1] * hi, mean[2] + axis[2] * hi});
      uint16_t c1 = To565({mean[0] + axis[0] * lo, mean[1] + axis[1] * lo, mean[2] + axis[2] * lo});
      if (c0 < c1) std::swap(c0, c1);
      const auto p0 = From565(c0), p1 = From565(c1);
      std::array<std::array<float, 3>, 4> pal = {p0, p1};
      for (int c = 0; c < 3; ++c) {
        pal[2][c] = (2 * p0[c] + p1[c]) / 3;
        pal[3][c] = (p0[c] + 2 * p1[c]) / 3;
      }
      uint32_t bits = 0;
      for (int i = 0; i < 16 && c0 != c1; ++i) {
        int best = 0;
        float best_d = 1e30f;
        for (int k = 0; k < 4; ++k) {
          float d = 0;
          for (int c = 0; c < 3; ++c) d += (rgb[i][c] - pal[k][c]) * (rgb[i][c] - pal[k][c]);
          if (d < best_d) best_d = d, best = k;
        }
        bits |= uint32_t(best) << (2 * i);
      }
      block[8] = uint8_t(c0), block[9] = uint8_t(c0 >> 8), block[10] = uint8_t(c1), block[11] = uint8_t(c1 >> 8);
      block[12] = uint8_t(bits), block[13] = uint8_t(bits >> 8), block[14] = uint8_t(bits >> 16);
      block[15] = uint8_t(bits >> 24);
      SwapEndian(block, t.endian);
      std::memcpy(BlockAt(data, t, bx, by), block, 16);
    }
  }
}

// --- Drawing masks (0..1, 8x oversampled)

// Supersampling: 8x at the texture's size; 2x for the sharp UI pack's 4x
// version (its masks would be 16 times as big).
constexpr int kSs = 8, kSsSharp = 2;

struct Mask {
  int w, h;
  std::vector<uint8_t> v;
  Mask(int w_, int h_) : w(w_), h(h_), v(size_t(w_) * h_) {}
};

struct Point {
  double x, y;
};

// Even-odd fill, sampled at pixel centres.
void FillPolygon(Mask& m, const std::vector<Point>& pts, uint8_t value) {
  std::vector<double> xs;
  for (int y = 0; y < m.h; ++y) {
    const double sy = y + 0.5;
    xs.clear();
    for (size_t i = 0; i < pts.size(); ++i) {
      const Point& a = pts[i];
      const Point& b = pts[(i + 1) % pts.size()];
      if ((a.y <= sy) != (b.y <= sy)) xs.push_back(a.x + (sy - a.y) / (b.y - a.y) * (b.x - a.x));
    }
    std::sort(xs.begin(), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i += 2) {
      const int x0 = std::max(0, int(std::ceil(xs[i] - 0.5))), x1 = std::min(m.w, int(std::ceil(xs[i + 1] - 0.5)));
      for (int x = x0; x < x1; ++x) m.v[size_t(y) * m.w + x] = value;
    }
  }
}

void FillCircle(Mask& m, Point c, double r) {
  const int y0 = std::max(0, int(c.y - r)), y1 = std::min(m.h - 1, int(c.y + r) + 1);
  for (int y = y0; y <= y1; ++y) {
    const double dy = y + 0.5 - c.y;
    if (dy * dy > r * r) continue;
    const double dx = std::sqrt(r * r - dy * dy);
    const int x0 = std::max(0, int(std::ceil(c.x - dx - 0.5))), x1 = std::min(m.w, int(std::ceil(c.x + dx - 0.5)));
    for (int x = x0; x < x1; ++x) m.v[size_t(y) * m.w + x] = 255;
  }
}

// A closed outline `width` wide with round joins.
void StrokeClosed(Mask& m, const std::vector<Point>& pts, double width) {
  const double half = width / 2;
  for (size_t i = 0; i < pts.size(); ++i) {
    const Point a = pts[i], b = pts[(i + 1) % pts.size()];
    const double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
    if (len > 0) {
      const double nx = -dy / len * half, ny = dx / len * half;
      FillPolygon(m, {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}}, 255);
    }
    FillCircle(m, a, half);
  }
}

// The mask or its copy moved by (dx, dy), whichever is more (the plate's
// drop shadow).
void LighterWithOffset(Mask& m, int dx, int dy) {
  const std::vector<uint8_t> src = m.v;
  for (int y = 0; y < m.h; ++y) {
    for (int x = 0; x < m.w; ++x) {
      const int sx = x - dx, sy = y - dy;
      if (sx >= 0 && sy >= 0 && sx < m.w && sy < m.h) {
        uint8_t& d = m.v[size_t(y) * m.w + x];
        d = std::max(d, src[size_t(sy) * m.w + sx]);
      }
    }
  }
}

// Coverage per output pixel (box filter over the ss x ss samples).
std::vector<float> Down(const Mask& m, int ss) {
  const int w = m.w / ss, h = m.h / ss;
  std::vector<float> out(size_t(w) * h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      uint32_t sum = 0;
      for (int sy = 0; sy < ss; ++sy) {
        const uint8_t* row = &m.v[size_t(y * ss + sy) * m.w + x * ss];
        for (int sx = 0; sx < ss; ++sx) sum += row[sx];
      }
      out[size_t(y) * w + x] = sum / (255.0f * ss * ss);
    }
  }
  return out;
}

// Straight-alpha "over".
Rgba Over(const Rgba& top, const Rgba& bottom) {
  const float ta = top.a / 255.0f, ba = bottom.a / 255.0f;
  const float a = ta + ba * (1 - ta);
  if (a <= 0) return {};
  auto mix = [&](float t, float b) { return (t * ta + b * ba * (1 - ta)) / a; };
  return {mix(top.r, bottom.r), mix(top.g, bottom.g), mix(top.b, bottom.b), a * 255.0f};
}

// Bilinear sample with premultiplied alpha (no dark fringes).
Rgba Sample(const Image& img, double x, double y) {
  x = std::clamp(x - 0.5, 0.0, double(img.w - 1));
  y = std::clamp(y - 0.5, 0.0, double(img.h - 1));
  const uint32_t x0 = uint32_t(x), y0 = uint32_t(y), x1 = std::min(x0 + 1, img.w - 1), y1 = std::min(y0 + 1, img.h - 1);
  const float fx = float(x - x0), fy = float(y - y0);
  float r = 0, g = 0, b = 0, a = 0;
  auto add = [&](const Rgba& p, float wgt) {
    const float pa = p.a * wgt;
    r += p.r * pa, g += p.g * pa, b += p.b * pa, a += pa;
  };
  add(img.at(x0, y0), (1 - fx) * (1 - fy));
  add(img.at(x1, y0), fx * (1 - fy));
  add(img.at(x0, y1), (1 - fx) * fy);
  add(img.at(x1, y1), fx * fy);
  return a > 0 ? Rgba{r / a, g / a, b / a, a} : Rgba{};
}

// Letters in an upright box W x H (the original E, in texture pixels).
constexpr double kW = 34.0, kH = 58.0;

// `logo` is the original (s = 1) or its 4x enlargement (s = 4): every
// measure below is in the original's pixels, times s.
//
// RE is two more letters of the word: as tall as M P E D (55 px), on the
// same top and bottom lines (rising 0.0425 px per px to the right), leaning
// like them (0.32), one letter gap left of the A, on the same black plate.
// RE's rim goes behind the original, so the two plates read as one.
Image Compose(const Image& logo, double s = 1.0) {
  const int ss = s > 1.0 ? kSsSharp : kSs;
  // tools/art/reamped_title.py build()'s parameters.
  constexpr double kScale = 0.8, kShiftX = 100, kShiftY = 18, kLean = 0.32, kSlope = 0.0425, kHeight = 55.0,
                   kGap = 6.3, kERight = 82.5, kBorder = 6.0;
  const int w = int(logo.w), h = int(logo.h);
  const double f = kHeight / kH, angle = std::atan(kSlope);
  const double xb = kERight - f * (kW + kGap + 34.0);
  const double yb = 135.0 + 0.042 * (168.0 - xb);  // M P E D's bottom line, carried left
  auto place = [&](Point p) {
    const double x = f * (p.x + kLean * (kH - p.y)), y = f * (p.y - kH);
    const double rx = x * std::cos(angle) + y * std::sin(angle), ry = -x * std::sin(angle) + y * std::cos(angle);
    return Point{(kShiftX + kScale * (xb + rx)) * s * ss, (kShiftY + kScale * (yb + ry)) * s * ss};
  };
  auto placed = [&](std::vector<Point> pts) {
    for (Point& p : pts) p = place(p);
    return pts;
  };
  const double e_x = kW + kGap;
  const std::vector<Point> r_outer = {{0, 0}, {36, 0}, {36, 31}, {28, 35}, {36, 58}, {22, 58}, {17, 38}, {14, 38}, {14, 58}, {0, 58}};
  const std::vector<Point> r_counter = {{14, 13}, {22, 13}, {22, 25}, {14, 25}};
  std::vector<Point> e_outer = {{0, 0}, {34, 0}, {34, 13}, {14, 13}, {14, 23}, {30, 23}, {30, 36}, {14, 36}, {14, 45}, {34, 45}, {34, 58}, {0, 58}};
  for (Point& p : e_outer) p.x += e_x;
  const std::vector<std::vector<Point>> outlines = {placed(r_outer), placed(e_outer)};

  const int bw = w * ss, bh = h * ss;
  Mask plate(bw, bh), fill(bw, bh), rim(bw, bh);
  for (const auto& pts : outlines) {
    FillPolygon(plate, pts, 255);
    FillPolygon(rim, pts, 255);
    StrokeClosed(plate, pts, int(2 * kBorder * kScale * s * ss));
    StrokeClosed(rim, pts, int((2 * kBorder + 1.6) * kScale * s * ss));
    FillPolygon(fill, pts, 255);
  }
  LighterWithOffset(plate, int(2 * s * ss), int(4 * s * ss));
  LighterWithOffset(rim, int(2 * s * ss), int(4 * s * ss));
  FillPolygon(fill, placed(r_counter), 0);

  const auto rim_d = Down(rim, ss), plate_d = Down(plate, ss), fill_d = Down(fill, ss);
  const Rgba kRim{190, 20, 110, 255}, kBlack{0, 0, 0, 255}, kPink{255, 106, 181, 255};
  auto paste = [](Rgba& d, const Rgba& c, float m) {
    d = {c.r * m + d.r * (1 - m), c.g * m + d.g * (1 - m), c.b * m + d.b * (1 - m), c.a * m + d.a * (1 - m)};
  };

  // The original, smaller and moved right: RE's rim behind it, RE's plate and
  // letters in front of it left of the gap between E and A (the original's
  // left stars partly behind RE), the A in front right of it.
  Image out{logo.w, logo.h, std::vector<Rgba>(logo.px.size())};
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      Rgba small;
      const double sx = (x + 0.5 - kShiftX * s) / kScale, sy = (y + 0.5 - kShiftY * s) / kScale;
      if (sx >= 0 && sy >= 0 && sx < w && sy < h) small = Sample(logo, sx, sy);
      const size_t i = size_t(y) * w + x;
      Rgba back, front;
      paste(back, kRim, rim_d[i]);
      paste(front, kBlack, plate_d[i]);
      paste(front, kPink, fill_d[i]);
      Rgba px = Over(small, back);
      const double edge = (kShiftX + kScale * (kERight + 3.2)) * s + (kShiftY * s + kScale * 135 * s - (y + 0.5)) * kLean;
      if (x + 0.5 < edge) px = Over(front, px);
      out.px[i] = px;
    }
  }
  return out;
}

// The runtime's texture key (16 hex digits): XXH3-64 of the base level as
// stored (DXT3, whole 32x32-block tiles).
std::string Key(const Bytes& xpr, const Texture& t) {
  const size_t bw = (t.width + 3) / 4, bh = (t.height + 3) / 4;
  const size_t size = ((bw + 31) & ~size_t(31)) * ((bh + 31) & ~size_t(31)) * 16;
  if (t.pixels_at + size > xpr.size()) return {};
  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx",
                static_cast<unsigned long long>(XXH3_64bits(xpr.data() + t.pixels_at, size)));
  return hex;
}

}  // namespace

bool WriteSharp(const std::vector<uint8_t>& original_xpr, const std::filesystem::path& replace_dir) {
  namespace fs = std::filesystem;
  Texture t;
  if (!Parse(original_xpr, t)) return false;
  Bytes patched = original_xpr;
  if (!Patch(patched)) return false;
  const std::string original_key = Key(original_xpr, t), patched_key = Key(patched, t);
  if (original_key.empty() || patched_key.empty()) return false;
  // The pack's enlargement of the original logo.
  std::ifstream in(replace_dir / (original_key + ".png"), std::ios::binary);
  if (!in) return false;
  const std::string png((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(png.data()), int(png.size()), &w, &h,
                                          &channels, 4);
  if (!pixels) return false;
  Image big{uint32_t(w), uint32_t(h), std::vector<Rgba>(size_t(w) * h)};
  for (size_t i = 0; i < big.px.size(); ++i) {
    big.px[i] = {float(pixels[i * 4]), float(pixels[i * 4 + 1]), float(pixels[i * 4 + 2]), float(pixels[i * 4 + 3])};
  }
  stbi_image_free(pixels);
  const Image sharp = Compose(big, double(w) / t.width);
  std::vector<uint8_t> rgba(sharp.px.size() * 4);
  auto q = [](float v) { return uint8_t(std::clamp(std::lround(v), 0L, 255L)); };
  for (size_t i = 0; i < sharp.px.size(); ++i) {
    rgba[i * 4] = q(sharp.px[i].r), rgba[i * 4 + 1] = q(sharp.px[i].g), rgba[i * 4 + 2] = q(sharp.px[i].b);
    rgba[i * 4 + 3] = q(sharp.px[i].a);
  }
  std::string out;
  stbi_write_png_to_func([](void* ctx, void* data, int size) {
    static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), size_t(size));
  }, &out, w, h, 4, rgba.data(), w * 4);
  std::ofstream file(replace_dir / (patched_key + ".png"), std::ios::binary);
  file.write(out.data(), std::streamsize(out.size()));
  return bool(file);
}

bool WriteLogoPng(const std::vector<uint8_t>& original_xpr, const std::filesystem::path& replace_dir,
                  const std::filesystem::path& out_png, bool reamped) {
  Texture t;
  if (!Parse(original_xpr, t)) return false;
  Bytes original = original_xpr;
  // The pack's enlargement of the original when there is one, else the
  // texture itself (decoded from DXT3).
  Image logo;
  double scale = 1.0;
  const std::string key = Key(original_xpr, t);
  std::ifstream in(replace_dir / (key + ".png"), std::ios::binary);
  if (!key.empty() && in) {
    const std::string png((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, channels = 0;
    if (stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(png.data()), int(png.size()), &w,
                                                &h, &channels, 4)) {
      logo = {uint32_t(w), uint32_t(h), std::vector<Rgba>(size_t(w) * h)};
      for (size_t i = 0; i < logo.px.size(); ++i) {
        logo.px[i] = {float(pixels[i * 4]), float(pixels[i * 4 + 1]), float(pixels[i * 4 + 2]),
                      float(pixels[i * 4 + 3])};
      }
      stbi_image_free(pixels);
      scale = double(w) / t.width;
    }
  }
  if (logo.px.empty()) logo = Decode(original, t);
  const Image out = reamped ? Compose(logo, scale) : logo;
  std::vector<uint8_t> rgba(out.px.size() * 4);
  auto q = [](float v) { return uint8_t(std::clamp(std::lround(v), 0L, 255L)); };
  for (size_t i = 0; i < out.px.size(); ++i) {
    rgba[i * 4] = q(out.px[i].r), rgba[i * 4 + 1] = q(out.px[i].g), rgba[i * 4 + 2] = q(out.px[i].b);
    rgba[i * 4 + 3] = q(out.px[i].a);
  }
  std::string png;
  stbi_write_png_to_func([](void* ctx, void* data, int size) {
    static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), size_t(size));
  }, &png, int(out.w), int(out.h), 4, rgba.data(), int(out.w) * 4);
  std::ofstream file(out_png, std::ios::binary);
  file.write(png.data(), std::streamsize(png.size()));
  return bool(file);
}

bool Patch(std::vector<uint8_t>& xpr) {
  Texture t;
  if (!Parse(xpr, t)) return false;
  const Image logo = Decode(xpr, t);
  Encode(Compose(logo), xpr, t);
  return true;
}

}  // namespace amped3::title_logo
