#pragma once
#include <stddef.h>
#include <stdint.h>
#include "debounce.h"
#include "knob_chord.h"
#include "mcp23017.h"
#include "volume_state.h"

// HID-agnostic actions. src/ maps them to real reports (F13+index, consumer codes, chords).
// Sync is local only (state re-aligned, no HID report); src/ uses it for feedback.
enum class ActionType : uint8_t { KeyDown, KeyUp, ReleaseAll, Consumer, MicMute, Sync };
enum class ConsumerCode : uint8_t { Mute, VolumeUp, VolumeDown, NextTrack, PrevTrack };

struct Action {
  ActionType type;
  uint8_t value;  // key index 0..11 for KeyDown/KeyUp, ConsumerCode for Consumer
};

struct ActionList {
  static constexpr size_t kMax = 32;
  Action items[kMax];
  size_t count = 0;
  void push(ActionType t, uint8_t v = 0) {
    if (count < kMax) items[count++] = {t, v};
  }
};

// Deck behaviour, hardware-free: debounced keys -> F-key indices, knob switches ->
// mute / mic mute / sync, rotation -> one consumer report per detent.
// State is open loop (HID cannot read the host back); sync() re-aligns it.
class Deck {
 public:
  explicit Deck(uint8_t volumeSteps = 16) : speaker_(volumeSteps) {}

  void onKeys(uint16_t pressedRaw, uint32_t nowMs, ActionList &out) {
    busLost_ = false;
    failing_ = false;
    uint16_t now = debounce_.update(pressedRaw, nowMs);
    uint16_t changed = (now ^ held_) & DECK_KEYS;
    for (uint8_t i = 0; i < 12; i++) {
      uint16_t bit = 1u << i;
      if (changed & bit) out.push((now & bit) ? ActionType::KeyDown : ActionType::KeyUp, i);
    }
    held_ = now;

    uint8_t chord = chord_.update(now, nowMs);
    if (chord & CHORD_KNOB1) {
      out.push(ActionType::Consumer, static_cast<uint8_t>(ConsumerCode::Mute));
      speaker_.toggleMute();
    }
    if (chord & CHORD_KNOB2) {
      out.push(ActionType::MicMute);
      micMuted_ = !micMuted_;
    }
    if (chord & CHORD_SYNC) {  // user set host volume to 0 % and mic unmuted first
      speaker_.sync();
      micMuted_ = false;
      out.push(ActionType::Sync);
    }
  }

  void onEncoders(int knob1, int knob2, ActionList &out) {
    // Track only the steps that fit in the list, so the display matches what the host got
    int room = static_cast<int>(ActionList::kMax - out.count);
    if (knob1 > room) knob1 = room;
    if (knob1 < -room) knob1 = -room;
    speaker_.rotate(knob1);
    pushSteps(knob1, ConsumerCode::VolumeUp, ConsumerCode::VolumeDown, out);
    pushSteps(knob2, ConsumerCode::NextTrack, ConsumerCode::PrevTrack, out);
  }

  // An expander read failed. One glitch (bad header contact) must not release held keys,
  // or the host sees a double press; only kBusLostMs of continuous failure counts as lost.
  static constexpr uint32_t kBusLostMs = 20;
  void onReadFailed(uint32_t nowMs, ActionList &out) {
    if (!failing_) {
      failing_ = true;
      failSince_ = nowMs;
    }
    if (nowMs - failSince_ >= kBusLostMs) onBusLost(out);
  }

  const VolumeState &speaker() const { return speaker_; }
  bool micMuted() const { return micMuted_; }

 private:
  // Expander stopped answering: drop everything held so the host never sees a stuck key.
  // Knob state is reset without firing; held keys press again once the bus is back.
  void onBusLost(ActionList &out) {
    if (busLost_) return;
    busLost_ = true;
    out.push(ActionType::ReleaseAll);
    debounce_ = MaskDebouncer();
    chord_ = KnobChord(DECK_ENC1_SW, DECK_ENC2_SW);
    held_ = 0;
  }

  static void pushSteps(int n, ConsumerCode up, ConsumerCode down, ActionList &out) {
    for (; n > 0; n--) out.push(ActionType::Consumer, static_cast<uint8_t>(up));
    for (; n < 0; n++) out.push(ActionType::Consumer, static_cast<uint8_t>(down));
  }

  MaskDebouncer debounce_;
  KnobChord chord_{DECK_ENC1_SW, DECK_ENC2_SW};
  VolumeState speaker_;
  uint16_t held_ = 0;
  bool micMuted_ = false;
  bool busLost_ = false;
  bool failing_ = false;
  uint32_t failSince_ = 0;
};
