#pragma once
#include <stdint.h>

// ESP32-S3 GPIO levels as the firmware reads them.
// Model choices: an input nothing drives reads 1 with a pull-up, else 0 (real pin floats).
struct SimGpio {
  static constexpr int kPins = 49;  // GPIO0..48
  enum Mode : uint8_t { In, Out, InPullup };
  Mode mode[kPins] = {};
  bool out[kPins] = {};
  bool pullup[kPins] = {};
  int8_t ext[kPins];  // level driven from outside (switch, encoder), -1 = none

  SimGpio() {
    for (int i = 0; i < kPins; i++) ext[i] = -1;
  }
  int read(int pin) const {
    if (pin < 0 || pin >= kPins) return 0;
    if (mode[pin] == Out) return out[pin] ? 1 : 0;
    if (ext[pin] >= 0) return ext[pin];
    return (pullup[pin] || mode[pin] == InPullup) ? 1 : 0;
  }
};
