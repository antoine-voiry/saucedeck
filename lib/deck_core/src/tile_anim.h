#pragma once
#include <stdint.h>

// Legend tile press feedback, hardware-free. A held key's tile is filled (level kFull);
// on release it fades back over kFull - 1 steps of kStepMs. "Failed" = the press could not
// run (helper not running): same animation, warning colour. The glue repaints the tiles
// tick() names and reads level()/failed() for each.
class TileAnim {
 public:
  static constexpr uint8_t kTiles = 12;
  static constexpr uint8_t kFull = 4;
  static constexpr uint32_t kStepMs = 60;

  void onKeys(uint16_t held, uint16_t failed, uint32_t nowMs) {
    held &= kMask;
    uint16_t down = held & static_cast<uint16_t>(~held_);
    uint16_t up = held_ & static_cast<uint16_t>(~held);
    failed_ = static_cast<uint16_t>((failed_ & ~down) | (failed & held));
    for (uint8_t i = 0; i < kTiles; i++)
      if (up & (1u << i)) releasedAt_[i] = nowMs;
    fading_ = static_cast<uint16_t>((fading_ | up) & ~held);
    held_ = held;
  }

  uint16_t tick(uint32_t nowMs) {
    uint16_t changed = 0;
    for (uint8_t i = 0; i < kTiles; i++) {
      uint16_t bit = 1u << i;
      uint8_t lv = 0;
      if (held_ & bit) {
        lv = kFull;
      } else if (fading_ & bit) {
        uint32_t step = (nowMs - releasedAt_[i]) / kStepMs;
        lv = step >= kFull - 1u ? 0 : static_cast<uint8_t>(kFull - 1 - step);
        if (lv == 0) fading_ = static_cast<uint16_t>(fading_ & ~bit);
      }
      uint8_t look = lv ? static_cast<uint8_t>(lv | ((failed_ & bit) ? 0x80 : 0)) : 0;
      if (look != look_[i]) changed |= bit;
      look_[i] = look;
    }
    return changed;
  }

  uint8_t level(uint8_t i) const { return i < kTiles ? look_[i] & 0x7F : 0; }
  bool failed(uint8_t i) const { return i < kTiles && (failed_ & (1u << i)); }

 private:
  static constexpr uint16_t kMask = (1u << kTiles) - 1;
  uint16_t held_ = 0, fading_ = 0, failed_ = 0;
  uint32_t releasedAt_[kTiles] = {};
  uint8_t look_[kTiles] = {};  // level | 0x80 when failed, as of the last tick()
};

// RGB565 mix: num/den of b, the rest of a (per channel, rounded down).
inline uint16_t blend565(uint16_t a, uint16_t b, uint8_t num, uint8_t den) {
  if (den == 0 || num >= den) return num ? b : a;
  uint32_t keep = den - num;
  uint32_t r = ((a >> 11) * keep + (b >> 11) * num) / den;
  uint32_t g = (((a >> 5) & 0x3F) * keep + ((b >> 5) & 0x3F) * num) / den;
  uint32_t bl = ((a & 0x1F) * keep + (b & 0x1F) * num) / den;
  return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}
