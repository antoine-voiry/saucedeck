#pragma once
#include <stdint.h>

// Hardware-free quadrature decoder for EC11 encoders.
// Feed raw phase levels; returns +1 / -1 once per full detent (4 transitions), else 0.
class QuadDecoder {
 public:
  QuadDecoder(uint8_t a, uint8_t b) : state_((a << 1) | b) {}

  int step(uint8_t a, uint8_t b) {
    static const int8_t kTable[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    uint8_t s = ((a & 1) << 1) | (b & 1);
    acc_ += kTable[(state_ << 2) | s];
    state_ = s;
    if (acc_ >= 4)  { acc_ = 0; return 1; }
    if (acc_ <= -4) { acc_ = 0; return -1; }
    return 0;
  }

 private:
  uint8_t state_;
  int8_t acc_ = 0;
};
