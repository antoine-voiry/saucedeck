#include "display.h"
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include "driver/gpio.h"
#include "deck_clock.h"
#include "legend.h"
#include "pins.h"
#include "theme.h"

// 240x320 panel, landscape. 1 was upside down in the case (bench 2026-10-18) -> 3.
static constexpr uint8_t kRotation = 3;
static constexpr uint32_t kSpiHz = 20000000;  // bench-verified (docs/kb/peripherals.md)
static constexpr uint32_t kSplashMs = 1500;
static constexpr uint32_t kMinFrameMs = 30;  // batch fast knob turns into one redraw

// "Candy" layout (landscape 320x240): top bar, 4x3 rounded tiles (key positions 1-12,
// row-major), status strip (volume segments, %, mic pill; card line). Colours and geometry:
// docs/superpowers/plans/2026-10-31-lcd-theme-candy-plan4.md (Global Constraints).
static constexpr int16_t kW = 320;
static constexpr int16_t kTopH = 22;
static constexpr int16_t kTileX0 = 3, kTileY0 = 24, kTilePitchX = 80, kTilePitchY = 54;
static constexpr int16_t kTileW = 74, kTileH = 50, kTileR = 8, kBandH = 5, kBandR = 3;
static constexpr uint16_t kLabelW = 68;
static constexpr int16_t kStripY = 188;
static constexpr int16_t kSegX = 8, kSegPitch = 8, kSegY = 194, kSegW = 6, kSegH = 15, kSegR = 2;
static constexpr int16_t kPctX = 172, kPctBase = 208, kPctBoxY = 190, kPctBoxW = 51, kPctBoxH = 22;
static constexpr int16_t kPillX = 226, kPillY = 193, kPillW = 87, kPillH = 17, kPillR = 8, kPillBase = 206;
static constexpr int16_t kLine2Y = 214, kLine2H = 26, kLine2Base = 231;
static constexpr int16_t kIconX = 8, kIconY = 216, kIconS = 18, kIconR = 5;
static constexpr int16_t kCardTextX = 34, kCardTextMaxW = 248, kDotY = 224, kDotR = 2;
static constexpr uint8_t kMaxDots = 7;
static constexpr int16_t kTopTextX = 76, kTopTextW = 208;  // slot 0 text, up to x 284

static constexpr uint16_t kWhite = 0xFFFF;
static constexpr uint16_t kApplyYellow = rgb565(0xFF, 0xD6, 0x0A);

// CARD icon ids (spec 4.4): rounded box with a white letter. 0 = none.
struct CardIcon {
  uint16_t color;
  char letter;
};
static const CardIcon kIcons[kCardIcons] = {
    {kThemeBar, ' '},
    {rgb565(0xAF, 0x52, 0xDE), 'M'},  // music
    {rgb565(0x0A, 0x84, 0xFF), 'C'},  // calendar
    {rgb565(0x30, 0xD1, 0x58), '%'},  // cpu
    {rgb565(0xFF, 0xB0, 0x00), 'W'},  // weather
    {rgb565(0xFF, 0x7A, 0x00), '@'},  // mail
    {rgb565(0x2A, 0xAB, 0xEE), 'T'},  // chat
    {rgb565(0x34, 0xC7, 0x59), 'B'},  // battery
};

