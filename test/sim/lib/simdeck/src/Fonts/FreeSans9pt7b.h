#pragma once
#include "Adafruit_GFX.h"

// Stand-in for Adafruit GFX Fonts/FreeSans9pt7b.h: same name and range (0x20-0x7E), but
// every glyph advances 10 px (real advances vary ~5-17 px). Sim checks layout logic, not
// pixel widths; legibility is a bench check.
struct SimFreeSansGlyphs {
  GFXglyph g[0x7E - 0x20 + 1];
  constexpr SimFreeSansGlyphs() : g() {
    for (GFXglyph &x : g) x = GFXglyph{0, 0, 0, 10, 0, 0};
  }
};
inline constexpr SimFreeSansGlyphs kSimFreeSansGlyphs{};
inline const GFXfont FreeSans9pt7b = {nullptr, const_cast<GFXglyph *>(kSimFreeSansGlyphs.g), 0x20, 0x7E, 22};
