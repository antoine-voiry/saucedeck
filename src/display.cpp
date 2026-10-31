#include "display.h"
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>
#include "driver/gpio.h"
#include "legend.h"
#include "pins.h"

// 240x320 panel, landscape. 1 was upside down in the case (bench 2026-10-18) -> 3.
static constexpr uint8_t kRotation = 3;
static constexpr uint32_t kSpiHz = 20000000;  // bench-verified (docs/kb/peripherals.md)
static constexpr uint32_t kSplashMs = 1500;
static constexpr uint32_t kMinFrameMs = 30;  // batch fast knob turns into one redraw

// Layout (landscape 320x240): top bar, 4x3 legend grid (key positions 1-12, row-major),
// status strip with two lines. Spec: docs/superpowers/specs/2026-10-31-lcd-legends-status-design.md
static constexpr int16_t kW = 320;
static constexpr int16_t kTopH = 20;
static constexpr int16_t kTileW = 80, kTileH = 56, kGridY = kTopH;  // grid y 20..187
static constexpr int16_t kStripeW = 3, kLabelPad = 3;
static constexpr int16_t kTextW = kTileW - kStripeW - 2 * kLabelPad;  // 71 px per label line
static constexpr int16_t kLine1Y = 190, kLine2Y = 214, kLine2H = 26;
static constexpr int16_t kBarX = 4, kBarW = 120, kBarH = 18;  // outline; interior 1 px inside
static constexpr int16_t kPctX = 132, kMicX = 204;

static constexpr uint16_t kBg = ST77XX_BLACK;
static constexpr uint16_t kFg = ST77XX_WHITE;
static constexpr uint16_t kTileBg = 0x2104;  // dark grey
static constexpr uint16_t kBarOn = ST77XX_CYAN;
static constexpr uint16_t kGrey = 0x4208;
static constexpr uint16_t kRed = ST77XX_RED;
static constexpr uint16_t kGreen = 0x07E0;
static constexpr uint16_t kBlue = 0x041F;
static constexpr uint16_t kPurple = 0xA01F;
static constexpr uint16_t kOrange = 0xFD20;  // failed press (helper not running)

