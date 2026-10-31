#pragma once
#include <stdint.h>

// Screen model, hardware-free: top bar, 4x3 legend tiles, status strip (volume bar, mic,
// banner line). The glue redraws only what the dirty flags name, only the tiles in
// tileMask(), and only the span of the bar that changed: a full clear flickers and stalls
// the key scan while SPI is busy (~61 ms per full frame at 20 MHz).

enum : uint8_t {
  BANNER_NONE, BANNER_WIFI_COUNTDOWN, BANNER_WIFI_STARTING, BANNER_WIFI_PORTAL, BANNER_WIFI_JOINED,
  BANNER_APPLY, BANNER_HELPER_MISSING, BANNER_KEYMAP_RESET, BANNER_WIFI_FAILED, BANNER_INVALID_KEY,
  BANNER_BROWNOUT,
};

struct ScreenState {
  uint8_t volumePercent;
  bool speakerMuted;
  bool micMuted;
  bool micLevelKnown;  // helper connected: show real mic %
  uint8_t micPercent;
  uint8_t banner;      // BANNER_*
  uint8_t bannerArg;   // countdown seconds, key number, ...
  uint8_t keymapGen;   // bumped when a key map goes live: every tile repaints
  bool helperPresent;  // top bar dot
  bool wifiOn;         // top bar dot
};

enum : uint16_t {
  DIRTY_NONE = 0,
  DIRTY_BAR = 1u << 0,          // fill level changed: redraw barDeltaH() span
  DIRTY_BAR_COLOR = 1u << 1,    // mute changed: repaint whole bar interior
  DIRTY_VOLUME_TEXT = 1u << 2,  // "62%" / "MUTED"
  DIRTY_MIC = 1u << 3,
  DIRTY_BANNER = 1u << 4,       // strip line 2 (Wi-Fi, Apply?, helper missing, ...)
  DIRTY_TOPBAR = 1u << 5,       // helper / Wi-Fi dots
  DIRTY_TILES = 1u << 6,        // repaint the tiles in tileMask()
  DIRTY_ALL = 0x7F,
};

struct Span {
  uint16_t x;
  uint16_t w;
  bool filled;  // true = paint fill color, false = paint background
};

// Filled pixels of a bar interior innerH tall, rounded to nearest.
inline uint16_t barFill(uint8_t percent, uint16_t innerH) {
  uint32_t p = percent > 100 ? 100 : percent;
  return static_cast<uint16_t>((p * innerH + 50) / 100);
}

// Span to repaint when the fill goes from oldFill to newFill. Bar grows rightward from
// innerLeft.
inline Span barDeltaH(uint16_t oldFill, uint16_t newFill, uint16_t innerLeft) {
  uint16_t hi = oldFill > newFill ? oldFill : newFill;
  uint16_t lo = oldFill > newFill ? newFill : oldFill;
  return {static_cast<uint16_t>(innerLeft + lo), static_cast<uint16_t>(hi - lo), newFill > oldFill};
}

class ScreenModel {
 public:
  static constexpr uint16_t kAllTiles = 0x0FFF;

  uint16_t update(const ScreenState &next, uint16_t tileChanges) {
    uint16_t dirty = DIRTY_NONE;
    tiles_ = tileChanges & kAllTiles;
    if (first_) {
      dirty = DIRTY_ALL;
      tiles_ = kAllTiles;
      first_ = false;
    } else {
      if (next.volumePercent != cur_.volumePercent) dirty |= DIRTY_BAR | DIRTY_VOLUME_TEXT;
      if (next.speakerMuted != cur_.speakerMuted) dirty |= DIRTY_BAR_COLOR | DIRTY_VOLUME_TEXT;
      if (next.micMuted != cur_.micMuted) dirty |= DIRTY_MIC;
      if (next.micLevelKnown != cur_.micLevelKnown || next.micPercent != cur_.micPercent) dirty |= DIRTY_MIC;
      if (next.banner != cur_.banner || next.bannerArg != cur_.bannerArg) dirty |= DIRTY_BANNER;
      if (next.helperPresent != cur_.helperPresent || next.wifiOn != cur_.wifiOn) dirty |= DIRTY_TOPBAR;
      if (next.keymapGen != cur_.keymapGen) tiles_ = kAllTiles;
      if (tiles_) dirty |= DIRTY_TILES;
    }
    prev_ = cur_;
    cur_ = next;
    return dirty;
  }

  uint16_t tileMask() const { return tiles_; }  // bit i = key position i + 1
  void invalidate() { first_ = true; }
  const ScreenState &current() const { return cur_; }
  const ScreenState &previous() const { return prev_; }

 private:
  ScreenState cur_{}, prev_{};
  uint16_t tiles_ = 0;
  bool first_ = true;
};
