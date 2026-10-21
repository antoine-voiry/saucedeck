#pragma once
#include <stdint.h>

enum class WifiState : uint8_t { Off, Starting, Portal, Joined };

// Wi-Fi is off by default; a 5 s two-knob hold toggles it, and it always turns itself off
// after kOnMs so a forgotten session doesn't leave the portal (and the radio) up.
class WifiSession {
 public:
  static constexpr uint32_t kOnMs = 600000;

  void toggle(uint32_t nowMs) {
    if (state_ == WifiState::Off) {
      state_ = WifiState::Starting;
      since_ = nowMs;
    } else {
      state_ = WifiState::Off;
    }
  }
  void onPortal() {
    if (state_ == WifiState::Starting) state_ = WifiState::Portal;
  }
  void onJoined() {
    if (state_ == WifiState::Starting || state_ == WifiState::Portal) state_ = WifiState::Joined;
  }
  void onFailed() { state_ = WifiState::Off; }

  bool tick(uint32_t nowMs) {
    if (state_ != WifiState::Off && nowMs - since_ >= kOnMs) {
      state_ = WifiState::Off;
      return true;
    }
    return false;
  }

  WifiState state() const { return state_; }
  bool on() const { return state_ != WifiState::Off; }

 private:
  WifiState state_ = WifiState::Off;
  uint32_t since_ = 0;
};