static Adafruit_ST7789 tft(&SPI, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static ScreenModel model;
static uint32_t splashUntil = 0;
static uint32_t lastFrame = 0;
static bool splashing = true;

static uint16_t typeColor(KeyType t) {
  switch (t) {
    case KeyType::Keystroke: return 0x7BEF;  // grey
    case KeyType::App: return kBlue;
    case KeyType::Url: return kGreen;
    case KeyType::Shortcut: return kPurple;
  }
  return kGrey;
}

static uint8_t glyphAdvance(char c) {
  uint8_t u = static_cast<uint8_t>(c);
  if (u < FreeSans9pt7b.first || u > FreeSans9pt7b.last) return 0;
  return FreeSans9pt7b.glyph[u - FreeSans9pt7b.first].xAdvance;
}

static void printCentered(const char *s, int16_t x0, int16_t w, int16_t baseline) {
  int16_t px = 0;
  for (const char *p = s; *p; p++) px += glyphAdvance(*p);
  tft.setCursor(x0 + (w - px) / 2, baseline);
  tft.print(s);
}

// Tile i = key position i + 1. Fill blends from the idle grey to the accent by the press
// level; custom fonts draw no background, so the fill always goes first.
static void drawTile(uint8_t i, const KeyEntry &e, uint8_t level, bool failed) {
  const int16_t x = (i % 4) * kTileW, y = kGridY + (i / 4) * kTileH;
  const uint16_t accent = failed ? kOrange : typeColor(e.type);
  tft.fillRect(x + 1, y + 1, kTileW - 2, kTileH - 2, blend565(kTileBg, accent, level, TileAnim::kFull));
  tft.fillRect(x + 1, y + 1, kStripeW, kTileH - 2, accent);

  char text[kLegendLineMax + 1];
  displayLabel(e, text, sizeof text);
  LabelLines lines;
  wrapLabel(text, kTextW, glyphAdvance, lines);
  const int16_t textX = x + kStripeW + kLabelPad;
  const uint16_t fg = level * 2 >= TileAnim::kFull ? kBg : kFg;  // dark text on a mostly-accent fill
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(fg, fg);
  if (lines.line2[0]) {
    printCentered(lines.line1, textX, kTextW, y + 24);
    printCentered(lines.line2, textX, kTextW, y + 45);
  } else {
    printCentered(lines.line1, textX, kTextW, y + 34);
  }
  tft.setFont(nullptr);
}

static void drawTopBar(const ScreenState &s) {
  tft.fillRect(0, 0, kW, kTopH, kBg);
  tft.fillCircle(296, 10, 5, s.helperPresent ? kGreen : kGrey);
  if (s.wifiOn) tft.fillCircle(310, 10, 5, kBlue);
}

static uint16_t barColor(bool muted) { return muted ? kGrey : kBarOn; }

static void drawBarFull(const ScreenState &s) {
  const uint16_t inner = kBarW - 2, fill = barFill(s.volumePercent, inner);
  tft.drawRect(kBarX, kLine1Y, kBarW, kBarH, kFg);
  tft.fillRect(kBarX + 1, kLine1Y + 1, fill, kBarH - 2, barColor(s.speakerMuted));
  tft.fillRect(kBarX + 1 + fill, kLine1Y + 1, inner - fill, kBarH - 2, kBg);
}

static void drawBarDelta(const ScreenState &prev, const ScreenState &cur) {
  const uint16_t inner = kBarW - 2;
  Span sp = barDeltaH(barFill(prev.volumePercent, inner), barFill(cur.volumePercent, inner), kBarX + 1);
  if (sp.w) tft.fillRect(sp.x, kLine1Y + 1, sp.w, kBarH - 2, sp.filled ? barColor(cur.speakerMuted) : kBg);
}

static void drawVolumeText(const ScreenState &s) {
  char buf[8];
  if (s.speakerMuted) snprintf(buf, sizeof buf, "MUTED");
  else snprintf(buf, sizeof buf, "%3u%% ", s.volumePercent);  // 5 chars: overwrites "MUTED"
  tft.setTextSize(2);
  tft.setTextColor(s.speakerMuted ? kRed : kFg, kBg);  // opaque text: no clear, no flicker
  tft.setCursor(kPctX, kLine1Y + 2);
  tft.print(buf);
}

static void drawMic(const ScreenState &s) {
  char buf[10];
  if (s.micMuted) snprintf(buf, sizeof buf, "MIC MUTE");
  else if (s.micLevelKnown) snprintf(buf, sizeof buf, "MIC %3u%%", s.micPercent);
  else snprintf(buf, sizeof buf, "MIC LIVE");
  tft.setTextSize(2);
  tft.setTextColor(s.micMuted ? kRed : kGreen, kBg);
  tft.setCursor(kMicX, kLine1Y + 2);
  tft.print(buf);
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
  // BL on GPIO9: 19 mA measured 2026-10-24 (limit 20 mA). Reduced drive strength stays.
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

static char apPassword[9] = "";
static char wifiInfo[28] = "";
void displaySetWifiInfo(const char *info) { snprintf(wifiInfo, sizeof wifiInfo, "%s", info); }
void displaySetApPassword(const char *p) { snprintf(apPassword, sizeof apPassword, "%s", p); }

// Strip line 2. Plan 2 shares this line with status cards (banner wins).
static void drawBanner(uint8_t b, uint8_t arg) {
  tft.fillRect(0, kLine2Y, kW, kLine2H, kBg);
  if (b == BANNER_NONE) return;
  char text[40];
  switch (b) {
    case BANNER_WIFI_COUNTDOWN: snprintf(text, sizeof text, "Wi-Fi in %us", arg); break;
    case BANNER_WIFI_STARTING: snprintf(text, sizeof text, "Wi-Fi.. %s", wifiInfo); break;
    case BANNER_WIFI_PORTAL: snprintf(text, sizeof text, "SETUP %s", apPassword); break;
    case BANNER_WIFI_JOINED: snprintf(text, sizeof text, "%s", wifiInfo); break;  // IP + RSSI
    case BANNER_APPLY: snprintf(text, sizeof text, "Apply? knob1 %us", arg); break;
    case BANNER_HELPER_MISSING: snprintf(text, sizeof text, "helper not running"); break;
    case BANNER_KEYMAP_RESET: snprintf(text, sizeof text, "Key map reset"); break;
    case BANNER_WIFI_FAILED: snprintf(text, sizeof text, "Wi-Fi failed"); break;
    case BANNER_INVALID_KEY: snprintf(text, sizeof text, "Invalid key %u", arg); break;
    case BANNER_BROWNOUT: snprintf(text, sizeof text, "Reset: brownout"); break;
    default: text[0] = 0; break;
  }
  tft.setTextSize(2);
  tft.setTextColor(b == BANNER_APPLY ? ST77XX_YELLOW : kFg, kBg);
  tft.setCursor(4, kLine2Y + 5);
  tft.print(text);
}

void displayUpdate(const ScreenState &state, const KeyMap &keys, TileAnim &anim, uint32_t nowMs) {
  if (splashing) {
    if (static_cast<int32_t>(nowMs - splashUntil) < 0) return;
    splashing = false;
    tft.fillScreen(kBg);
    model.invalidate();
  }
  if (nowMs - lastFrame < kMinFrameMs) return;
  lastFrame = nowMs;

  uint16_t dirty = model.update(state, anim.tick(nowMs));
  if (!dirty) return;
  const ScreenState &cur = model.current();
  if (dirty & DIRTY_TOPBAR) drawTopBar(cur);
  if (dirty & DIRTY_TILES) {
    uint16_t m = model.tileMask();
    for (uint8_t i = 0; i < kKeyCount; i++)
      if (m & (1u << i)) drawTile(i, keys.entry(i), anim.level(i), anim.failed(i));
  }
  if (dirty & DIRTY_BAR_COLOR) drawBarFull(cur);
  else if (dirty & DIRTY_BAR) drawBarDelta(model.previous(), cur);
  if (dirty & DIRTY_VOLUME_TEXT) drawVolumeText(cur);
  if (dirty & DIRTY_MIC) drawMic(cur);
  if (dirty & DIRTY_BANNER) drawBanner(cur.banner, cur.bannerArg);
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
