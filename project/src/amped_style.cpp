// amped_style.cpp - See amped_style.h. Shapes and colours taken from the
// game's riding tip ("Butter by moving..."): an off-white arrow with a dark
// outline whose head stands out above and below the body, cyan Agency-like
// text outlined in dark blue, a smaller light-blue arrow, and the score's
// yellow digits with a black outline. The condensed font is Saira Extra
// Condensed (SIL OFL), the closest open match to the game's New Agency.

#include "amped_style.h"

#include "fonts.h"

#include <imgui.h>
#include <rex/cvar.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace amped3::amped_style {
namespace {

constexpr uint32_t kPaper = 0xECE9E1, kPaperShade = 0xD6D2C8, kInk = 0x26282C, kCyan = 0x4FC9F2,
                   kCyanEdge = 0x0E2A3C, kTagBlue = 0x8DB5E0, kTagShade = 0x7AA6D6, kTagEdge = 0xE3EAF0, kTagText = 0x1E2A36, kYellow = 0xF7CF10,
                   kSubtitle = 0x3C5468;

// The game's 16:9 picture: on an ultrawide screen (the runtime's
// ultrawide_squeeze, 16:9 / the window's aspect) its UI is in the middle
// squeeze x width, on a 16:10 one (tall_squeeze) in the middle rows, so
// banners are placed and sized in it and clipped to it, as the game's HUD.
float Squeeze(const char* name) {
  try {
    return float(std::clamp(std::stod(rex::cvar::GetFlagByName(name)), 0.1, 1.0));
  } catch (...) {
    return 1.0f;
  }
}

ImVec4 Frame16x9(const ImGuiIO& io) {
  const float w = io.DisplaySize.x * Squeeze("ultrawide_squeeze"), h = io.DisplaySize.y * Squeeze("tall_squeeze");
  return ImVec4((io.DisplaySize.x - w) * 0.5f, (io.DisplaySize.y - h) * 0.5f, w, h);
}

ImU32 Color(uint32_t rgb, float alpha) {
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, uint8_t(255 * std::clamp(alpha, 0.0f, 1.0f)));
}

// Text with a solid outline (the game's look), drawn as offset copies.
void Outlined(ImDrawList* draw, ImFont* font, float px, ImVec2 at, uint32_t fill, uint32_t edge, float width,
              float alpha, const char* text) {
  const ImU32 e = Color(edge, alpha);
  for (int i = 0; i < 12; ++i) {
    const float a = i * 0.5235988f;
    draw->AddText(font, px, ImVec2(at.x + std::cos(a) * width, at.y + std::sin(a) * width), e, text);
  }
  draw->AddText(font, px, at, Color(fill, alpha), text);
}

// An arrow: a body from x0 to x1, height h at center_y, and a head standing
// out by `flare` above and below, its tip `head` past x1.
void Arrow(ImDrawList* draw, float x0, float x1, float cy, float h, float flare, float head, uint32_t fill,
           uint32_t shade, uint32_t edge, float edge_w, float alpha, float shadow) {
  const float y0 = cy - h * 0.5f, y1 = cy + h * 0.5f;
  const ImVec2 outline[] = {{x0, y0}, {x1, y0}, {x1, y0 - flare}, {x1 + head, cy},
                            {x1, y1 + flare}, {x1, y1}, {x0, y1}};
  if (shadow > 0) {
    ImVec2 s[7];
    for (int i = 0; i < 7; ++i) s[i] = ImVec2(outline[i].x + shadow, outline[i].y + shadow);
    draw->AddConcavePolyFilled(s, 7, IM_COL32(0, 0, 0, uint8_t(90 * alpha)));
  }
  // Body with a soft top-to-bottom shade, then the head.
  draw->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1 + 1, y1), Color(fill, alpha), Color(fill, alpha),
                                Color(shade, alpha), Color(shade, alpha));
  draw->AddTriangleFilled(ImVec2(x1, y0 - flare), ImVec2(x1 + head, cy), ImVec2(x1, y1 + flare), Color(fill, alpha));
  draw->AddPolyline(outline, 7, Color(edge, alpha), ImDrawFlags_None, edge_w);
}

