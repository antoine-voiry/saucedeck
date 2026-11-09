#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "keymap.h"
#include "legend.h"
#include "tile_anim.h"

// "Candy" LCD theme, hardware-free: colours, tile look, legend font choice, volume segments.
// Values from docs/superpowers/plans/2026-10-31-lcd-theme-candy-plan4.md (Global Constraints).

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

constexpr uint16_t kThemeBg = rgb565(0x0B, 0x0B, 0x1A);
constexpr uint16_t kThemeBar = rgb565(0x14, 0x14, 0x2B);
constexpr uint16_t kThemeSegOff = rgb565(0x34, 0x34, 0x4F);
constexpr uint16_t kThemeFailed = rgb565(0xFF, 0x9F, 0x0A);
constexpr uint16_t kThemeHelper = rgb565(0x30, 0xD1, 0x58);
constexpr uint16_t kThemeWifi = rgb565(0x0A, 0x84, 0xFF);
constexpr uint16_t kThemeMicLive = rgb565(0x30, 0xD1, 0x58);
constexpr uint16_t kThemeMicMuted = rgb565(0xFF, 0x45, 0x3A);
constexpr uint16_t kThemeMicUnknown = rgb565(0x8E, 0x8E, 0x93);
constexpr uint16_t kThemeMutedSeg = rgb565(0x8E, 0x8E, 0x93);
constexpr uint16_t kThemeTopText = rgb565(0xC7, 0xC7, 0xE0);

constexpr uint16_t kKeyHues[kKeyCount] = {
    rgb565(0xFF, 0x4D, 0x6D), rgb565(0xFF, 0xB0, 0x00), rgb565(0xEA, 0x43, 0x35), rgb565(0x2A, 0xAB, 0xEE),
    rgb565(0xFF, 0x7A, 0x00), rgb565(0x8E, 0x8E, 0x93), rgb565(0x34, 0xC7, 0x59), rgb565(0xFF, 0x00, 0x33),
    rgb565(0xAF, 0x52, 0xDE), rgb565(0x30, 0xD1, 0x58), rgb565(0x0A, 0x84, 0xFF), rgb565(0x00, 0xC2, 0xA8)};

// --- tiles ---

struct TileLook {
  uint16_t fill, band, text;
};

// level: TileAnim level (0 idle .. kFull held). Text turns black once the fill is half accent.
inline TileLook tileLook(uint8_t key, uint8_t level, bool failed) {
  uint16_t accent = failed ? kThemeFailed : kKeyHues[key < kKeyCount ? key : 0];
  uint16_t idle = blend565(kThemeBg, accent, 140, 255);
  TileLook t;
  t.fill = blend565(idle, accent, level, TileAnim::kFull);
  t.band = accent;
  t.text = level * 2 >= TileAnim::kFull ? 0x0000 : 0xFFFF;
  return t;
}

// --- legend ---

enum class LegendFont : uint8_t { Bold, Regular };

struct Legend {
  LegendFont font;
  LabelLines lines;
};

namespace theme_detail {
inline uint16_t px(const char *s, size_t n, GlyphWidthFn w) {
  uint16_t t = 0;
  for (size_t i = 0; i < n; i++) t += w(s[i]);
  return t;
}

// Best two-line split of text (n chars) for font w: both lines <= maxPx, narrowest wider
// line wins (first on ties). Splits at a space, or between any two chars if byChar.
inline bool split2(const char *text, size_t n, uint16_t maxPx, GlyphWidthFn w, bool byChar, size_t &at,
                   size_t &skip) {
  bool found = false;
  uint16_t best = 0xFFFF;
  for (size_t k = 1; k < n; k++) {
    size_t a = k, b = k;  // line 1 = [0,a), line 2 = [b,n)
    if (!byChar) {
      if (text[k] != ' ') continue;
      while (a > 0 && text[a - 1] == ' ') a--;
      while (b < n && text[b] == ' ') b++;
      if (a == 0 || b == n) continue;
    }
    uint16_t w1 = px(text, a, w), w2 = px(text + b, n - b, w);
    if (w1 > maxPx || w2 > maxPx) continue;
    uint16_t wide = w1 > w2 ? w1 : w2;
    if (wide < best) {
      best = wide;
      at = a;
      skip = b;
      found = true;
    }
  }
  return found;
}

inline void setTwo(const char *text, size_t n, size_t at, size_t skip, LegendFont f, Legend &out) {
  out.font = f;
  memcpy(out.lines.line1, text, at);
  out.lines.line1[at] = 0;
  memcpy(out.lines.line2, text + skip, n - skip);
  out.lines.line2[n - skip] = 0;
}
}  // namespace theme_detail

