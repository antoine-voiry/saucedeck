#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// ST7789 via the Adafruit API: records what was printed (plan 2 adds a framebuffer).
struct SimLcdText {
  int16_t x, y;
  uint8_t size;
  uint16_t fg;
  std::string s;
};

struct SimLcd {
  bool inited = false;
  bool asleep = false;
  uint8_t rotation = 0;
  uint32_t spiHz = 0;
  uint32_t fills = 0;
  std::vector<SimLcdText> texts;
  bool printed(const std::string &s) const {
    for (const SimLcdText &t : texts)
      if (t.s.find(s) != std::string::npos) return true;
    return false;
  }
};