void Star(ImDrawList* draw, ImVec2 c, float r, ImU32 fill, ImU32 outline, float edge) {
  ImVec2 p[10];
  for (int i = 0; i < 10; ++i) {
    const float angle = -1.5707963f + i * 0.6283185f;
    const float rr = (i & 1) ? r * 0.45f : r;
    p[i] = ImVec2(c.x + std::cos(angle) * rr, c.y + std::sin(angle) * rr);
  }
  draw->AddConcavePolyFilled(p, 10, fill);
  draw->AddPolyline(p, 10, outline, ImDrawFlags_Closed, edge);
}

void DrawIcon(ImDrawList* draw, Icon icon, ImVec2 c, float r, float s, float alpha) {
  const ImU32 ink = Color(kInk, alpha), yellow = Color(kYellow, alpha), cyan = Color(kCyan, alpha);
  switch (icon) {
    case Icon::kStar: Star(draw, c, r, yellow, ink, 4.0f * s); break;
    case Icon::kStarOutline: Star(draw, c, r, Color(kPaper, alpha), ink, 4.0f * s); break;
    case Icon::kNote: {
      // A quaver: head, stem and flag.
      const ImVec2 head(c.x - r * 0.25f, c.y + r * 0.55f);
      draw->AddCircleFilled(head, r * 0.38f, cyan);
      draw->AddCircle(head, r * 0.38f, ink, 0, 3.0f * s);
      const float sx = head.x + r * 0.36f;
      draw->AddLine(ImVec2(sx, head.y), ImVec2(sx, c.y - r * 0.85f), ink, 5.0f * s);
      draw->AddBezierCubic(ImVec2(sx, c.y - r * 0.85f), ImVec2(sx + r * 0.6f, c.y - r * 0.5f),
                           ImVec2(sx + r * 0.5f, c.y - r * 0.1f), ImVec2(sx + r * 0.25f, c.y + r * 0.05f), ink,
                           5.0f * s);
      break;
    }
    case Icon::kSaved: {
      // A disk with its label, cyan.
      const ImVec2 a(c.x - r * 0.8f, c.y - r * 0.8f), b(c.x + r * 0.8f, c.y + r * 0.8f);
      draw->AddRectFilled(a, b, cyan, 4.0f * s);
      draw->AddRectFilled(ImVec2(a.x + r * 0.35f, a.y), ImVec2(b.x - r * 0.35f, a.y + r * 0.6f), Color(kPaper, alpha));
      draw->AddRectFilled(ImVec2(a.x + r * 0.3f, c.y + r * 0.15f), ImVec2(b.x - r * 0.3f, b.y - r * 0.2f),
                          Color(kPaper, alpha), 2.0f * s);
      draw->AddRect(a, b, ink, 4.0f * s, 0, 3.5f * s);
      break;
    }
    case Icon::kNone: break;
  }
}

}  // namespace

float Shown(float t, float seconds) {
  const float in = std::clamp(t / 0.3f, 0.0f, 1.0f), out = std::clamp((seconds - t) / 0.35f, 0.0f, 1.0f);
  return std::min(in, out);
}

