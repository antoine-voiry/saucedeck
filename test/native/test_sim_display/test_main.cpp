// Display scenarios: the real src/ firmware drawing legends and press feedback on the sim LCD.
#include <stdlib.h>
#include <string.h>
#include <unity.h>
#include <vector>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include "fonts/FreeSans7pt7b.h"
#include "frame.h"
#include "keymap.h"
#include "reset_log.h"
#include "sim_deck.h"
#include "sim_test.h"
#include "status_board.h"
#include "theme.h"

void setUp() {}
void tearDown() {}

static constexpr uint32_t kAfterSplash = 1600;  // splash 1500 ms (src/display.cpp)
static constexpr uint32_t kSettle = 30;         // > 25 ms debounce

// Tile of key position pos (1..12): Candy geometry (src/display.cpp drawTile, plan 4 Global Constraints).
static int16_t tileX(uint8_t pos) { return ((pos - 1) % 4) * 80 + 3; }
static int16_t tileY(uint8_t pos) { return 24 + ((pos - 1) / 4) * 54; }

static bool isBody(const SimLcdRect &r, uint8_t pos) {
  return r.x == tileX(pos) && r.y == tileY(pos) && r.w == 74 && r.h == 50;
}

static bool insideTile(const SimLcdRect &r, uint8_t pos) {
  return r.x >= tileX(pos) && r.y >= tileY(pos) && r.x + r.w <= tileX(pos) + 74 && r.y + r.h <= tileY(pos) + 50;
}

static bool circleInsideTile(const SimLcdRect &c, uint8_t pos) {
  return insideTile({static_cast<int16_t>(c.x - c.w), static_cast<int16_t>(c.y - c.w), static_cast<int16_t>(2 * c.w + 1),
                     static_cast<int16_t>(2 * c.w + 1), c.c},
                    pos);
}

static std::vector<SimLcdRect> bodies(uint8_t pos) {
  std::vector<SimLcdRect> out;
  for (const SimLcdRect &r : board().lcd.rounds)
    if (isBody(r, pos)) out.push_back(r);
  return out;
}

static void clearRecords() {
  board().lcd.texts.clear();
  board().lcd.rects.clear();
  board().lcd.rounds.clear();
  board().lcd.circles.clear();
  board().lcd.hlines.clear();
}

static int16_t widthIn(const GFXfont &f, const char *s) {
  int16_t w = 0;
  for (const char *p = s; *p; p++) w += f.glyph[*p - f.first].xAdvance;
  return w;
}

static void storeKeyMap(const KeyMap &m) {
  uint8_t blob[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, blob, sizeof blob);
  board().nvs.ns["saucedeck"]["keymap"].assign(blob, blob + n);  // src/keymap_store.cpp names
}

// Icon box of tile pos (spec 2026-11-08 key icons, section 6): (x+25, y+8), 24x24.
static bool insideIconBox(const SimLcdRect &h, uint8_t pos) {
  const int16_t bx = tileX(pos) + 25, by = tileY(pos) + 8;
  return h.h == 1 && h.w > 0 && h.x >= bx && h.x + h.w <= bx + 24 && h.y >= by && h.y < by + 24;
}

static std::vector<SimLcdRect> hlinesIn(uint8_t pos) {
  std::vector<SimLcdRect> out;
  for (const SimLcdRect &h : board().lcd.hlines)
    if (h.x >= tileX(pos) && h.x < tileX(pos) + 74 && h.y >= tileY(pos) && h.y < tileY(pos) + 50) out.push_back(h);
  return out;
}

// Icon tile labels: generated FreeSans 7pt regular (include/fonts), max 72 px, baseline y+44.

// Fonts are static per translation unit: identify the face by its glyph table, not its address.
static bool isRegular7(const void *font) {
  const GFXfont *f = static_cast<const GFXfont *>(font);
  const GFXfont &r = FreeSans7pt7b;
  return f && f->first == r.first && f->last == r.last && f->yAdvance == r.yAdvance &&
         memcmp(f->glyph, r.glyph, sizeof(GFXglyph) * (r.last - r.first + 1)) == 0;
}

