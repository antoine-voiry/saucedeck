#pragma once
#include <stdint.h>

// Wall clock for the top bar, hardware-free. The helper sends TIME (UTC + offset) every
// 60 s; between messages the deck counts on millis(). Before the first TIME: unknown.
class DeckClock {
 public:
  void onTime(uint32_t epoch, int16_t tzOffsetMin, uint32_t nowMs) {
    epoch_ = epoch;
    tz_ = tzOffsetMin;
    atMs_ = nowMs;
    known_ = true;
  }

  bool known() const { return known_; }

  // Local minute of the day 0..1439, or -1 before the first TIME.
  int16_t minuteOfDay(uint32_t nowMs) const {
    if (!known_) return -1;
    int64_t local = static_cast<int64_t>(epoch_) + (nowMs - atMs_) / 1000 + static_cast<int64_t>(tz_) * 60;
    int64_t m = (local / 60) % 1440;
    if (local < 0 && local % 60 != 0) m -= 1;  // floor for times before the epoch
    if (m < 0) m += 1440;
    return static_cast<int16_t>(m);
  }

 private:
  uint32_t epoch_ = 0, atMs_ = 0;
  int16_t tz_ = 0;
  bool known_ = false;
};

// "HH:MM", or "--:--" outside 0..1439 (unknown clock).
inline void formatHhmm(int16_t minuteOfDay, char out[6]) {
  if (minuteOfDay < 0 || minuteOfDay >= 1440) {
    out[0] = out[1] = out[3] = out[4] = '-';
  } else {
    int h = minuteOfDay / 60, m = minuteOfDay % 60;
    out[0] = static_cast<char>('0' + h / 10);
    out[1] = static_cast<char>('0' + h % 10);
    out[3] = static_cast<char>('0' + m / 10);
    out[4] = static_cast<char>('0' + m % 10);
  }
  out[2] = ':';
  out[5] = 0;
}
