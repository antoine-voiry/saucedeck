#pragma once
#include <stdint.h>

// LEDs + LCD fade out when the deck is idle: full for kFadeStartMs after the last input,
// then linear to 0 at kOffMs. Input wakes instantly (and still acts, see Deck).
class IdleDimmer {
 public:
  static constexpr uint32_t kFadeStartMs = 120000;
  static constexpr uint32_t kOffMs = 240000;

  void onActivity(uint32_t nowMs) { last_ = nowMs; }

  uint8_t level(uint32_t nowMs) const {
    uint32_t e = nowMs - last_;
    if (e < kFadeStartMs) return 255;
    if (e >= kOffMs) return 0;
    return static_cast<uint8_t>(255u * (kOffMs - e) / (kOffMs - kFadeStartMs));
  }

 private:
  uint32_t last_ = 0;
};
