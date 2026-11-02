// fonts.cpp - The bundled fonts for the in-game prompts
//
// Exo 2 and Saira Extra Condensed (SIL Open Font License) ship in fonts/ next
// to the exe. The fonts are
// added to the runtime's ImGui atlas by name; the runtime's name prompt finds
// them by the same names.

#include "fonts.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <imgui.h>
#include <rex/cvar.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace amped3::fonts {

void Load(ImFontAtlas* atlas) {
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0};
  struct Entry {
    const char* name;
    const char* file;
    float size;
  };
  const Entry entries[] = {
      {"ReAmped Display", "Exo2-ExtraBoldItalic.ttf", 64.0f},
      {"ReAmped UI", "Exo2-Regular.ttf", 28.0f},
      {"ReAmped UI Bold", "Exo2-ExtraBoldItalic.ttf", 28.0f},
      // The notifications (amped_style.cpp): condensed, like the game's own.
      {"ReAmped Hint", "SairaExtraCondensed-Bold.ttf", 64.0f},
      {"ReAmped Hint Semi", "SairaExtraCondensed-SemiBold.ttf", 40.0f},
  };
  // The static atlas is baked once, so bake at the size the text is drawn at
  // the output's height (the notifications scale with it: 1080p = 1x), else
  // it's enlarged and soft at 1440p and 4K.
  float bake = 1.0f;
  {
    const std::string height = rex::cvar::GetFlagByName("window_height");
    const int h = height.empty() ? 0 : std::atoi(height.c_str());
    if (h > 0) bake = std::clamp(h / 1080.0f, 1.0f, 2.5f);
  }
  const std::filesystem::path folder = rex::filesystem::GetExecutableFolder() / "fonts";
  int loaded = 0;
  for (const Entry& entry : entries) {
    const std::filesystem::path path = folder / entry.file;
    if (!std::filesystem::exists(path)) {
      REXLOG_WARN("Amped3: fonts: {} missing", path.string());
      continue;
    }
    ImFontConfig config;
    std::strncpy(config.Name, entry.name, sizeof(config.Name) - 1);
    config.OversampleH = 2;
    if (atlas->AddFontFromFileTTF(path.string().c_str(), entry.size * bake, &config, kRanges)) {
      ++loaded;
    }
  }
  REXLOG_INFO("Amped3: fonts: {} loaded (baked at {:.2f}x for the output)", loaded, bake);
}

ImFont* Find(const char* name) {
  for (ImFont* font : ImGui::GetIO().Fonts->Fonts) {
    if (std::strcmp(font->GetDebugName(), name) == 0) {
      return font;
    }
  }
  return nullptr;
}

}  // namespace amped3::fonts