void Draw(ImDrawList* draw, const ImGuiIO& io, float y, const Banner& banner, float shown, float alpha) {
  ImFont* hint = fonts::Find("ReAmped Hint");
  ImFont* hint_semi = fonts::Find("ReAmped Hint Semi");
  if (!hint || !hint_semi || alpha <= 0) return;
  const ImVec4 frame = Frame16x9(io);
  const float frame_x = frame.x, frame_w = frame.z;
  const float s = frame.w / 1080.0f, center_y = frame.y + y * frame.w;
  float title_px = 50.0f * s, sub_px = 34.0f * s;
  const float value_px = 58.0f * s, tag_px = 26.0f * s;
  const bool two_lines = !banner.subtitle.empty() || banner.progress >= 0;
  const float h = (two_lines ? 128.0f : 92.0f) * s;
  const float icon_w = banner.icon == Icon::kNone ? 0.0f : 96.0f * s;
  // Long lines (song names) get smaller to fit, down to 70%.
  const float room = frame_w * 0.62f - 120.0f * s - icon_w;
  float title_w = hint->CalcTextSizeA(title_px, FLT_MAX, 0, banner.title.c_str()).x;
  if (title_w > room) {
    title_px *= std::max(0.7f, room / title_w);
    title_w = hint->CalcTextSizeA(title_px, FLT_MAX, 0, banner.title.c_str()).x;
  }
  float sub_w = hint_semi->CalcTextSizeA(sub_px, FLT_MAX, 0, banner.subtitle.c_str()).x;
  if (sub_w > room) {
    sub_px *= std::max(0.7f, room / sub_w);
    sub_w = hint_semi->CalcTextSizeA(sub_px, FLT_MAX, 0, banner.subtitle.c_str()).x;
  }
  const float value_w =
      banner.value.empty() ? 0.0f : hint->CalcTextSizeA(value_px, FLT_MAX, 0, banner.value.c_str()).x + 36.0f * s;
  const float text_w = std::max({title_w, sub_w, banner.progress >= 0 ? 320.0f * s : 0.0f});
  const float body = std::min(84.0f * s + icon_w + text_w + value_w + 36.0f * s, frame_w * 0.62f);
  const float head = h * 0.42f, flare = h * 0.2f;
  // Off the left edge when hidden, slid in with an ease-out.
  const float ease = 1.0f - (1.0f - shown) * (1.0f - shown);
  const float x0 = frame_x - 40.0f * s, x1 = x0 + body - (1.0f - ease) * (body + head + 40.0f * s);
  draw->PushClipRect(ImVec2(frame.x, frame.y), ImVec2(frame.x + frame.z, frame.y + frame.w), true);
  Arrow(draw, x0, x1, center_y, h, flare, head, kPaper, kPaperShade, kInk, 4.0f * s, alpha, 8.0f * s);
  const float left = x1 - body + 40.0f * s + 44.0f * s;  // first content x
  const float right = x1 - 36.0f * s;                     // last content x
  // The tag: a small blue arrow under the body's left part, as the game's
  // area names (light outline, white slanted text outlined in dark).
  if (!banner.tag.empty()) {
    ImFont* slanted = fonts::Find("ReAmped UI Bold");
    ImFont* tag_font = slanted ? slanted : hint;
    const float tw = tag_font->CalcTextSizeA(tag_px, FLT_MAX, 0, banner.tag.c_str()).x;
    const float th = 44.0f * s, ty = center_y + h * 0.5f + flare + th * 0.5f + 10.0f * s;
    const float tx1 = x1 - body + 40.0f * s + 30.0f * s + tw + 40.0f * s;
    Arrow(draw, x0, tx1, ty, th, th * 0.22f, th * 0.55f, kTagBlue, kTagShade, kTagEdge, 2.5f * s, alpha, 5.0f * s);
    Outlined(draw, tag_font, tag_px, ImVec2(tx1 - tw - 24.0f * s, ty - tag_px * 0.55f), 0xFFFFFF, kTagText, 2.2f * s,
             alpha, banner.tag.c_str());
  }
  if (banner.icon != Icon::kNone) {
    DrawIcon(draw, banner.icon, ImVec2(left + 34.0f * s, center_y), 34.0f * s, s, alpha);
  }
  const float tx = left + icon_w;
  draw->PushClipRect(ImVec2(0, center_y - h), ImVec2(right - value_w, center_y + h), true);
  const float title_y = center_y - (two_lines ? h * 0.42f : title_px * 0.56f);
  Outlined(draw, hint, title_px, ImVec2(tx, title_y), kCyan, kCyanEdge, 2.6f * s, alpha, banner.title.c_str());
  const float line2 = title_y + title_px * 1.02f;
  if (banner.progress >= 0) {
    const float bx1 = std::max(tx + 200.0f * s, right - value_w), by = line2 + 10.0f * s;
    draw->AddRectFilled(ImVec2(tx, by), ImVec2(bx1, by + 16.0f * s), Color(kInk, 0.85f * alpha), 8.0f * s);
    const float fill = (bx1 - tx - 6.0f * s) * std::clamp(banner.progress, 0.0f, 1.0f);
    if (fill > 2.0f * s) {
      draw->AddRectFilled(ImVec2(tx + 3.0f * s, by + 3.0f * s), ImVec2(tx + 3.0f * s + fill, by + 13.0f * s),
                          Color(kYellow, alpha), 5.0f * s);
    }
  } else if (!banner.subtitle.empty()) {
    draw->AddText(hint_semi, sub_px, ImVec2(tx, line2), Color(kSubtitle, alpha), banner.subtitle.c_str());
  }
  draw->PopClipRect();
  if (!banner.value.empty()) {
    const float vw = value_w - 36.0f * s;
    Outlined(draw, hint, value_px, ImVec2(right - vw, center_y - value_px * 0.58f), kYellow, 0x000000,
             3.5f * s, alpha, banner.value.c_str());
  }
  draw->PopClipRect();
}

}  // namespace amped3::amped_style
