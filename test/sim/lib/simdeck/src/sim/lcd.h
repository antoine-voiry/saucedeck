#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// ST7789 via the Adafruit API: records printed text and filled rects (no framebuffer).
struct SimLcdText {
  int16_t x, y;
  uint8_t size;
  uint16_t fg;
  std::string s;
  bool customFont;  // printed with setFont(&FreeSans9pt7b) (legend tiles)
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
  std::vector<SimLcdRect> rects;  // every fillRect, in order
  bool printed(const std::string &s) const {
    for (const SimLcdText &t : texts)
      if (t.s.find(s) != std::string::npos) return true;
    return false;
  }
};