static const SimLcdText *labelOf(uint8_t pos) {
  const SimLcdText *label = nullptr;
  for (const SimLcdText &t : board().lcd.texts)
    if (t.y == tileY(pos) + 44 && t.x >= tileX(pos) && t.x < tileX(pos) + 74) label = &t;
  return label;
}

SIM_TEST(test_boot_shows_default_legends_f13_to_f24) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  char t[8];
  for (unsigned k = 13; k <= 24; k++) {
    snprintf(t, sizeof t, "F%u", k);
    TEST_ASSERT_TRUE_MESSAGE(board().lcd.printed(t), t);
  }
  TEST_ASSERT_TRUE(board().lcd.printedExact("MIC LIVE"));
}

// Text tiles (icon 0): the Plan 4 two-line legend path is unchanged.
SIM_TEST(test_labels_without_icons_become_legends) {
  KeyMap m;
  const char *labels[] = {"Chrome", "YouTube", "Weather", "Hide app"};
  for (uint8_t i = 0; i < 4; i++) TEST_ASSERT_EQUAL(KeyMapError::None, m.setLabel(i, labels[i]));
  storeKeyMap(m);
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Chrome"));
  // chooseLegend, 68 px: "YouTube" is 78 px bold, 73 px regular, no space -> balanced bold halves.
  TEST_ASSERT_TRUE(board().lcd.printedExact("You"));
  TEST_ASSERT_TRUE(board().lcd.printedExact("Tube"));
  TEST_ASSERT_TRUE(board().lcd.printedExact("Weather"));  // 68 px regular: one line
  TEST_ASSERT_TRUE(board().lcd.printedExact("Hide"));     // "Hide app": split at the space
  TEST_ASSERT_TRUE(board().lcd.printedExact("app"));
  TEST_ASSERT_FALSE(board().lcd.printed("F14"));
}

SIM_TEST(test_icon_tile_draws_icon_runs_in_box_and_one_line_label) {
  SimDeck sim;
  TEST_ASSERT_TRUE(sim.loadPresetIntoNvs(SIM_PROJECT_DIR "/presets/mac-daily.json"));
  sim.boot();
  sim.run(kAfterSplash);
  std::vector<SimLcdRect> runs = hlinesIn(1);
  TEST_ASSERT_GREATER_THAN(20, runs.size());
  for (const SimLcdRect &h : runs) {
    TEST_ASSERT_TRUE(insideIconBox(h, 1));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, h.c);  // idle: white ink
  }
  // Label: one line at baseline y+44, whole, FreeSans 7pt regular (bold 7pt drops the i/j dot).
  const SimLcdText *label = labelOf(1);
  TEST_ASSERT_NOT_NULL(label);
  TEST_ASSERT_EQUAL_STRING("Screenshot", label->s.c_str());
  TEST_ASSERT_TRUE(isRegular7(label->font));
  const GFXfont &face = FreeSans7pt7b;
  const int16_t w = widthIn(face, "Screenshot");
  TEST_ASSERT_TRUE(w <= 72);
  TEST_ASSERT_EQUAL(tileX(1) + (74 - w) / 2, label->x);
  // Nothing else printed on tile 1 (no two-line legend).
  for (const SimLcdText &t : board().lcd.texts)
    if (t.x >= tileX(1) && t.x < tileX(1) + 74 && t.y > tileY(1) && t.y < tileY(1) + 50)
      TEST_ASSERT_EQUAL(tileY(1) + 44, t.y);
  // Short label fits bold whole: "Gmail" (key 3).
  TEST_ASSERT_TRUE(board().lcd.printedExact("Gmail"));
  // Ink pixels land in the framebuffer inside the icon box.
  int ink = 0;
  for (int y = tileY(1) + 8; y < tileY(1) + 32; y++)
    for (int x = tileX(1) + 25; x < tileX(1) + 49; x++)
      if (board().lcd.fb[y][x] == 0xFFFF) ink++;
  TEST_ASSERT_GREATER_THAN(20, ink);
}

