#include "display.h"
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "driver/gpio.h"
#include "pins.h"

// 240x320 panel, landscape. 1 was upside down in the case (bench 2026-10-18) -> 3.
static constexpr uint8_t kRotation = 3;
static constexpr uint32_t kSpiHz = 20000000;  // bench-verified (docs/kb/peripherals.md)
static constexpr uint32_t kSplashMs = 1500;
static constexpr uint32_t kMinFrameMs = 30;  // batch fast knob turns into one redraw

// Layout (landscape 320x240)
static constexpr int16_t kBarX = 24, kBarY = 20, kBarW = 44, kBarH = 200;
static constexpr int16_t kBarInnerTop = kBarY + 1, kBarInnerH = kBarH - 2;
static constexpr int16_t kSpkX = 92, kSpkY = 36;   // 64x64 icon box
static constexpr int16_t kPctX = 88, kPctY = 140;  // size-4 text "100%"
static constexpr int16_t kMicX = 222, kMicY = 36;  // 64x64 icon box
static constexpr int16_t kMicTextX = 200, kMicTextY = 140;

static constexpr uint16_t kBg = ST77XX_BLACK;
static constexpr uint16_t kFg = ST77XX_WHITE;
static constexpr uint16_t kBarOn = ST77XX_CYAN;
static constexpr uint16_t kBarMuted = 0x4208;  // grey
static constexpr uint16_t kRed = ST77XX_RED;
static constexpr uint16_t kGreen = 0x07E0;

static Adafruit_ST7789 tft(&SPI, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static ScreenModel model;
static uint32_t splashUntil = 0;
static uint32_t lastFrame = 0;
static bool splashing = true;

static void drawSpeaker(bool muted) {
  const int16_t x = kSpkX, y = kSpkY;
  tft.fillRect(x, y, 64, 64, kBg);
  tft.fillRect(x, y + 22, 14, 20, kFg);
  tft.fillTriangle(x + 14, y + 22, x + 32, y + 4, x + 32, y + 60, kFg);
  tft.fillTriangle(x + 14, y + 22, x + 32, y + 60, x + 14, y + 42, kFg);
  if (muted) {
    for (int8_t o = -2; o <= 2; o++) {
      tft.drawLine(x + 40 + o, y + 18, x + 60 + o, y + 46, kRed);
      tft.drawLine(x + 60 + o, y + 18, x + 40 + o, y + 46, kRed);
    }
  } else {
    for (int16_t r : {12, 13, 20, 21, 28, 29})
      tft.drawCircleHelper(x + 32, y + 32, r, 0x2 | 0x4, kFg);  // right half arcs
  }
}

static void drawMic(bool muted) {
  const int16_t x = kMicX, y = kMicY;
  const uint16_t c = muted ? kRed : kGreen;
  tft.fillRect(x, y, 64, 64, kBg);
  tft.fillRoundRect(x + 20, y, 24, 38, 12, c);
  for (int16_t r : {20, 21})
    tft.drawCircleHelper(x + 32, y + 26, r, 0x4 | 0x8, c);  // holder, bottom half
  tft.fillRect(x + 31, y + 47, 3, 10, c);
  tft.fillRect(x + 20, y + 57, 24, 3, c);
  if (muted)
    for (int8_t o = -2; o <= 2; o++) tft.drawLine(x + 6 + o, y + 2, x + 58 + o, y + 62, kRed);

  tft.setTextSize(3);
  tft.setTextColor(c, kBg);
  tft.setCursor(kMicTextX, kMicTextY);
  tft.print(muted ? "MUTED" : " LIVE");
}

static void drawPercent(uint8_t pct) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%3u%%", pct);
  tft.setTextSize(4);
  tft.setTextColor(kFg, kBg);  // opaque text: no clear, no flicker
  tft.setCursor(kPctX, kPctY);
  tft.print(buf);
}

static uint16_t barColor(bool muted) { return muted ? kBarMuted : kBarOn; }

static void drawBarFull(const ScreenState &s) {
  uint16_t fill = barFill(s.volumePercent, kBarInnerH);
  tft.drawRect(kBarX, kBarY, kBarW, kBarH, kFg);
  tft.fillRect(kBarX + 1, kBarInnerTop, kBarW - 2, kBarInnerH - fill, kBg);
  tft.fillRect(kBarX + 1, kBarInnerTop + kBarInnerH - fill, kBarW - 2, fill,
               barColor(s.speakerMuted));
}

static void drawBarDelta(const ScreenState &prev, const ScreenState &cur) {
  Strip st = barDelta(barFill(prev.volumePercent, kBarInnerH),
                      barFill(cur.volumePercent, kBarInnerH), kBarInnerTop, kBarInnerH);
  if (st.h) tft.fillRect(kBarX + 1, st.y, kBarW - 2, st.h, st.filled ? barColor(cur.speakerMuted) : kBg);
}

static void drawSplash() {
  tft.fillScreen(kBg);
  tft.setTextColor(kFg, kBg);
  tft.setTextSize(4);
  tft.setCursor((320 - 9 * 24) / 2, 90);
  tft.print("SAUCEDECK");
  tft.setTextSize(2);
  tft.setCursor((320 - 12 * 12) / 2, 140);
  tft.print("macro deck v1");
}

void displayBegin() {
  // BL on GPIO9, current not measured yet (owner accepted the risk 2026-10-18).
  // Reduced drive strength limits what the pin can source until it is measured.
  // PWM so the backlight can fade out when idle (IdleDimmer). 5 kHz, 8 bit, core 3.x API.
  ledcAttach(TFT_BL_PIN, 5000, 8);
  gpio_set_drive_capability(static_cast<gpio_num_t>(TFT_BL_PIN), GPIO_DRIVE_CAP_1);
  ledcWrite(TFT_BL_PIN, 255);

  SPI.begin(TFT_SCL_PIN, -1, TFT_SDA_PIN, -1);  // MISO -1: GPIO13 is LCD DC
  tft.init(240, 320);
  tft.setSPISpeed(kSpiHz);
  tft.setRotation(kRotation);
  tft.setTextWrap(false);
  drawSplash();
  splashUntil = millis() + kSplashMs;
}

void displayUpdate(const ScreenState &state, uint32_t nowMs) {
  if (splashing) {
    if (static_cast<int32_t>(nowMs - splashUntil) < 0) return;
    splashing = false;
    tft.fillScreen(kBg);
    model.invalidate();
  }
  if (nowMs - lastFrame < kMinFrameMs) return;
  lastFrame = nowMs;

  uint8_t dirty = model.update(state);
  if (!dirty) return;
  const ScreenState &cur = model.current();
  if (dirty & DIRTY_BAR_COLOR || dirty == DIRTY_ALL) drawBarFull(cur);
  else if (dirty & DIRTY_BAR) drawBarDelta(model.previous(), cur);
  if (dirty & DIRTY_VOLUME_TEXT) drawPercent(cur.volumePercent);
  if (dirty & DIRTY_SPEAKER_ICON) drawSpeaker(cur.speakerMuted);
  if (dirty & DIRTY_MIC) drawMic(cur.micMuted);
}

static bool panelAsleep = false;

void displaySetDim(uint8_t level) {
  ledcWrite(TFT_BL_PIN, static_cast<uint32_t>(level) * level / 255);  // squared: perceptual fade
  if (level == 0 && !panelAsleep) {
    tft.enableSleep(true);  // SLPIN: panel keeps its frame, draws almost nothing
    panelAsleep = true;
  } else if (level > 0 && panelAsleep) {
    tft.enableSleep(false);
    panelAsleep = false;
  }
}
