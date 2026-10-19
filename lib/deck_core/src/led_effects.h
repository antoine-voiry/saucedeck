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

  void onAction(const Action &a, uint32_t nowMs) {
    switch (a.type) {
      case ActionType::KeyDown: flash(kKeyColors[a.value % 12], nowMs); break;
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
      case ActionType::KeyUp: break;
    }
  }

  void render(const LedInputs &in, uint32_t nowMs, Rgb *px, size_t n) {
    if (gauge_ && nowMs - gaugeStart_ >= kGaugeMs) gauge_ = false;
    if (flash_ && nowMs - flashStart_ >= kFlashMs) flash_ = false;

    const Rgb base = in.micMuted ? Rgb{4, 0, 0} : Rgb{0, 1, 3};  // bench: brighter was too much
    size_t lit = (static_cast<size_t>(in.volumePercent > 100 ? 100 : in.volumePercent) * n + 50) / 100;
    const Rgb gaugeOn = in.speakerMuted ? Rgb{60, 60, 60} : Rgb{0, 180, 180};

    Rgb f{0, 0, 0};
    if (flash_) {
      uint32_t left = kFlashMs - (nowMs - flashStart_);
      f = {scale(flashColor_.r, left), scale(flashColor_.g, left), scale(flashColor_.b, left)};
    }
    for (size_t i = 0; i < n; i++) {
      Rgb p = gauge_ ? (i < lit ? gaugeOn : Rgb{0, 0, 0}) : base;
      px[i] = {max(p.r, f.r), max(p.g, f.g), max(p.b, f.b)};
    }
    limitPower(px, n, kBudgetMa);
  }

 private:
  static constexpr Rgb kKeyColors[12] = {
      {200, 0, 0},   {200, 100, 0}, {200, 200, 0}, {100, 200, 0}, {0, 200, 0},   {0, 200, 100},
      {0, 200, 200}, {0, 100, 200}, {0, 0, 200},   {100, 0, 200}, {200, 0, 200}, {200, 0, 100},
  };

  static uint8_t scale(uint8_t c, uint32_t left) { return static_cast<uint8_t>(c * left / kFlashMs); }
  static uint8_t max(uint8_t a, uint8_t b) { return a > b ? a : b; }

  void flash(Rgb c, uint32_t nowMs) {
    flash_ = true;
    flashColor_ = c;
    flashStart_ = nowMs;
  }

  bool flash_ = false, gauge_ = false;
  Rgb flashColor_{0, 0, 0};
  uint32_t flashStart_ = 0, gaugeStart_ = 0;
};
