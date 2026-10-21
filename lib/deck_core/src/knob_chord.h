#pragma once
#include <stdint.h>

enum : uint8_t {
  CHORD_NONE = 0,
  CHORD_KNOB1 = 1u << 0,  // knob 1 switch pressed and released alone
  CHORD_KNOB2 = 1u << 1,  // knob 2 switch pressed and released alone
  CHORD_SYNC = 1u << 2,   // both held, chord ended before kWifiHoldMs
  CHORD_WIFI = 1u << 3,   // both held for kWifiHoldMs (fires once, while held)
};

// Knob push-switch chord detector. Feed the debounced deck mask every scan.
// Single knobs fire on release; a two-knob chord decides on duration: released early =
// SYNC, held to 5 s = WIFI. After the chord starts nothing else fires until both are up.
class KnobChord {
 public:
  static constexpr uint32_t kWifiHoldMs = 5000;

  KnobChord(uint16_t sw1, uint16_t sw2) : sw1_(sw1), sw2_(sw2) {}

  uint8_t update(uint16_t pressed, uint32_t nowMs) {
    bool a = pressed & sw1_;
    bool b = pressed & sw2_;
    uint8_t out = CHORD_NONE;
    if (a && b && !chord_) {
      chord_ = true;
      wifiFired_ = false;
      timing_ = true;
      since_ = nowMs;
    }
    if (chord_ && timing_ && !(a && b)) timing_ = false;  // first knob up: clock stops
    if (chord_ && timing_ && !wifiFired_ && nowMs - since_ >= kWifiHoldMs) {
      wifiFired_ = true;
      out |= CHORD_WIFI;
    }
    if (!chord_) {
      if (wasA_ && !a) out |= CHORD_KNOB1;
      if (wasB_ && !b) out |= CHORD_KNOB2;
    }
    if (chord_ && !a && !b) {
      if (!wifiFired_) out |= CHORD_SYNC;
      chord_ = false;
    }
    wasA_ = a;
    wasB_ = b;
    held_ = a && b;
    return out;
  }

  // For the LCD countdown: how long both have been held, 0 if not both held.
  uint32_t bothHeldMs(uint32_t nowMs) const { return (chord_ && timing_ && held_) ? nowMs - since_ : 0; }

 private:
  uint16_t sw1_, sw2_;
  bool wasA_ = false, wasB_ = false, chord_ = false, wifiFired_ = false, timing_ = false, held_ = false;
  uint32_t since_ = 0;
};
