#pragma once
#include <stdint.h>

// Arduino SPIClass subset for display.cpp (plan 4 adds byte transfers for the bringup driver).
class SPIClass {
 public:
  void begin(int8_t sck, int8_t miso, int8_t mosi, int8_t ss);
  int8_t sck = -1, miso = -1, mosi = -1, ss = -1;
};
extern SPIClass SPI;