// Pick font + lines for a tile label of at most maxPx per line:
// 1 line bold, 1 line regular, 2 lines at a space (bold, else regular), a single long word
// split in balanced halves (bold, else regular), else wrapLabel in regular ("..." on overflow).
inline void chooseLegend(const char *text, uint16_t maxPx, GlyphWidthFn bold, GlyphWidthFn regular, Legend &out) {
  using namespace theme_detail;
  size_t n = strnlen(text, kLegendLineMax);
  out.lines.line1[0] = out.lines.line2[0] = 0;
  out.font = LegendFont::Bold;
  if (px(text, n, bold) <= maxPx) {
    memcpy(out.lines.line1, text, n);
    out.lines.line1[n] = 0;
    return;
  }
  if (px(text, n, regular) <= maxPx) {
    out.font = LegendFont::Regular;
    memcpy(out.lines.line1, text, n);
    out.lines.line1[n] = 0;
    return;
  }
  size_t at = 0, skip = 0;
  if (split2(text, n, maxPx, bold, false, at, skip)) return setTwo(text, n, at, skip, LegendFont::Bold, out);
  if (split2(text, n, maxPx, regular, false, at, skip)) return setTwo(text, n, at, skip, LegendFont::Regular, out);
  if (memchr(text, ' ', n) == nullptr) {
    if (split2(text, n, maxPx, bold, true, at, skip)) return setTwo(text, n, at, skip, LegendFont::Bold, out);
    if (split2(text, n, maxPx, regular, true, at, skip)) return setTwo(text, n, at, skip, LegendFont::Regular, out);
  }
  out.font = LegendFont::Regular;
  wrapLabel(text, maxPx, regular, out.lines);
}

// --- volume segments ---

constexpr uint8_t kVolSegments = 20;

inline uint8_t litSegments(uint8_t pct) {
  uint32_t n = (static_cast<uint32_t>(pct) * kVolSegments + 50) / 100;
  return n > kVolSegments ? kVolSegments : static_cast<uint8_t>(n);
}

// Green (#30D158) -> yellow (#FFD60A) at segment 10 -> red (#FF453A) at segment 19, linear in RGB
// per half. Muted: one grey for every lit segment.
inline uint16_t segmentColor(uint8_t i, bool muted) {
  if (muted) return kThemeMutedSeg;
  if (i > kVolSegments - 1) i = kVolSegments - 1;
  constexpr int g[3] = {0x30, 0xD1, 0x58}, y[3] = {0xFF, 0xD6, 0x0A}, r[3] = {0xFF, 0x45, 0x3A};
  const int *from = i <= 10 ? g : y, *to = i <= 10 ? y : r;
  int num = i <= 10 ? i : i - 10, den = i <= 10 ? 10 : 9;
  uint8_t c[3];
  for (int k = 0; k < 3; k++) c[k] = static_cast<uint8_t>(from[k] + (to[k] - from[k]) * num / den);
  return rgb565(c[0], c[1], c[2]);
}

struct SegSpan {
  uint8_t first, count;
  bool lit;  // true: draw [first, first+count) lit; false: draw them off
};

inline SegSpan segDelta(uint8_t oldLit, uint8_t newLit) {
  if (newLit > oldLit) return {oldLit, static_cast<uint8_t>(newLit - oldLit), true};
  return {newLit, static_cast<uint8_t>(oldLit - newLit), false};
}

// --- card dots ---

// Centre x of dot k of count (count <= 7): 9 px pitch, right aligned, last centre 313
// (5 px dot spans 311..315, inside the 316 limit).
inline int16_t cardDotX(uint8_t k, uint8_t count) {
  return static_cast<int16_t>(313 - (count - 1 - k) * 9);
}
