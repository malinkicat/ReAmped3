// boot_screen.cpp - The start-up logo and warning screens as one loading screen
//
// While the game loads, a thread of its own (the object at 0x828944F0) shows
// the Amped 3 logo (texture key fa22afde4841f96e, 1024x512) and then the
// legal/warning screen (8e6c16534e6037e3, 512x512). Both are drawn into the
// same 4:3 box in the middle of the 16:9 frame (640x480 of 1280x720). At
// start we draw one picture for that box - the title logo (Amped 3's, or
// ReAmped 3's with --title_logo; title_logo.cpp, from the player's own files,
// the sharp UI pack's enlargement when there is one) on black with
// "LOADING..." under it and the legal screen's notice at the bottom - and
// save it under both keys, squeezed to each texture's shape, so the two
// screens show as one.
//
// Without the sharp UI pack the pictures go to <user data>\cache\boot_screen
// and texture replacement is on only until the game ends its loading screen
// (sub_82456A88); replacements are never waited for in game
// (texture_replace_async), but are during the boot screen, so the originals
// don't flash up first.

#include "boot_screen.h"

#include "title_logo.h"
#include "ui_patch.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "../thirdparty/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "../thirdparty/stb/stb_image_write.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <imstb_truetype.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

REXCVAR_DEFINE_BOOL(boot_screen, true, "UI", "One loading screen (the title logo, LOADING... and the legal notice) in place of the start-up logo and warning screens");
REXCVAR_DECLARE(std::string, user_data_root);
REXCVAR_DECLARE(bool, title_logo);

