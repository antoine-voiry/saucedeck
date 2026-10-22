#pragma once
#include <stddef.h>
#include <stdint.h>
#include "deck.h"

struct Rgb {
  uint8_t r, g, b;
};

struct LedInputs {
  uint8_t volumePercent;
  bool speakerMuted;
  bool micMuted;
};

// Full-saturation colour for hue 0..359 at brightness v.
inline Rgb hsvToRgb(uint16_t hue, uint8_t v) {
  hue %= 360;
  uint8_t sector = static_cast<uint8_t>(hue / 60);
  uint8_t up = static_cast<uint8_t>(v * (hue % 60) / 60);  // rising edge within the sector
  uint8_t down = static_cast<uint8_t>(v - up);
  switch (sector) {
    case 0: return {v, up, 0};
    case 1: return {down, v, 0};
    case 2: return {0, v, up};
    case 3: return {0, down, v};
    case 4: return {up, 0, v};
    default: return {v, 0, down};
  }
}

// Estimated strip current: ~20 mA per channel at 255, so 60 mA per LED full white
// (WS2812B, approx; docs/kb/peripherals.md).
inline uint32_t estimateMilliamps(const Rgb *px, size_t n) {
  uint32_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += px[i].r + px[i].g + px[i].b;
  return sum * 20 / 255;
}

// Scale the whole frame down so the estimate stays within budgetMa (USB 500 mA total).
inline void limitPower(Rgb *px, size_t n, uint32_t budgetMa) {
  uint32_t ma = estimateMilliamps(px, n);
  if (ma <= budgetMa) return;
  for (size_t i = 0; i < n; i++) {
    px[i].r = static_cast<uint8_t>(px[i].r * budgetMa / ma);
    px[i].g = static_cast<uint8_t>(px[i].g * budgetMa / ma);
    px[i].b = static_cast<uint8_t>(px[i].b * budgetMa / ma);
  }
}

// LED feedback per deck action, hardware-free. Idle = dim (red while mic muted),
// actions flash a colour that fades out, volume changes show a gauge for a while.
class LedEffects {
 public:
  static constexpr uint32_t kBudgetMa = 250;  // leaves room for ESP32 + LCD BL (estimate)
  static constexpr uint32_t kFlashMs = 250;
  static constexpr uint32_t kGaugeMs = 1500;
  static constexpr uint32_t kWifiBudgetMa = 30;
  void setWifi(bool on) { wifi_ = on; }
  void setDim(uint8_t level) { dim_ = level; }  // idle fade (IdleDimmer), after the power cap

  void onAction(const Action &a, uint32_t nowMs) {
    switch (a.type) {
      case ActionType::KeyDown: keyFlash(a.value, nowMs); break;
      case ActionType::MicMute: flash({255, 0, 0}, nowMs); break;
      case ActionType::Sync: flash({0, 255, 0}, nowMs); break;
      case ActionType::ReleaseAll: flash({255, 40, 0}, nowMs); break;  // bus lost
      case ActionType::Consumer:
        switch (static_cast<ConsumerCode>(a.value)) {
          case ConsumerCode::VolumeUp:
          case ConsumerCode::VolumeDown:
            gauge_ = true;
            gaugeStart_ = nowMs;
            break;
          case ConsumerCode::Mute: flash({255, 120, 0}, nowMs); break;
          case ConsumerCode::NextTrack:
          case ConsumerCode::PrevTrack: flash({200, 200, 200}, nowMs); break;
        }
        break;
      case ActionType::HelperAction: keyFlash(a.value, nowMs); break;
      case ActionType::HelperMissing: flash({255, 40, 0}, nowMs); break;
      case ActionType::HelperMicMute: flash({255, 0, 0}, nowMs); break;
      case ActionType::KeyMapCommitted: flash({0, 255, 0}, nowMs); break;
      case ActionType::KeyUp:
      case ActionType::MicUp:
      case ActionType::MicDown:
      case ActionType::WifiToggle:
      case ActionType::KeyMapDiscarded: break;
    }
  }

  void render(const LedInputs &in, uint32_t nowMs, Rgb *px, size_t n) {
    if (flash_ && nowMs - flashStart_ >= kFlashMs) flash_ = false;
    if (wifi_) {  // Wi-Fi TX peaks need the USB budget: blue pulse + flashes, capped at 30 mA
      uint32_t ph = nowMs % 1000;
      uint8_t lvl = static_cast<uint8_t>((ph < 500 ? ph : 1000 - ph) * 40 / 500);
      for (size_t i = 0; i < n; i++) px[i] = flashAt(i, nowMs);
      if (n) px[0] = {px[0].r, px[0].g, max(px[0].b, lvl)};
      limitPower(px, n, kWifiBudgetMa);
      applyDim(px, n);
      return;
    }
    if (gauge_ && nowMs - gaugeStart_ >= kGaugeMs) gauge_ = false;

    const Rgb base = in.micMuted ? Rgb{4, 0, 0} : Rgb{0, 1, 3};  // bench: brighter was too much
    size_t lit = (static_cast<size_t>(in.volumePercent > 100 ? 100 : in.volumePercent) * n + 50) / 100;
    const Rgb gaugeOn = in.speakerMuted ? Rgb{60, 60, 60} : Rgb{0, 180, 180};

    for (size_t i = 0; i < n; i++) {
      Rgb p = gauge_ ? (i < lit ? gaugeOn : Rgb{0, 0, 0}) : base;
      Rgb f = flashAt(i, nowMs);
      px[i] = {max(p.r, f.r), max(p.g, f.g), max(p.b, f.b)};
    }
    limitPower(px, n, kBudgetMa);
    applyDim(px, n);
  }

 private:

  static uint8_t scale(uint8_t c, uint32_t left) { return static_cast<uint8_t>(c * left / kFlashMs); }
  static uint8_t max(uint8_t a, uint8_t b) { return a > b ? a : b; }

  void flash(Rgb c, uint32_t nowMs) {
    flash_ = true;
    flashColor_ = c;
    flashStart_ = nowMs;
    flashCount_ = 0;  // all LEDs
  }

  // Key N lights the first N LEDs as a rainbow; the start hue shifts per key so keys differ.
  void keyFlash(uint8_t key, uint32_t nowMs) {
    flash_ = true;
    flashStart_ = nowMs;
    flashKey_ = key % 12;
    flashCount_ = static_cast<uint8_t>(flashKey_ + 1);
  }

  Rgb flashAt(size_t i, uint32_t nowMs) const {
    if (!flash_) return {0, 0, 0};
    uint32_t left = kFlashMs - (nowMs - flashStart_);
    Rgb c = flashColor_;
    if (flashCount_) {
      if (i >= flashCount_) return {0, 0, 0};
      c = hsvToRgb(static_cast<uint16_t>(flashKey_ * 30 + i * 30), 200);
    }
    return {scale(c.r, left), scale(c.g, left), scale(c.b, left)};
  }

  void applyDim(Rgb *px, size_t n) const {
    if (dim_ == 255) return;
    for (size_t i = 0; i < n; i++)
      px[i] = {static_cast<uint8_t>(px[i].r * dim_ / 255), static_cast<uint8_t>(px[i].g * dim_ / 255),
               static_cast<uint8_t>(px[i].b * dim_ / 255)};
  }

  bool flash_ = false, gauge_ = false, wifi_ = false;
  uint8_t dim_ = 255;
  Rgb flashColor_{0, 0, 0};
  uint8_t flashCount_ = 0, flashKey_ = 0;
  uint32_t flashStart_ = 0, gaugeStart_ = 0;
};