SIM_TEST(test_all_mac_daily_labels_fit_whole_on_icon_tiles) {
  SimDeck sim;
  TEST_ASSERT_TRUE(sim.loadPresetIntoNvs(SIM_PROJECT_DIR "/presets/mac-daily.json"));
  sim.boot();
  sim.run(kAfterSplash);
  const char *labels[12] = {"Screenshot", "Chrome", "Gmail",    "Telegram", "Fusion 360", "Settings",
                            "Help",       "YouTube", "Hide app", "iMessage", "Weather",    "Bambu"};
  for (uint8_t pos = 1; pos <= 12; pos++) {
    const SimLcdText *label = labelOf(pos);
    TEST_ASSERT_NOT_NULL_MESSAGE(label, labels[pos - 1]);
    TEST_ASSERT_EQUAL_STRING(labels[pos - 1], label->s.c_str());
    TEST_ASSERT_TRUE_MESSAGE(isRegular7(label->font), labels[pos - 1]);  // e.g. "Gmail": regular, not bold
    TEST_ASSERT_TRUE_MESSAGE(widthIn(FreeSans7pt7b, labels[pos - 1]) <= 72, labels[pos - 1]);
    TEST_ASSERT_TRUE(label->x >= tileX(pos) + 1);
  }
}

SIM_TEST(test_pressed_icon_tile_draws_black_ink) {
  SimDeck sim;
  TEST_ASSERT_TRUE(sim.loadPresetIntoNvs(SIM_PROJECT_DIR "/presets/mac-daily.json"));
  sim.boot();
  sim.run(kAfterSplash);
  clearRecords();
  sim.press(8);
  sim.run(kSettle + 40);
  std::vector<SimLcdRect> runs = hlinesIn(8);
  TEST_ASSERT_FALSE(runs.empty());
  for (const SimLcdRect &h : runs) {
    TEST_ASSERT_TRUE(insideIconBox(h, 8));
    TEST_ASSERT_EQUAL_HEX16(0x0000, h.c);
  }
  for (const SimLcdRect &h : board().lcd.hlines) TEST_ASSERT_TRUE(insideIconBox(h, 8));  // only its tile
  TEST_ASSERT_EQUAL_HEX16(tileLook(7, TileAnim::kFull, false).fill, bodies(8).back().c);
}

SIM_TEST(test_icon_zero_key_keeps_text_legend_among_icon_tiles) {
  SimDeck sim;
  TEST_ASSERT_TRUE(sim.loadPresetIntoNvs(SIM_PROJECT_DIR "/presets/mac-daily.json"));
  // Key 3 without icon, two-word label: Plan 4 two-line legend.
  KeyMap m;
  size_t n = board().nvs.ns["saucedeck"]["keymap"].size();
  TEST_ASSERT_TRUE(deserializeKeyMap(board().nvs.ns["saucedeck"]["keymap"].data(), n, m));
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setIcon(2, ""));
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setLabel(2, "Gmail in Chrome"));
  storeKeyMap(m);
  sim.boot();
  sim.run(kAfterSplash);
  TEST_ASSERT_TRUE(hlinesIn(3).empty());
  TEST_ASSERT_FALSE(hlinesIn(2).empty());
  TEST_ASSERT_TRUE(board().lcd.printedExact("Gmail in"));
  TEST_ASSERT_TRUE(board().lcd.printedExact("Chrome"));
}