namespace amped3::boot_screen {
namespace {

namespace fs = std::filesystem;

// Changes to the picture: bump to redraw it in existing folders.
constexpr int kVersion = 2;

// The 4:3 box at 4K: 1920x1440, drawn larger for sampling down.
constexpr int kW = 2560, kH = 1920;

struct Canvas {
  int w, h;
  std::vector<float> rgb;  // 0..1
};

void Blend(Canvas& c, int x, int y, float r, float g, float b, float a) {
  if (x < 0 || y < 0 || x >= c.w || y >= c.h || a <= 0) return;
  float* p = &c.rgb[(size_t(y) * c.w + x) * 3];
  p[0] += (r - p[0]) * a, p[1] += (g - p[1]) * a, p[2] += (b - p[2]) * a;
}

// The logo (straight-alpha RGBA) scaled into dst_w wide at (x0, y0), area
// sampled.
void DrawLogo(Canvas& c, const unsigned char* px, int w, int h, int x0, int y0, int dst_w) {
  const double s = double(w) / dst_w;
  const int dst_h = int(std::lround(h / s));
  for (int y = 0; y < dst_h; ++y) {
    for (int x = 0; x < dst_w; ++x) {
      double r = 0, g = 0, b = 0, a = 0, n = 0;
      const int sx0 = int(x * s), sx1 = std::max(sx0 + 1, int((x + 1) * s));
      const int sy0 = int(y * s), sy1 = std::max(sy0 + 1, int((y + 1) * s));
      for (int sy = sy0; sy < std::min(sy1, h); ++sy) {
        for (int sx = sx0; sx < std::min(sx1, w); ++sx) {
          const unsigned char* p = px + (size_t(sy) * w + sx) * 4;
          const double al = p[3] / 255.0;
          r += p[0] * al, g += p[1] * al, b += p[2] * al, a += al, n += 1;
        }
      }
      if (a <= 0) continue;
      Blend(c, x0 + x, y0 + y, float(r / a / 255), float(g / a / 255), float(b / a / 255), float(a / n));
    }
  }
}

// One line of Latin-1 text centred on cx with its baseline at `baseline`.
bool DrawText(Canvas& c, const std::vector<unsigned char>& ttf, const char* text, float px_height, int cx,
              int baseline, float tracking, float shade) {
  stbtt_fontinfo font;
  if (!stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) return false;
  const float scale = stbtt_ScaleForPixelHeight(&font, px_height);
  float width = 0;
  for (const char* t = text; *t; ++t) {
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&font, static_cast<unsigned char>(*t), &advance, &lsb);
    width += advance * scale + (t[1] ? tracking : 0);
  }
  float x = cx - width / 2;
  for (const char* t = text; *t; ++t) {
    int advance, lsb, gw, gh, ox, oy;
    stbtt_GetCodepointHMetrics(&font, static_cast<unsigned char>(*t), &advance, &lsb);
    unsigned char* bitmap = stbtt_GetCodepointBitmapSubpixel(&font, scale, scale, x - std::floor(x), 0,
                                                             static_cast<unsigned char>(*t), &gw, &gh, &ox, &oy);
    for (int y = 0; y < gh; ++y) {
      for (int gx = 0; gx < gw; ++gx) {
        Blend(c, int(std::floor(x)) + ox + gx, baseline + oy + y, shade, shade, shade, bitmap[y * gw + gx] / 255.0f);
      }
    }
    stbtt_FreeBitmap(bitmap, nullptr);
    x += advance * scale + tracking;
  }
  return true;
}

// The canvas stretched to w x h (bilinear: both outputs are larger than or
// close to the canvas), as a PNG.
bool WritePng(const Canvas& c, int w, int h, const fs::path& path) {
  std::vector<unsigned char> out(size_t(w) * h * 3);
  for (int y = 0; y < h; ++y) {
    const double fy = std::clamp((y + 0.5) * c.h / h - 0.5, 0.0, double(c.h - 1));
    const int y0 = int(fy), y1 = std::min(y0 + 1, c.h - 1);
    const double ty = fy - y0;
    for (int x = 0; x < w; ++x) {
      const double fx = std::clamp((x + 0.5) * c.w / w - 0.5, 0.0, double(c.w - 1));
      const int x0 = int(fx), x1 = std::min(x0 + 1, c.w - 1);
      const double tx = fx - x0;
      for (int k = 0; k < 3; ++k) {
        auto at = [&](int xx, int yy) { return c.rgb[(size_t(yy) * c.w + xx) * 3 + k]; };
        const double v = (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) +
                         (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
        out[(size_t(y) * w + x) * 3 + k] = (unsigned char)std::clamp(std::lround(v * 255), 0L, 255L);
      }
    }
  }
  std::string png;
  stbi_write_png_to_func([](void* ctx, void* data, int size) {
    static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), size_t(size));
  }, &png, w, h, 3, out.data(), w * 3);
  std::ofstream file(path, std::ios::binary);
  file.write(png.data(), std::streamsize(png.size()));
  return !png.empty() && bool(file);
}

