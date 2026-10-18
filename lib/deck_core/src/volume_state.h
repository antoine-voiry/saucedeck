#pragma once
#include <stdint.h>

// Open-loop mirror of the host speaker volume (HID cannot read it back).
// Level is counted in host volume steps (one VOL+/VOL- report = one step), so the
// display never drifts by rounding; maxSteps is host-specific (macOS 16, unverified).
// Re-align with the host via sync() after the user turns the host to 0 %.
class VolumeState {
 public:
  explicit VolumeState(uint8_t maxSteps = 16) : max_(maxSteps ? maxSteps : 1) {}

  void rotate(int detents) {
    if (detents > 0) muted_ = false;  // VOL+ unmutes (upstream SauceDeck behaviour)
    int s = steps_ + detents;
    steps_ = s < 0 ? 0 : (s > max_ ? max_ : static_cast<uint8_t>(s));
  }

  void toggleMute() { muted_ = !muted_; }

  void sync() {
    steps_ = 0;
    muted_ = false;
  }

  uint8_t steps() const { return steps_; }
  uint8_t percent() const { return static_cast<uint8_t>((steps_ * 100u + max_ / 2) / max_); }
  bool muted() const { return muted_; }

 private:
  uint8_t max_;
  uint8_t steps_ = 0;
  bool muted_ = false;
};