SIM_TEST(test_key_press_repaints_only_its_tile) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  clearRecords();
  sim.press(7);
  sim.run(kSettle + 40);
  TEST_ASSERT_FALSE(board().lcd.rounds.empty());
  for (const SimLcdRect &r : board().lcd.rects) TEST_ASSERT_TRUE(insideTile(r, 7));
  for (const SimLcdRect &r : board().lcd.rounds) TEST_ASSERT_TRUE(insideTile(r, 7));
  for (const SimLcdRect &c : board().lcd.circles) TEST_ASSERT_TRUE(circleInsideTile(c, 7));
  for (const SimLcdText &t : board().lcd.texts) {
    TEST_ASSERT_TRUE(t.x >= tileX(7) && t.x < tileX(7) + 74);
    TEST_ASSERT_TRUE(t.y > tileY(7) && t.y < tileY(7) + 50);
  }
  // Rounded body (radius 8) then the hue band (x, y, 74, 5) in key 7's colour.
  TEST_ASSERT_EQUAL_HEX16(tileLook(6, TileAnim::kFull, false).fill, bodies(7).back().c);
  const SimLcdRect &band = board().lcd.rounds.back();
  TEST_ASSERT_EQUAL(tileX(7), band.x);
  TEST_ASSERT_EQUAL(tileY(7), band.y);
  TEST_ASSERT_EQUAL(74, band.w);
  TEST_ASSERT_EQUAL(5, band.h);
  TEST_ASSERT_EQUAL_HEX16(kKeyHues[6], band.c);
  // Corner pixel of the rounded tile keeps the background colour.
  TEST_ASSERT_EQUAL_HEX16(kThemeBg, board().lcd.fb[tileY(7)][tileX(7)]);
}

SIM_TEST(test_idle_tiles_use_key_hues) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  for (uint8_t pos = 1; pos <= 12; pos++) {
    TEST_ASSERT_FALSE(bodies(pos).empty());
    TEST_ASSERT_EQUAL_HEX16(tileLook(pos - 1, 0, false).fill, bodies(pos).back().c);
  }
}

SIM_TEST(test_release_fades_back_to_idle_in_four_repaints) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  const uint16_t idle = bodies(6).back().c;
  sim.press(6);
  sim.run(kSettle + 40);
  TEST_ASSERT_NOT_EQUAL(idle, bodies(6).back().c);
  clearRecords();
  sim.release(6);
  sim.run(kSettle + 300);
  std::vector<SimLcdRect> fade = bodies(6);
  TEST_ASSERT_EQUAL(4, fade.size());  // levels 3, 2, 1, 0
  TEST_ASSERT_EQUAL_HEX16(idle, fade.back().c);
}

SIM_TEST(test_app_key_without_helper_flashes_orange) {
  KeyMap m;
  TEST_ASSERT_EQUAL(KeyMapError::None, m.set(0, KeyType::App, "Calculator"));
  uint8_t blob[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, blob, sizeof blob);
  board().nvs.ns["saucedeck"]["keymap"].assign(blob, blob + n);  // src/keymap_store.cpp names
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  sim.press(1);
  sim.run(kSettle + 40);
  TEST_ASSERT_EQUAL_HEX16(kThemeFailed, bodies(1).back().c);  // amber, theme.h
  TEST_ASSERT_TRUE(board().lcd.printed("helper not running"));
}

// --- fake Mac helper on the CDC port (host -> deck frames) ---

static void sendMsg(uint8_t type, const uint8_t *p, uint8_t len) {
  uint8_t m[kReportSize] = {kProtoVersion, type, len};
  memcpy(m + 3, p, len);
  uint8_t f[kFrameSize];
  frameMessage(m, f);
  board().serial.rx.insert(board().serial.rx.end(), f, f + kFrameSize);
}

static void hello() {
  const uint8_t p[1] = {1};
  sendMsg(MSG_HELLO, p, 1);
}

static void sendTime(uint32_t epoch, int16_t tz) {
  const uint8_t p[6] = {static_cast<uint8_t>(epoch), static_cast<uint8_t>(epoch >> 8),
                        static_cast<uint8_t>(epoch >> 16), static_cast<uint8_t>(epoch >> 24),
                        static_cast<uint8_t>(tz), static_cast<uint8_t>(static_cast<uint16_t>(tz) >> 8)};
  sendMsg(MSG_TIME, p, 6);
}

