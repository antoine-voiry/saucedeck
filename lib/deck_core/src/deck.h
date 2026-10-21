#pragma once
#include <stddef.h>
#include <stdint.h>
#include "debounce.h"
#include "keymap.h"
#include "knob_chord.h"
#include "mcp23017.h"
#include "volume_state.h"

// HID-agnostic actions. src/ maps them to real reports (F13+index, consumer codes, chords).
// Sync is local only (state re-aligned, no HID report); src/ uses it for feedback.
enum class ActionType : uint8_t {
  KeyDown, KeyUp, ReleaseAll, Consumer, MicMute, Sync,
  HelperAction, HelperMissing, HelperMicMute, MicUp, MicDown, WifiToggle, KeyMapCommitted, KeyMapDiscarded,
};
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
      if (!(changed & bit)) continue;
      bool down = now & bit;
      if (keymap_.entry(i).type == KeyType::Keystroke) {
        out.push(down ? ActionType::KeyDown : ActionType::KeyUp, i);
      } else if (down) {  // app / URL / Shortcut: one-shot, run by the Mac helper
        out.push(helper_ ? ActionType::HelperAction : ActionType::HelperMissing, i);
      }
    }
    if ((now & DECK_ENC2_SW) && !(held_ & DECK_ENC2_SW)) {  // knob 2 pressed: gesture starts
      k2Since_ = nowMs;
      k2Turned_ = false;
      k2MissingSent_ = false;
    }
    held_ = now;

    uint8_t chord = chord_.update(now, nowMs);
    if (chord & CHORD_KNOB1) {
      out.push(ActionType::Consumer, static_cast<uint8_t>(ConsumerCode::Mute));
      speaker_.toggleMute();
    }
    if ((chord & CHORD_KNOB2) && !k2Turned_) {  // push+turn adjusted the mic: no mute on release
      if (helper_) {
        out.push(ActionType::HelperMicMute);  // real system mic mute via the helper
      } else {
        out.push(ActionType::MicMute);  // fallback: app shortcut, local indicator
        micMuted_ = !micMuted_;
      }
    }
    if (chord & CHORD_SYNC) {  // user set host volume to 0 % and mic unmuted first
      speaker_.sync();
      micMuted_ = false;
      out.push(ActionType::Sync);
    }
    if (chord & CHORD_WIFI) out.push(ActionType::WifiToggle);
  }

  static constexpr uint32_t kMicTurnAfterMs = 150;

  void onEncoders(int knob1, int knob2, uint32_t nowMs, ActionList &out) {
    // Track only the steps that fit in the list, so the display matches what the host got
    int room = static_cast<int>(ActionList::kMax - out.count);
    if (knob1 > room) knob1 = room;
    if (knob1 < -room) knob1 = -room;
    speaker_.rotate(knob1);
    pushSteps(knob1, ConsumerCode::VolumeUp, ConsumerCode::VolumeDown, out);

    if (!(held_ & DECK_ENC2_SW)) {  // knob 2 turned alone: track skip
      pushSteps(knob2, ConsumerCode::NextTrack, ConsumerCode::PrevTrack, out);
      return;
    }
    // Knob 2 held + turned = mic level. Ignore counts right after the press (shaft nudge).
    if (knob2 == 0 || nowMs - k2Since_ < kMicTurnAfterMs) return;
    k2Turned_ = true;
    if (!helper_) {
      if (!k2MissingSent_) out.push(ActionType::HelperMissing, 0xFF);
      k2MissingSent_ = true;
      return;
    }
    for (; knob2 > 0; knob2--) out.push(ActionType::MicUp);
    for (; knob2 < 0; knob2++) out.push(ActionType::MicDown);
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

  void setKeyMap(const KeyMap &m) { keymap_ = m; }
  const KeyMap &keyMap() const { return keymap_; }
  void setHelperPresent(bool p) { helper_ = p; }
  uint32_t chordHeldMs(uint32_t nowMs) const { return chord_.bothHeldMs(nowMs); }

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
    k2Turned_ = false;
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
  KeyMap keymap_;
  bool helper_ = false;
  uint32_t k2Since_ = 0;
  bool k2Turned_ = false, k2MissingSent_ = false;
  uint32_t failSince_ = 0;
};
