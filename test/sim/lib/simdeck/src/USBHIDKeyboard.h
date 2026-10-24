#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sim/host.h"

// Report building copied from Arduino-ESP32 core 3.x libraries/USB/src/USBHIDKeyboard.cpp
// (press/release/pressRaw/releaseRaw/releaseAll). Every change sends a report, as the core does.
#define KEY_LEFT_CTRL 0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT 0x82
#define KEY_LEFT_GUI 0x83

class USBHIDKeyboard {
 public:
  void begin();
  size_t press(uint8_t k);
  size_t release(uint8_t k);
  size_t pressRaw(uint8_t k);
  size_t releaseRaw(uint8_t k);
  void releaseAll();

 private:
  void send();
  KbReport report_ = {0, {0, 0, 0, 0, 0, 0}};
};
