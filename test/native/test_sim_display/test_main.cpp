// Display scenarios: the real src/ firmware drawing legends and press feedback on the sim LCD.
#include <string.h>
#include <unity.h>
#include <vector>
#include "frame.h"
#include "keymap.h"
#include "sim_deck.h"
#include "sim_test.h"
#include "status_board.h"

void setUp() {}
void tearDown() {}

static constexpr uint32_t kAfterSplash = 1600;  // splash 1500 ms (src/display.cpp)
static constexpr uint32_t kSettle = 30;         // > 25 ms debounce

// Tile body of key position pos (1..12): src/display.cpp drawTile.
static bool isBody(const SimLcdRect &r, uint8_t pos) {
  const int16_t i = pos - 1, x = (i % 4) * 80, y = 20 + (i / 4) * 56;
  return r.x == x + 1 && r.y == y + 1 && r.w == 78 && r.h == 54;
}

static bool insideTile(const SimLcdRect &r, uint8_t pos) {
  const int16_t i = pos - 1, x = (i % 4) * 80, y = 20 + (i / 4) * 56;
  return r.x >= x && r.y >= y && r.x + r.w <= x + 80 && r.y + r.h <= y + 56;
}

static std::vector<SimLcdRect> bodies(uint8_t pos) {
  std::vector<SimLcdRect> out;
  for (const SimLcdRect &r : board().lcd.rects)
    if (isBody(r, pos)) out.push_back(r);
  return out;
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
  TEST_ASSERT_TRUE(board().lcd.printed("MIC LIVE"));
}

SIM_TEST(test_preset_labels_become_legends) {
  SimDeck sim;
  TEST_ASSERT_TRUE(sim.loadPresetIntoNvs(SIM_PROJECT_DIR "/presets/mac-daily.json"));
  sim.boot();
  sim.run(kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Chrome"));
  // Real FreeSans9pt advances: "YouTube" is 73 px > the 71 px tile text, so it is cut to "YouTu...".
  // (The old 10 px stand-in font made it fit; Plan 4's Candy legends fix long labels.)
  TEST_ASSERT_TRUE(board().lcd.printed("YouTu"));
  TEST_ASSERT_TRUE(board().lcd.printed("Weather"));
  TEST_ASSERT_FALSE(board().lcd.printed("F14"));
}

SIM_TEST(test_key_press_repaints_only_its_tile) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  board().lcd.rects.clear();
  sim.press(6);
  sim.run(kSettle + 40);
  TEST_ASSERT_FALSE(board().lcd.rects.empty());
  for (const SimLcdRect &r : board().lcd.rects) TEST_ASSERT_TRUE(insideTile(r, 6));
}

SIM_TEST(test_release_fades_back_to_idle_in_four_repaints) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  const uint16_t idle = bodies(6).back().c;
  sim.press(6);
  sim.run(kSettle + 40);
  TEST_ASSERT_NOT_EQUAL(idle, bodies(6).back().c);
  board().lcd.rects.clear();
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
  TEST_ASSERT_EQUAL_HEX16(0xFD20, bodies(1).back().c);  // kOrange, src/display.cpp
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

static bool line2Cleared() {
  for (const SimLcdRect &r : board().lcd.rects)
    if (r.x == 0 && r.y == 214 && r.w == 320 && r.h == 26) return true;
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
  TEST_ASSERT_TRUE(board().lcd.printed("1/2"));
  board().lcd.texts.clear();
  runWithHelper(sim, StatusBoard::kRotateMs);
  TEST_ASSERT_TRUE(board().lcd.printed("CPU 12%"));
  TEST_ASSERT_TRUE(board().lcd.printed("2/2"));
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
  TEST_ASSERT_TRUE(board().lcd.printed("1/1"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_shows_default_legends_f13_to_f24);
  RUN_TEST(test_preset_labels_become_legends);
  RUN_TEST(test_key_press_repaints_only_its_tile);
  RUN_TEST(test_release_fades_back_to_idle_in_four_repaints);
  RUN_TEST(test_app_key_without_helper_flashes_orange);
  RUN_TEST(test_clock_dashes_until_time_then_shows_local_time);
  RUN_TEST(test_slot0_card_in_top_bar);
  RUN_TEST(test_cards_rotate_with_index);
  RUN_TEST(test_helper_silence_clears_cards);
  RUN_TEST(test_banner_takes_line2_then_card_returns);
  RUN_TEST(test_bad_card_frame_is_ignored);
  return UNITY_END();
}
