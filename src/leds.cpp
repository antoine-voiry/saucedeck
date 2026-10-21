#include "leds.h"
#include <Adafruit_NeoPixel.h>
#include "pins.h"

// 16 LEDs on the LED board (owner, 2026-10-19). Full white would be ~960 mA:
// LedEffects caps every frame at 250 mA estimated.
static constexpr uint16_t kLedCount = 16;
static constexpr uint32_t kFrameMs = 20;

static Adafruit_NeoPixel strip(kLedCount, LED_DATA_PIN, NEO_GRB + NEO_KHZ800);
static LedEffects effects;
static Rgb frame[kLedCount];
static uint32_t lastFrame = 0;

void ledsBegin() {
  strip.begin();
  strip.setBrightness(255);  // brightness capped by limitPower() in deck_core instead
  strip.clear();
  strip.show();
}

void ledsOnAction(const Action &a, uint32_t nowMs) { effects.onAction(a, nowMs); }

void ledsUpdate(const LedInputs &in, uint32_t nowMs) {
  if (nowMs - lastFrame < kFrameMs) return;
  lastFrame = nowMs;
  effects.render(in, nowMs, frame, kLedCount);
  for (uint16_t i = 0; i < kLedCount; i++) strip.setPixelColor(i, frame[i].r, frame[i].g, frame[i].b);
  strip.show();
}

void ledsSetDim(uint8_t level) { effects.setDim(level); }
