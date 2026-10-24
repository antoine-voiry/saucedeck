#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

// Arduino-ESP32 TwoWire subset. Transfers go to SimBoard::i2cWrite / i2cRead.
class TwoWire {
 public:
  bool begin(int sda, int scl, uint32_t frequency);
  void beginTransmission(uint8_t address);
  size_t write(uint8_t b);
  uint8_t endTransmission(bool sendStop = true);
  uint8_t requestFrom(uint8_t address, uint8_t quantity);
  int available();
  int read();

  int sda = -1, scl = -1;
  uint32_t frequency = 0;

 private:
  uint8_t txAddr_ = 0;
  std::vector<uint8_t> tx_, rx_;
  size_t rxPos_ = 0;
};
extern TwoWire Wire;