static Adafruit_ST7789 tft(&SPI, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static ScreenModel model;
static uint32_t splashUntil = 0;
static uint32_t lastFrame = 0;
static bool splashing = true;

static uint8_t advance(const GFXfont &f, char c) {
  uint8_t u = static_cast<uint8_t>(c);
  if (u < f.first || u > f.last) return 0;
  return f.glyph[u - f.first].xAdvance;
}
static uint8_t advRegular(char c) { return advance(FreeSans9pt7b, c); }
static uint8_t advBold(char c) { return advance(FreeSansBold9pt7b, c); }
static uint8_t advBig(char c) { return advance(FreeSansBold12pt7b, c); }

static int16_t textWidth(const char *s, GlyphWidthFn w) {
  int16_t px = 0;
  for (const char *p = s; *p; p++) px += w(*p);
  return px;
}

// Custom fonts draw no background: callers fill the area first.
static void printAt(const GFXfont &f, const char *s, int16_t x, int16_t baseline, uint16_t color) {
  tft.setFont(&f);
  tft.setTextSize(1);
  tft.setTextColor(color, color);
  tft.setCursor(x, baseline);
  tft.print(s);
  tft.setFont(nullptr);
}

static void printCentered(const GFXfont &f, GlyphWidthFn w, const char *s, int16_t x0, int16_t width,
                          int16_t baseline, uint16_t color) {
  printAt(f, s, x0 + (width - textWidth(s, w)) / 2, baseline, color);
}

// Tile i = key position i + 1: rounded body (idle tint -> full hue by press level), hue band
// on top, legend centred. Corners stay background, so a repaint needs no clear.
static void drawTile(uint8_t i, const KeyEntry &e, uint8_t level, bool failed) {
  const int16_t x = kTileX0 + (i % 4) * kTilePitchX, y = kTileY0 + (i / 4) * kTilePitchY;
  const TileLook look = tileLook(i, level, failed);
  tft.fillRoundRect(x, y, kTileW, kTileH, kTileR, look.fill);
  tft.fillRoundRect(x, y, kTileW, kBandH, kBandR, look.band);
  tft.fillRect(x, y + kBandR - 1, kTileW, kBandH - kBandR + 1, look.band);  // square bottom corners

  char text[kLegendLineMax + 1];
  displayLabel(e, text, sizeof text);
  Legend lg;
  chooseLegend(text, kLabelW, advBold, advRegular, lg);
  const bool bold = lg.font == LegendFont::Bold;
  const GFXfont &f = bold ? FreeSansBold9pt7b : FreeSans9pt7b;
  const GlyphWidthFn w = bold ? advBold : advRegular;
  if (lg.lines.line2[0]) {
    printCentered(f, w, lg.lines.line1, x, kTileW, y + 23, look.text);
    printCentered(f, w, lg.lines.line2, x, kTileW, y + 41, look.text);
  } else {
    printCentered(f, w, lg.lines.line1, x, kTileW, y + 32, look.text);
  }
}

static void drawTopBar(const ScreenState &s, const StatusBoard &status) {
  tft.fillRect(0, 0, kW, kTopH, kThemeBar);
  char hhmm[6];
  formatHhmm(s.clockMinute, hhmm);
  printAt(FreeSansBold12pt7b, hhmm, 6, 18, kWhite);
  if (const StatusCard *c = status.top()) {
    char line[kCardTextMax + 1];
    fitLine(c->text, kTopTextW, advRegular, line, sizeof line);
    printAt(FreeSans9pt7b, line, kTopTextX, 16, kThemeTopText);
  }
  tft.fillCircle(294, 10, 4, s.helperPresent ? kThemeHelper : kThemeSegOff);
  if (s.wifiOn) tft.fillCircle(308, 10, 4, kThemeWifi);
}

static void drawSegments(uint8_t first, uint8_t count, uint8_t lit, bool muted) {
  for (uint8_t i = first; i < first + count && i < kVolSegments; i++)
    tft.fillRoundRect(kSegX + i * kSegPitch, kSegY, kSegW, kSegH, kSegR,
                      i < lit ? segmentColor(i, muted) : kThemeSegOff);
}

static void drawBarFull(const ScreenState &s) {
  drawSegments(0, kVolSegments, litSegments(s.volumePercent), s.speakerMuted);
}

static void drawBarDelta(const ScreenState &prev, const ScreenState &cur) {
  const uint8_t lit = litSegments(cur.volumePercent);
  const SegSpan sp = segDelta(litSegments(prev.volumePercent), lit);
  drawSegments(sp.first, sp.count, lit, cur.speakerMuted);
}

// "65%" in FreeSansBold12pt; "100%" (61 px) does not fit the 51 px box -> FreeSansBold9pt.
// Muted: "OFF" in red ("MUTED" is 87 px bold 12, 63 px regular 9: wider than the box).
static void drawVolumeText(const ScreenState &s) {
  tft.fillRect(kPctX, kPctBoxY, kPctBoxW, kPctBoxH, kThemeBar);
  char buf[8];
  if (s.speakerMuted) snprintf(buf, sizeof buf, "OFF");
  else snprintf(buf, sizeof buf, "%u%%", s.volumePercent);
  const bool big = textWidth(buf, advBig) <= kPctBoxW;
  printAt(big ? FreeSansBold12pt7b : FreeSansBold9pt7b, buf, kPctX, kPctBase,
          s.speakerMuted ? kThemeMicMuted : kWhite);
}

// Pill: green with the helper's mic %, red when muted, grey when the level is unknown.
// Muted reads "MIC OFF": "MIC MUTE" is 93 px bold, wider than the 87 px pill.
static void drawMic(const ScreenState &s) {
  char buf[10];
  uint16_t color;
  if (s.micMuted) {
    snprintf(buf, sizeof buf, "MIC OFF");
    color = kThemeMicMuted;
  } else if (s.micLevelKnown) {
    snprintf(buf, sizeof buf, "MIC %u%%", s.micPercent);
    color = kThemeMicLive;
  } else {
    snprintf(buf, sizeof buf, "MIC LIVE");
    color = kThemeMicUnknown;
  }
  tft.fillRoundRect(kPillX, kPillY, kPillW, kPillH, kPillR, color);
  // Bold 9pt up to 79 px (4 px margin each side); wider ("MIC 100%" = 86 px) -> regular 9pt.
  const bool bold = textWidth(buf, advBold) <= kPillW - 8;
  printCentered(bold ? FreeSansBold9pt7b : FreeSans9pt7b, bold ? advBold : advRegular, buf, kPillX, kPillW,
                kPillBase, 0x0000);
}

static void drawSplash() {
  tft.fillScreen(kThemeBg);
  const char *name = "SAUCEDECK";
  const int16_t w = textWidth(name, advBig), x = (kW - w) / 2;
  printAt(FreeSansBold12pt7b, name, x, 120, kWhite);
  // Underline: one short bar per key hue.
  const int16_t seg = w / kKeyCount;
  for (uint8_t i = 0; i < kKeyCount; i++) tft.fillRect(x + i * seg, 128, seg, 4, kKeyHues[i]);
  printCentered(FreeSans9pt7b, advRegular, "macro deck v1", 0, kW, 156, kThemeTopText);
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

// Banner text on strip line 2 (drawLine2 decides banner vs card).
static void drawBanner(uint8_t b, uint8_t arg) {
  tft.fillRect(0, kLine2Y, kW, kLine2H, kThemeBar);
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
  printAt(FreeSansBold9pt7b, text, 8, kLine2Base, b == BANNER_APPLY ? kApplyYellow : kWhite);
}

static void drawIcon(uint8_t icon) {
  if (icon == 0 || icon >= kCardIcons) return;
  tft.fillRoundRect(kIconX, kIconY, kIconS, kIconS, kIconR, kIcons[icon].color);
  char s[2] = {kIcons[icon].letter, 0};
  printCentered(FreeSansBold9pt7b, advBold, s, kIconX, kIconS, kLine2Base, kWhite);
}

// Strip line 2: a banner wins; otherwise the rotating status card with one dot per card.
static void drawLine2(const ScreenState &s, const StatusBoard &status) {
  if (s.banner != BANNER_NONE) {
    drawBanner(s.banner, s.bannerArg);
    return;
  }
  tft.fillRect(0, kLine2Y, kW, kLine2H, kThemeBar);
  const StatusCard *c = status.current();
  if (!c) return;
  drawIcon(c->icon);
  const uint8_t n = status.count() < kMaxDots ? status.count() : kMaxDots;
  int16_t textW = kCardTextMaxW;
  if (n) {
    const int16_t room = cardDotX(0, n) - kDotR - 4 - kCardTextX;  // >= 4 px before the first dot
    if (room < textW) textW = room;
  }
  char line[kCardTextMax + 1];
  fitLine(c->text, static_cast<uint16_t>(textW), advRegular, line, sizeof line);
  printAt(FreeSans9pt7b, line, kCardTextX, kLine2Base, kWhite);
  for (uint8_t k = 0; k < n; k++)
    tft.fillCircle(cardDotX(k, n), kDotY, kDotR, k + 1 == status.index() ? kWhite : kThemeSegOff);
}

void displayUpdate(const ScreenState &state, const KeyMap &keys, TileAnim &anim, const StatusBoard &status,
                   uint32_t nowMs) {
  if (splashing) {
    if (static_cast<int32_t>(nowMs - splashUntil) < 0) return;
    splashing = false;
    tft.fillScreen(kThemeBg);
    tft.fillRect(0, kStripY, kW, 240 - kStripY, kThemeBar);  // strip ground; parts clear only their box
    model.invalidate();
  }
  if (nowMs - lastFrame < kMinFrameMs) return;
  lastFrame = nowMs;

  uint16_t dirty = model.update(state, anim.tick(nowMs));
  if (!dirty) return;
  const ScreenState &cur = model.current();
  if (dirty & DIRTY_TOPBAR) drawTopBar(cur, status);
  if (dirty & DIRTY_TILES) {
    uint16_t m = model.tileMask();
    for (uint8_t i = 0; i < kKeyCount; i++)
      if (m & (1u << i)) drawTile(i, keys.entry(i), anim.level(i), anim.failed(i));
  }
  if (dirty & DIRTY_BAR_COLOR) drawBarFull(cur);
  else if (dirty & DIRTY_BAR) drawBarDelta(model.previous(), cur);
  if (dirty & DIRTY_VOLUME_TEXT) drawVolumeText(cur);
  if (dirty & DIRTY_MIC) drawMic(cur);
  if (dirty & DIRTY_BANNER) drawLine2(cur, status);
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
