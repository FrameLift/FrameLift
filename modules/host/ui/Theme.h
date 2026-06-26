#pragma once

#include "ThemeSettings.h"

// ── Host-side theme application ───────────────────────────────────────────────
// Applies the user's theme settings to the live Dear ImGui context. Both calls
// touch global ImGui state and MUST run outside any NewFrame()..Render() pair
// (i.e. before UIBeginFrame at the top of the frame, or before the first frame).
namespace Theme
{
// Apply the base preset (Dark/Light/Classic) then retint accent-bearing colors
// from s.accentColor. Cheap; safe to call every time the style changes.
void ApplyStyle(const ThemeSettings& s);

// Rebuild the font atlas using Dear ImGui's default font. This legacy ImGui path
// is dormant during the Qt migration.
void RebuildFonts(const ThemeSettings& s);
} // namespace Theme
