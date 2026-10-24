#pragma once
#include <stdint.h>
#include "Arduino.h"

// Adafruit NeoPixel 1.15.5 API subset; frame lands in board().leds on show().
typedef uint16_t neoPixelType;
#define NEO_GRB ((1 << 6) | (1 << 4) | (0 << 2) | (2))
#define NEO_KHZ800 0x0000

class Adafruit_NeoPixel {
 public:
  Adafruit_NeoPixel(uint16_t n, int16_t pin, neoPixelType type);
  void begin();
  void setBrightness(uint8_t b);
  void clear();
  void show();
  void setPixelColor(uint16_t i, uint8_t r, uint8_t g, uint8_t b);
};
