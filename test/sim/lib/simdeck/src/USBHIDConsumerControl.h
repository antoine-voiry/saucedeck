#pragma once
#include <stddef.h>
#include <stdint.h>

// Usage IDs: Arduino-ESP32 USBHIDConsumerControl.h (HID Usage Tables, Consumer page 0x0C).
#define CONSUMER_CONTROL_SCAN_NEXT 0x00B5
#define CONSUMER_CONTROL_SCAN_PREVIOUS 0x00B6
#define CONSUMER_CONTROL_MUTE 0x00E2
#define CONSUMER_CONTROL_VOLUME_INCREMENT 0x00E9
#define CONSUMER_CONTROL_VOLUME_DECREMENT 0x00EA

class USBHIDConsumerControl {
 public:
  void begin();
  size_t press(uint16_t k);
  size_t release();
};
