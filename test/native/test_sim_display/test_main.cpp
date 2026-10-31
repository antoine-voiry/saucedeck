// Display scenarios: the real src/ firmware drawing legends and press feedback on the sim LCD.
#include <unity.h>
#include <vector>
#include "keymap.h"
#include "sim_deck.h"
#include "sim_test.h"

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
  TEST_ASSERT_TRUE(board().lcd.printed("YouTube"));
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

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_shows_default_legends_f13_to_f24);
  RUN_TEST(test_preset_labels_become_legends);
  RUN_TEST(test_key_press_repaints_only_its_tile);
  RUN_TEST(test_release_fades_back_to_idle_in_four_repaints);
  RUN_TEST(test_app_key_without_helper_flashes_orange);
  return UNITY_END();
}