static void sendCard(uint8_t slot, uint8_t icon, const char *text, uint16_t ttl = 0) {
  uint8_t p[5 + kCardTextMax];
  size_t n = strlen(text);
  p[0] = slot; p[1] = icon; p[2] = ttl & 0xFF; p[3] = ttl >> 8; p[4] = static_cast<uint8_t>(n);
  memcpy(p + 5, text, n);
  sendMsg(MSG_CARD, p, static_cast<uint8_t>(5 + n));
}

// Runs ms with the helper alive (HELLO every second, like the real helper).
static void runWithHelper(SimDeck &sim, uint32_t ms) {
  while (ms) {
    hello();
    uint32_t step = ms < 1000 ? ms : 1000;
    sim.run(step);
    ms -= step;
  }
}

static void sendLevel(uint8_t type, uint8_t pct, bool muted) {
  const uint8_t p[2] = {pct, static_cast<uint8_t>(muted)};
  sendMsg(type, p, 2);
}

// Card dots on line 2 (y 224, radius 2): returns how many; current must be the white one.
static size_t cardDots(uint8_t current) {
  std::vector<SimLcdRect> dots;
  for (const SimLcdRect &c : board().lcd.circles)
    if (c.y == 224 && c.w == 2) dots.push_back(c);
  for (size_t k = 0; k < dots.size(); k++) {
    TEST_ASSERT_EQUAL(cardDotX(k, dots.size()), dots[k].x);
    TEST_ASSERT_EQUAL_HEX16(k + 1 == current ? 0xFFFF : kThemeSegOff, dots[k].c);
  }
  return dots.size();
}

static bool line2Cleared() {
  for (const SimLcdRect &r : board().lcd.rects)
    if (r.x == 0 && r.y == 214 && r.w == 320 && r.h == 26 && r.c == kThemeBar) return true;
  return false;
}

SIM_TEST(test_clock_dashes_until_time_then_shows_local_time) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("--:--"));
  sendTime(20000u * 86400u + 13 * 3600 + 32 * 60, 60);  // 13:32 UTC, +1 h
  runWithHelper(sim, 100);
  TEST_ASSERT_TRUE(board().lcd.printed("14:32"));
}

