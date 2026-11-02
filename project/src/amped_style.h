// amped_style.h - Our on-screen notifications drawn like the game's own: the
// big off-white arrow that slides in from the left edge (its riding tips), a
// small blue arrow under it as the tag, condensed cyan text with a dark
// outline, and yellow outlined numbers like its score.
#pragma once

#include <string>

struct ImDrawList;
struct ImGuiIO;

namespace amped3::amped_style {

enum class Icon { kNone, kStar, kStarOutline, kNote, kSaved };

struct Banner {
  std::string tag;       // on the small blue arrow, e.g. "ACHIEVEMENT UNLOCKED"
  std::string title;     // the main line
  std::string subtitle;  // a second, smaller line (may be empty)
  std::string value;     // yellow score-style text on the right, e.g. "30G" (may be empty)
  float progress = -1;   // 0..1 draws a bar under the title; < 0 none
  Icon icon = Icon::kNone;
};

// Draws `banner` with its middle at `y` (0..1 down the game's 16:9 picture,
// which ultrawide and 16:10 screens show in the middle); `shown` 0..1 slides it
// in from the picture's left edge (eased), `alpha` fades it.
void Draw(ImDrawList* draw, const ImGuiIO& io, float y, const Banner& banner, float shown, float alpha);

// Seconds in and out of a banner shown for `seconds`: 0..1 for Draw's `shown`.
float Shown(float t, float seconds);

}  // namespace amped3::amped_style
