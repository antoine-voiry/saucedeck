#pragma once
#include <stdint.h>

// Per-bit debouncer for an active-high pressed mask (deck bits).
// A bit takes its new level only after the raw level has held for settleMs.
// Time is passed in (millis()); unsigned subtraction keeps it correct across wrap.
class MaskDebouncer {
 public:
  explicit MaskDebouncer(uint32_t settleMs = 25) : settleMs_(settleMs) {}

  uint16_t update(uint16_t raw, uint32_t nowMs) {
    uint16_t changed = raw ^ last_;
    last_ = raw;
    for (int i = 0; i < 16; i++) {
      uint16_t bit = 1u << i;
      if (changed & bit) {
        since_[i] = nowMs;
      } else if (((raw ^ stable_) & bit) && nowMs - since_[i] >= settleMs_) {
        stable_ ^= bit;
      }
    }
    return stable_;
  }

  uint16_t stable() const { return stable_; }

 private:
  uint32_t settleMs_;
  uint16_t last_ = 0;
  uint16_t stable_ = 0;
  uint32_t since_[16] = {};
};
