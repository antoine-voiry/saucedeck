#pragma once
#include <stdint.h>

// Edge events between two reads. Inputs are active-low (0 = pressed).
struct ButtonEvents {
  uint16_t pressed;
  uint16_t released;
};

inline ButtonEvents diffButtons(uint16_t prev, uint16_t now) {
  uint16_t changed = prev ^ now;
  return {static_cast<uint16_t>(changed & ~now), static_cast<uint16_t>(changed & now)};
}