bool Draw(const fs::path& replace) {
  const std::vector<uint8_t> xpr = ui_patch::OriginalTitleLogo();
  if (xpr.empty()) return false;
  const fs::path logo_png = replace / "boot_logo.tmp.png";
  if (!title_logo::WriteLogoPng(xpr, replace, logo_png, REXCVAR_GET(title_logo))) return false;
  std::ifstream logo_in(logo_png, std::ios::binary);
  const std::string logo_bytes((std::istreambuf_iterator<char>(logo_in)), {});
  logo_in.close();
  int lw = 0, lh = 0, channels = 0;
  unsigned char* logo = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(logo_bytes.data()),
                                              int(logo_bytes.size()), &lw, &lh, &channels, 4);
  std::error_code ec;
  fs::remove(logo_png, ec);
  if (!logo) return false;
  // Crop to what's drawn.
  int x0 = lw, y0 = lh, x1 = -1, y1 = -1;
  for (int y = 0; y < lh; ++y) {
    for (int x = 0; x < lw; ++x) {
      if (logo[(size_t(y) * lw + x) * 4 + 3] > 8) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    }
  }
  Canvas c{kW, kH, std::vector<float>(size_t(kW) * kH * 3, 0.0f)};
  if (x1 >= x0) {
    const int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    std::vector<unsigned char> crop(size_t(cw) * ch * 4);
    for (int y = 0; y < ch; ++y) {
      std::copy_n(logo + (size_t(y0 + y) * lw + x0) * 4, size_t(cw) * 4, crop.begin() + size_t(y) * cw * 4);
    }
    // The logo as wide as 60% of the box, a little above the middle (the
    // box's 4:3 is shown as 4:3).
    const int dst_w = kW * 60 / 100, dst_h = int(std::lround(double(ch) * dst_w / cw));
    DrawLogo(c, crop.data(), cw, ch, (kW - dst_w) / 2, kH * 44 / 100 - dst_h / 2, dst_w);
  }
  stbi_image_free(logo);
  std::ifstream in(rex::filesystem::GetExecutableFolder() / "fonts" / "SairaExtraCondensed-SemiBold.ttf",
                   std::ios::binary);
  const std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(in)), {});
  if (ttf.empty() || !DrawText(c, ttf, "LOADING...", kH * 0.06f, kW / 2, kH * 76 / 100, kH * 0.006f, 0.82f)) {
    REXLOG_WARN("Amped3: boot screen: no font for the loading line");
  } else {
    // The publisher's notice from the legal screen this replaces.
    DrawText(c, ttf, "\xA9 2005 Take-Two Interactive Software and its subsidiaries. All rights reserved.", kH * 0.028f,
             kW / 2, kH * 90 / 100, 0, 0.55f);
    DrawText(c, ttf, "Amped 3, 2K Sports, the 2K Sports logo and Take-Two Interactive Software are trademarks of "
             "Take-Two Interactive Software.", kH * 0.028f, kW / 2, kH * 94 / 100, 0, 0.55f);
  }
  // Each texture's shape at 4x: the logo's 1024x512, the warning's 512x512.
  return WritePng(c, 4096, 2048, replace / "fa22afde4841f96e.png") &&
         WritePng(c, 2048, 2048, replace / "8e6c16534e6037e3.png");
}

bool g_own_folder = false;

}  // namespace

std::string Prepare(const std::string& pack_folder) {
  if (!REXCVAR_GET(boot_screen)) return pack_folder;
  fs::path folder = pack_folder;
  if (folder.empty()) {
    const std::string user_root = REXCVAR_GET(user_data_root);
    folder = (user_root.empty() ? rex::filesystem::GetUserFolder() / "amped3" : fs::path(user_root)) / "cache" /
             "boot_screen";
  }
  const fs::path replace = folder / "replace", marker = replace / "boot_screen.txt";
  std::string stamp;
  if (std::ifstream in(marker); in) std::getline(in, stamp);
  const std::string want = std::to_string(kVersion) + "." + std::to_string(title_logo::kLogoVersion) +
                           (REXCVAR_GET(title_logo) ? " reamped" : " amped");
  if (stamp != want) {
    std::error_code ec;
    fs::create_directories(replace, ec);
    try {
      if (!Draw(replace)) {
        REXLOG_WARN("Amped3: boot screen: not made (the game's own start-up screens stay)");
        return pack_folder;
      }
    } catch (const std::exception& e) {
      REXLOG_WARN("Amped3: boot screen: {}", e.what());
      return pack_folder;
    }
    std::ofstream(marker) << want << "\n";
    REXLOG_INFO("Amped3: boot screen written to {}", replace.string());
  }
  g_own_folder = pack_folder.empty();
  return folder.string();
}

bool OwnFolder() { return g_own_folder; }

}  // namespace amped3::boot_screen

// sub_82456A88 ends the game's loading screen: replacements go back to their
// in-game settings (off without the sharp UI pack, never waited for).
REX_EXTERN(__imp__sub_82456A88);
extern "C" REX_FUNC(sub_82456A88) {
  __imp__sub_82456A88(ctx, base);
  static bool done = false;
  if (done || !REXCVAR_GET(boot_screen)) return;
  done = true;
  rex::cvar::SetFlagByName("texture_replace_async", "true");
  if (amped3::boot_screen::OwnFolder()) rex::cvar::SetFlagByName("texture_replace_enabled", "false");
  REXLOG_INFO("Amped3: boot screen over");
}