SIM_TEST(test_slot0_card_in_top_bar) {
  SimDeck sim;
  sim.boot();
  hello();
  sendCard(0, 2, "Standup in 12m");
  runWithHelper(sim, kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Standup in 12m"));
}

SIM_TEST(test_cards_rotate_with_index) {
  SimDeck sim;
  sim.boot();
  hello();
  sendCard(1, 1, "Daft Punk");
  sendCard(3, 3, "CPU 12%");
  runWithHelper(sim, kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Daft Punk"));
  TEST_ASSERT_EQUAL(2, cardDots(1));
  clearRecords();
  runWithHelper(sim, StatusBoard::kRotateMs);
  TEST_ASSERT_TRUE(board().lcd.printed("CPU 12%"));
  TEST_ASSERT_EQUAL(2, cardDots(2));
  // Rotation repaints line 2 only (y 214..239).
  for (const SimLcdRect &r : board().lcd.rects) TEST_ASSERT_TRUE(r.y >= 214);
  for (const SimLcdRect &r : board().lcd.rounds) TEST_ASSERT_TRUE(r.y >= 214);
  for (const SimLcdRect &c : board().lcd.circles) TEST_ASSERT_TRUE(c.y - c.w >= 214);
}

SIM_TEST(test_helper_silence_clears_cards) {
  SimDeck sim;
  sim.boot();
  hello();
  sendCard(1, 1, "Daft Punk");
  runWithHelper(sim, kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Daft Punk"));
  board().lcd.texts.clear();
  board().lcd.rects.clear();
  sim.run(3500);  // no HELLO: HostLink::kTimeoutMs = 3000
  TEST_ASSERT_TRUE(line2Cleared());
  TEST_ASSERT_FALSE(board().lcd.printed("Daft Punk"));
}

SIM_TEST(test_banner_takes_line2_then_card_returns) {
  SimDeck sim;
  sim.boot();
  hello();
  sendCard(1, 1, "Daft Punk");
  runWithHelper(sim, kAfterSplash);
  sim.knobPress(1);
  sim.knobPress(2);
  runWithHelper(sim, 1500);  // > 500 ms: Wi-Fi countdown banner
  TEST_ASSERT_TRUE(board().lcd.printed("Wi-Fi in"));
  sim.knobRelease(1);
  sim.knobRelease(2);
  board().lcd.texts.clear();
  runWithHelper(sim, 500);
  TEST_ASSERT_TRUE(board().lcd.printed("Daft Punk"));
}

SIM_TEST(test_bad_card_frame_is_ignored) {
  SimDeck sim;
  sim.boot();
  hello();
  sendCard(1, 1, "ok");
  uint8_t p[5 + 3] = {1, 1, 0, 0, 3, 'a', 0x01, 'b'};  // control char -> rejected
  sendMsg(MSG_CARD, p, sizeof p);
  runWithHelper(sim, kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("ok"));
  TEST_ASSERT_EQUAL(1, cardDots(1));
}

SIM_TEST(test_seven_cards_seven_dots_text_clear_of_dots) {
  SimDeck sim;
  sim.boot();
  hello();
  const char *t = "Daft Punk - One More Time (Radio Edit) 2001";
  for (uint8_t slot = 1; slot <= 7; slot++) sendCard(slot, slot, t);
  runWithHelper(sim, kAfterSplash);
  TEST_ASSERT_EQUAL(7, cardDots(1));
  // Card text (FreeSans9pt, x 34) ends >= 4 px before the first dot's left edge.
  const int16_t firstDotLeft = cardDotX(0, 7) - 2;
  bool seen = false;
  for (const SimLcdText &tx : board().lcd.texts)
    if (tx.x == 34 && tx.y == 231) {
      seen = true;
      int16_t right = 0;  // rightmost white pixel that is not the current (white) dot 0
      for (int y = 214; y < 240; y++)
        for (int x = 34; x < 320; x++) {
          const bool dot0 = abs(x - cardDotX(0, 7)) <= 2 && abs(y - 224) <= 2;
          if (board().lcd.fb[y][x] == 0xFFFF && !dot0 && x > right) right = x;
        }
      TEST_ASSERT_TRUE(right + 4 < firstDotLeft);
      TEST_ASSERT_TRUE(tx.s.find("...") != std::string::npos);
    }
  TEST_ASSERT_TRUE(seen);
}

SIM_TEST(test_volume_step_repaints_only_changed_segments) {
  SimDeck sim;
  sim.boot();
  hello();
  sendLevel(MSG_VOLUME, 50, false);
  runWithHelper(sim, kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printedExact("50%"));
  clearRecords();
  sendLevel(MSG_VOLUME, 55, false);  // 10 -> 11 lit segments
  runWithHelper(sim, 100);
  TEST_ASSERT_TRUE(board().lcd.printedExact("55%"));
  TEST_ASSERT_EQUAL(1, board().lcd.rounds.size());
  const SimLcdRect &seg = board().lcd.rounds[0];
  TEST_ASSERT_EQUAL(8 + 10 * 8, seg.x);
  TEST_ASSERT_EQUAL(194, seg.y);
  TEST_ASSERT_EQUAL(6, seg.w);
  TEST_ASSERT_EQUAL(15, seg.h);
  TEST_ASSERT_EQUAL_HEX16(segmentColor(10, false), seg.c);
  // Only the % text box is cleared (custom font, no background).
  TEST_ASSERT_EQUAL(1, board().lcd.rects.size());
  const SimLcdRect &box = board().lcd.rects[0];
  TEST_ASSERT_EQUAL(172, box.x);
  TEST_ASSERT_EQUAL(190, box.y);
  TEST_ASSERT_EQUAL(51, box.w);
  TEST_ASSERT_EQUAL(22, box.h);
  TEST_ASSERT_EQUAL_HEX16(kThemeBar, box.c);
  TEST_ASSERT_TRUE(board().lcd.circles.empty());
  // Step down: 11 -> 10, segment 10 goes back to off.
  clearRecords();
  sendLevel(MSG_VOLUME, 50, false);
  runWithHelper(sim, 100);
  TEST_ASSERT_EQUAL(1, board().lcd.rounds.size());
  TEST_ASSERT_EQUAL_HEX16(kThemeSegOff, board().lcd.rounds[0].c);
}

SIM_TEST(test_speaker_mute_greys_all_lit_segments) {
  SimDeck sim;
  sim.boot();
  hello();
  sendLevel(MSG_VOLUME, 50, false);
  runWithHelper(sim, kAfterSplash);
  clearRecords();
  sendLevel(MSG_VOLUME, 50, true);
  runWithHelper(sim, 100);
  TEST_ASSERT_EQUAL(kVolSegments, board().lcd.rounds.size());
  TEST_ASSERT_EQUAL_HEX16(kThemeMutedSeg, board().lcd.rounds[0].c);
  TEST_ASSERT_EQUAL_HEX16(kThemeSegOff, board().lcd.rounds[10].c);
  TEST_ASSERT_TRUE(board().lcd.printedExact("OFF"));
}

static const SimLcdRect *micPill() {
  const SimLcdRect *pill = nullptr;
  for (const SimLcdRect &r : board().lcd.rounds)
    if (r.x == 226 && r.y == 193 && r.w == 87 && r.h == 17) pill = &r;
  return pill;
}

SIM_TEST(test_mic_pill_text_and_colour) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  TEST_ASSERT_NOT_NULL(micPill());
  TEST_ASSERT_EQUAL_HEX16(kThemeMicUnknown, micPill()->c);
  TEST_ASSERT_TRUE(board().lcd.printedExact("MIC LIVE"));
  hello();
  sendLevel(MSG_MIC, 70, false);
  clearRecords();
  runWithHelper(sim, 100);
  TEST_ASSERT_NOT_NULL(micPill());
  TEST_ASSERT_EQUAL_HEX16(kThemeMicLive, micPill()->c);
  TEST_ASSERT_TRUE(board().lcd.printedExact("MIC 70%"));
  sendLevel(MSG_MIC, 70, true);
  clearRecords();
  runWithHelper(sim, 100);
  TEST_ASSERT_NOT_NULL(micPill());
  TEST_ASSERT_EQUAL_HEX16(kThemeMicMuted, micPill()->c);
  TEST_ASSERT_TRUE(board().lcd.printedExact("MIC OFF"));
}

static const SimLcdText *textNamed(const char *s) {
  for (const SimLcdText &t : board().lcd.texts)
    if (t.s == s) return &t;
  return nullptr;
}


SIM_TEST(test_mic_100_falls_back_to_regular_font_and_fits_pill) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  hello();
  sendLevel(MSG_MIC, 100, false);
  clearRecords();
  runWithHelper(sim, 100);
  const SimLcdText *t = textNamed("MIC 100%");
  TEST_ASSERT_NOT_NULL(t);
  // Fonts are static per translation unit: identify by glyph advances, not by address.
  const GFXfont *used = static_cast<const GFXfont *>(t->font);
  TEST_ASSERT_NOT_NULL(used);
  TEST_ASSERT_EQUAL_INT16(widthIn(FreeSans9pt7b, "MIC 100%"), widthIn(*used, "MIC 100%"));
  TEST_ASSERT_NOT_EQUAL(widthIn(FreeSansBold9pt7b, "MIC 100%"), widthIn(*used, "MIC 100%"));
  // Regular 9pt = 84 px in the 87 px pill: 4 px margin is unattainable, 1 px is the real margin.
  TEST_ASSERT_TRUE(t->x >= 226 + 1);
  TEST_ASSERT_TRUE(t->x + widthIn(FreeSans9pt7b, "MIC 100%") <= 226 + 87 - 1);
}

SIM_TEST(test_tile_band_has_square_bottom_corners) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  const uint16_t band = tileLook(0, 0, false).band;
  TEST_ASSERT_EQUAL_HEX16(band, board().lcd.fb[tileY(1) + 4][tileX(1)]);
  TEST_ASSERT_EQUAL_HEX16(band, board().lcd.fb[tileY(1) + 4][tileX(1) + 73]);
}

