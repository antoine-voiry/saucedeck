#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

// ST7789 via the Adafruit API: records printed text and filled rects and rasterizes every
// primitive into an RGB565 framebuffer (fb) so tests can write BMP screenshots.
// Hardware assumptions: panel 320x240 landscape (rotation 3, as src/display.cpp sets it);
// fb is indexed [y][x] in drawn coordinates; no rotation remap. Rasterizers are ports of
// Adafruit GFX 1.12.x (BSD, Adafruit_GFX.cpp) so pixels match the real library.
struct SimLcdText {
  int16_t x, y;
  uint8_t size;
  uint16_t fg;
  std::string s;
  bool customFont;  // printed with setFont(&FreeSans9pt7b) (legend tiles)
  const void *font; // the GFXfont pointer passed to setFont (nullptr = built-in 5x7)
};

struct SimLcdRect {
  int16_t x, y, w, h;
  uint16_t c;
};

struct SimLcd {
  bool inited = false;
  bool asleep = false;
  uint8_t rotation = 0;
  uint32_t spiHz = 0;
  uint32_t fills = 0;
  std::vector<SimLcdText> texts;
  std::vector<SimLcdRect> rects;    // every fillRect, in order
  std::vector<SimLcdRect> rounds;   // every fillRoundRect (bounding box), in order
  std::vector<SimLcdRect> hlines;   // every drawFastHLine: x, y, w, h = 1 (icon runs)
  std::vector<SimLcdRect> circles;  // every fillCircle: x, y = centre, w = h = radius
  static constexpr int kW = 320, kH = 240;
  uint16_t fb[kH][kW] = {};
  // 24-bit BMP (RGB565 expanded to 8 bits per channel, rows bottom-up). False on I/O error.
  bool saveBmp(const char *path) const;
  bool printed(const std::string &s) const {
    for (const SimLcdText &t : texts)
      if (t.s.find(s) != std::string::npos) return true;
    return false;
  }
  bool printedExact(const std::string &s) const {
    for (const SimLcdText &t : texts)
      if (t.s == s) return true;
    return false;
  }
};
