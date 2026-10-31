#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "keymap.h"

// Legend tile text, hardware-free. The glue passes the font's glyph advances, so the
// wrapping rule is tested here without the font.

// Text a tile shows: the user's label, else something readable derived from the value
// (keystroke as written with F-keys uppercased, URL host without www., app/shortcut name).
inline void displayLabel(const KeyEntry &e, char *out, size_t cap) {
  if (cap == 0) return;
  const bool derived = e.label[0] == 0;
  const char *src = derived ? e.value : e.label;
  size_t n;
  if (derived && e.type == KeyType::Url) {
    const char *host = strstr(e.value, "://");
    src = host ? host + 3 : e.value;
    if (strncmp(src, "www.", 4) == 0) src += 4;
    n = strcspn(src, "/?#");
  } else {
    n = strlen(src);
  }
  if (n > cap - 1) n = cap - 1;
  memcpy(out, src, n);
  out[n] = 0;
  if (derived && e.type == KeyType::Keystroke && out[0] == 'f' && n > 1 && !strchr(out, '+')) out[0] = 'F';
}

using GlyphWidthFn = uint8_t (*)(char);
constexpr size_t kLegendLineMax = kKeyValueMax;  // longest displayLabel() result

struct LabelLines {
  char line1[kLegendLineMax + 1];
  char line2[kLegendLineMax + 4];  // room for "..."
};

namespace legend_detail {
inline uint16_t width(const char *s, size_t n, GlyphWidthFn w) {
  uint16_t t = 0;
  for (size_t i = 0; i < n; i++) t += w(s[i]);
  return t;
}

// Number of leading chars of s[0..n) that fit in maxPx.
inline size_t fit(const char *s, size_t n, uint16_t maxPx, GlyphWidthFn w) {
  uint16_t t = 0;
  size_t i = 0;
  for (; i < n; i++) {
    if (t + w(s[i]) > maxPx) break;
    t += w(s[i]);
  }
  return i;
}
}  // namespace legend_detail

// Up to two lines, each at most maxPx wide. Line 1 breaks at the last space that fits
// (hard cut if none); line 2 that still overflows is cut and ends with "...".
inline void wrapLabel(const char *text, uint16_t maxPx, GlyphWidthFn w, LabelLines &out) {
  using namespace legend_detail;
  out.line1[0] = out.line2[0] = 0;
  size_t n = strnlen(text, kLegendLineMax);
  size_t a = fit(text, n, maxPx, w);
  if (a == n) {
    memcpy(out.line1, text, n);
    out.line1[n] = 0;
    return;
  }
  size_t cut = a;
  for (size_t k = a; k > 0; k--)
    if (text[k] == ' ') {
      cut = k;
      break;
    }
  if (cut == 0) cut = 1;  // first glyph alone is wider than the tile: still make progress
  memcpy(out.line1, text, cut);
  out.line1[cut] = 0;

  const char *rest = text + cut;
  size_t rn = n - cut;
  while (rn && *rest == ' ') {
    rest++;
    rn--;
  }
  if (fit(rest, rn, maxPx, w) == rn) {
    memcpy(out.line2, rest, rn);
    out.line2[rn] = 0;
    return;
  }
  uint16_t dots = width("...", 3, w);
  size_t c = dots >= maxPx ? 0 : fit(rest, rn, static_cast<uint16_t>(maxPx - dots), w);
  while (c && rest[c - 1] == ' ') c--;
  memcpy(out.line2, rest, c);
  memcpy(out.line2 + c, "...", 4);
}
