#pragma once
#include <stdint.h>

// Converts a hardware quadrature running count (ESP32-S3 PCNT, x4 decoding, overflow
// accumulated by the driver) into detents. EC11: 4 counts per detent, rest to rest.
// Hardware counts every edge even while the loop is busy drawing; this only divides.
class DetentCounter {
 public:
  explicit DetentCounter(int countsPerDetent = 4, int32_t startTotal = 0)
      : per_(countsPerDetent > 0 ? countsPerDetent : 1), last_(startTotal) {}

  int update(int32_t total) {
    acc_ += total - last_;
    last_ = total;
    int detents = 0;
    while (acc_ >= per_) { acc_ -= per_; detents++; }
    while (acc_ <= -per_) { acc_ += per_; detents--; }
    return detents;
  }

 private:
  int per_;
  int32_t last_;
  int32_t acc_ = 0;
};
