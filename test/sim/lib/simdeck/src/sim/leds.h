#pragma once
#include <stdint.h>
#include <vector>

// WS2812 strip via the Adafruit NeoPixel API: last shown frame.
struct SimRgb {
  uint8_t r, g, b;
};

struct SimLeds {
  uint16_t count = 0;
  int16_t pin = -1;
  uint8_t brightness = 255;
  uint32_t shows = 0;
  std::vector<SimRgb> pending, shown;
};
