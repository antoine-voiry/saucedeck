#pragma once
#include <stdint.h>
#include "helper_proto.h"

// What the deck knows about the Mac helper: is it running (HELLO keep-alive every second,
// gone after kTimeoutMs of silence or a USB reset) and the last real volume / mic state.
class HostLink {
 public:
  static constexpr uint32_t kTimeoutMs = 3000;

  void onMessage(const HostMsg &m, uint32_t nowMs) {
    if (m.type == MSG_HELLO) hello_ = true;
    lastMs_ = nowMs;
    if (m.type == MSG_VOLUME) {
      vol_ = m.a;
      volMuted_ = m.b;
      hasVol_ = true;
    }
    if (m.type == MSG_MIC) {
      mic_ = m.a;
      micMuted_ = m.b;
      hasMic_ = true;
    }
  }

  void onUsbReset() { *this = HostLink(); }

  bool present(uint32_t nowMs) const { return hello_ && nowMs - lastMs_ < kTimeoutMs; }
  bool hasVolume() const { return hasVol_; }
  uint8_t volumePercent() const { return vol_; }
  bool volumeMuted() const { return volMuted_; }
  bool hasMic() const { return hasMic_; }
  uint8_t micPercent() const { return mic_; }
  bool micMuted() const { return micMuted_; }

 private:
  bool hello_ = false, hasVol_ = false, hasMic_ = false, volMuted_ = false, micMuted_ = false;
  uint8_t vol_ = 0, mic_ = 0;
  uint32_t lastMs_ = 0;
};