// Reset log: src/reset_store.cpp keeps it under NVS "saucedeck"/"resets".
static ResetLog storedResets() {
  ResetLog log;
  const std::vector<uint8_t> &b = board().nvs.ns["saucedeck"]["resets"];
  log.load(b.data(), b.size());
  return log;
}

SIM_TEST(test_panic_reset_shows_banner_then_clears_and_is_logged) {
  board().resetReason = ESP_RST_PANIC;
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Reset: panic"));
  ResetLog log = storedResets();
  TEST_ASSERT_EQUAL_UINT8(1, log.count());
  TEST_ASSERT_EQUAL_UINT8(ESP_RST_PANIC, log.at(0).reason);
  sim.run(25000);
  board().lcd.texts.clear();
  sim.run(5000);  // banner lasts 30 s
  TEST_ASSERT_FALSE(board().lcd.printed("Reset:"));
}

SIM_TEST(test_power_on_logs_without_banner) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  TEST_ASSERT_FALSE(board().lcd.printed("Reset:"));
  ResetLog log = storedResets();
  TEST_ASSERT_EQUAL_UINT8(1, log.count());
  TEST_ASSERT_EQUAL_UINT8(ESP_RST_POWERON, log.at(0).reason);
}

SIM_TEST(test_reset_log_appends_to_stored_entries) {
  ResetLog old;
  old.push({ESP_RST_POWERON, 0});
  uint8_t blob[ResetLog::kBytes];
  size_t n = old.serialize(blob, sizeof blob);
  board().nvs.ns["saucedeck"]["resets"].assign(blob, blob + n);
  board().resetReason = ESP_RST_BROWNOUT;
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Reset: brownout"));
  ResetLog log = storedResets();
  TEST_ASSERT_EQUAL_UINT8(2, log.count());
  TEST_ASSERT_EQUAL_UINT8(ESP_RST_BROWNOUT, log.at(0).reason);
  TEST_ASSERT_EQUAL_UINT8(ESP_RST_POWERON, log.at(1).reason);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_shows_default_legends_f13_to_f24);
  RUN_TEST(test_labels_without_icons_become_legends);
  RUN_TEST(test_icon_tile_draws_icon_runs_in_box_and_one_line_label);
  RUN_TEST(test_all_mac_daily_labels_fit_whole_on_icon_tiles);
  RUN_TEST(test_pressed_icon_tile_draws_black_ink);
  RUN_TEST(test_icon_zero_key_keeps_text_legend_among_icon_tiles);
  RUN_TEST(test_key_press_repaints_only_its_tile);
  RUN_TEST(test_release_fades_back_to_idle_in_four_repaints);
  RUN_TEST(test_app_key_without_helper_flashes_orange);
  RUN_TEST(test_clock_dashes_until_time_then_shows_local_time);
  RUN_TEST(test_slot0_card_in_top_bar);
  RUN_TEST(test_cards_rotate_with_index);
  RUN_TEST(test_helper_silence_clears_cards);
  RUN_TEST(test_banner_takes_line2_then_card_returns);
  RUN_TEST(test_panic_reset_shows_banner_then_clears_and_is_logged);
  RUN_TEST(test_power_on_logs_without_banner);
  RUN_TEST(test_reset_log_appends_to_stored_entries);
  RUN_TEST(test_bad_card_frame_is_ignored);
  RUN_TEST(test_idle_tiles_use_key_hues);
  RUN_TEST(test_seven_cards_seven_dots_text_clear_of_dots);
  RUN_TEST(test_volume_step_repaints_only_changed_segments);
  RUN_TEST(test_speaker_mute_greys_all_lit_segments);
  RUN_TEST(test_mic_pill_text_and_colour);
  RUN_TEST(test_mic_100_falls_back_to_regular_font_and_fits_pill);
  RUN_TEST(test_tile_band_has_square_bottom_corners);
  return UNITY_END();
}
