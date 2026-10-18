#pragma once
#include <stdint.h>

enum : uint8_t {
  CHORD_NONE = 0,
  CHORD_KNOB1 = 1u << 0,  // knob 1 switch pressed and released alone
  CHORD_KNOB2 = 1u << 1,  // knob 2 switch pressed and released alone
  CHORD_SYNC = 1u << 2,   // both switches held
};

// Knob push-switch chord detector. Feed the debounced deck mask every scan.
// A single knob fires on release, so starting a two-knob press never also fires a
// single-knob action (upstream SauceDeck fired on press and could mute before syncing).
// Once both are held, nothing else fires until both are released.
class KnobChord {
 public:
  KnobChord(uint16_t sw1, uint16_t sw2) : sw1_(sw1), sw2_(sw2) {}

  uint8_t update(uint16_t pressed) {
    bool a = pressed & sw1_;
    bool b = pressed & sw2_;
    uint8_t out = CHORD_NONE;
    if (a && b && !chord_) {
      chord_ = true;
      out |= CHORD_SYNC;
    }
    if (!chord_) {
      if (wasA_ && !a) out |= CHORD_KNOB1;
      if (wasB_ && !b) out |= CHORD_KNOB2;
    }
    if (!a && !b) chord_ = false;
    wasA_ = a;
    wasB_ = b;
    return out;
  }

 private:
  uint16_t sw1_, sw2_;
  bool wasA_ = false, wasB_ = false, chord_ = false;
};
