// fonts.h - The bundled Exo 2 fonts for the in-game prompts
#pragma once

struct ImFont;
struct ImFontAtlas;

namespace amped3::fonts {

// Adds "ReAmped Display" (Exo 2 ExtraBold Italic, 64 px), "ReAmped UI" (Exo 2
// Regular, 28 px) and "ReAmped UI Bold" (ExtraBold Italic, 28 px) from the
// fonts folder next to the exe; nothing (with a warning) if they're missing.
void Load(ImFontAtlas* atlas);

// A font Load added, by name; nullptr if it isn't there.
ImFont* Find(const char* name);

}  // namespace amped3::fonts
