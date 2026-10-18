#pragma once
#include <stdint.h>

// Volume screen model, hardware-free. The glue redraws only what the dirty flags name,
// and only the strip of the bar that changed: a full clear like upstream SauceDeck
// flickers and stalls the polled encoders while SPI is busy.

struct ScreenState {
  uint8_t volumePercent;
  bool speakerMuted;
  bool micMuted;
};

enum : uint8_t {
  DIRTY_NONE = 0,
  DIRTY_BAR = 1u << 0,        // fill level changed: redraw barDelta() strip
  DIRTY_BAR_COLOR = 1u << 1,  // mute changed: repaint whole bar interior
  DIRTY_VOLUME_TEXT = 1u << 2,
  DIRTY_SPEAKER_ICON = 1u << 3,
  DIRTY_MIC = 1u << 4,
  DIRTY_ALL = 0x1F,
};

struct Strip {
  uint16_t y;
  uint16_t h;
  bool filled;  // true = paint fill color, false = paint background
};

// Filled pixels of a bar interior innerH tall, rounded to nearest.
inline uint16_t barFill(uint8_t percent, uint16_t innerH) {
  uint32_t p = percent > 100 ? 100 : percent;
  return static_cast<uint16_t>((p * innerH + 50) / 100);
}

// Strip to repaint when the fill goes from oldFill to newFill. Bar grows upward from
// the bottom of the interior (innerTop + innerH).
inline Strip barDelta(uint16_t oldFill, uint16_t newFill, uint16_t innerTop, uint16_t innerH) {
  uint16_t hi = oldFill > newFill ? oldFill : newFill;
  uint16_t lo = oldFill > newFill ? newFill : oldFill;
  return {static_cast<uint16_t>(innerTop + innerH - hi), static_cast<uint16_t>(hi - lo),
          newFill > oldFill};
}

class ScreenModel {
 public:
  uint8_t update(const ScreenState &next) {
    uint8_t dirty = DIRTY_NONE;
    if (first_) {
      dirty = DIRTY_ALL;
      first_ = false;
    } else {
      if (next.volumePercent != cur_.volumePercent) dirty |= DIRTY_BAR | DIRTY_VOLUME_TEXT;
      if (next.speakerMuted != cur_.speakerMuted) dirty |= DIRTY_BAR_COLOR | DIRTY_SPEAKER_ICON;
      if (next.micMuted != cur_.micMuted) dirty |= DIRTY_MIC;
    }
    prev_ = cur_;
    cur_ = next;
    return dirty;
  }

  void invalidate() { first_ = true; }
  const ScreenState &current() const { return cur_; }
  const ScreenState &previous() const { return prev_; }

 private:
  ScreenState cur_{}, prev_{};
  bool first_ = true;
};
