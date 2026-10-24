// Scenario tests: the real src/ firmware on the simulated board (test/sim/lib/simdeck).
#include <stdio.h>
#include <unity.h>
#include <vector>
#include "mcp23017.h"
#include "sim_deck.h"
#include "sim_test.h"

void setUp() {}
void tearDown() {}

// --- harness ---

SIM_TEST(test_harness_a_mutates_board) {
  board().mcp.extLow = 0xFFFF;
  board().clock.advanceMs(1234);
  SimDeck sim;
  sim.boot();
}

// Runs after test_harness_a: passes only if each SIM_TEST gets a fresh process.
SIM_TEST(test_harness_b_sees_fresh_board) {
  TEST_ASSERT_EQUAL_HEX16(0, board().mcp.extLow);
  TEST_ASSERT_EQUAL_UINT32(0, board().clock.millis());
  TEST_ASSERT_EQUAL_HEX8(0xFF, board().mcp.reg[MCP_IODIRA]);  // POR value, setup() never ran here
}

// --- boot ---

SIM_TEST(test_boot_configures_mcp_inputs_pullups_and_output_only_pins) {
  SimDeck sim;
  sim.boot();
  SimMcp23017 &m = sim.mcp();
  TEST_ASSERT_EQUAL_HEX8(0x7F, m.reg[MCP_IODIRA]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, m.reg[MCP_IODIRB]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, m.reg[MCP_GPPUA]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, m.reg[MCP_GPPUB]);
  TEST_ASSERT_EQUAL_HEX8(0x00, m.reg[MCP_OLATA]);
  TEST_ASSERT_EQUAL_HEX8(0x00, m.reg[MCP_OLATB]);
  sim.run(50);
  TEST_ASSERT_FALSE(m.outputOnlyAsInput);
  TEST_ASSERT_TRUE(sim.host().kb.empty());  // idle deck sends nothing
}

// --- keys, factory default map (F13..F24) ---

static constexpr uint32_t kSettle = 30;  // > 25 ms debounce (lib/deck_core/src/debounce.h)

SIM_TEST(test_default_map_each_key_sends_its_f_key) {
  SimDeck sim;
  sim.boot();
  for (uint8_t pos = 1; pos <= 12; pos++) {
    char msg[24];
    snprintf(msg, sizeof msg, "key %u", pos);
    sim.press(pos);
    sim.run(kSettle);
    TEST_ASSERT_TRUE_MESSAGE(sim.host().keyDown(static_cast<uint8_t>(0x68 + pos - 1)), msg);
    sim.release(pos);
    sim.run(kSettle);
    TEST_ASSERT_TRUE_MESSAGE(sim.host().allKeysUp(), msg);
  }
}

SIM_TEST(test_bounce_shorter_than_debounce_sends_nothing) {
  SimDeck sim;
  sim.boot();
  sim.press(3);
  sim.run(10);
  sim.release(3);
  sim.run(kSettle);
  TEST_ASSERT_TRUE(sim.host().kb.empty());
}

SIM_TEST(test_two_keys_held_then_one_released) {
  SimDeck sim;
  sim.boot();
  sim.press(1);
  sim.press(2);
  sim.run(kSettle);
  TEST_ASSERT_TRUE(sim.host().keyDown(0x68));
  TEST_ASSERT_TRUE(sim.host().keyDown(0x69));
  sim.release(1);
  sim.run(kSettle);
  TEST_ASSERT_FALSE(sim.host().keyDown(0x68));
  TEST_ASSERT_TRUE(sim.host().keyDown(0x69));
}

// --- keys, presets/mac-daily.json loaded from NVS ---

static void bootWithMacDaily(SimDeck &sim) {
  TEST_ASSERT_TRUE(sim.loadPresetIntoNvs(SIM_PROJECT_DIR "/presets/mac-daily.json"));
  sim.boot();
}

SIM_TEST(test_mac_daily_keys_1_to_8_send_f13_to_f20) {
  SimDeck sim;
  bootWithMacDaily(sim);
  for (uint8_t pos = 1; pos <= 8; pos++) {
    sim.press(pos);
    sim.run(kSettle);
    TEST_ASSERT_TRUE(sim.host().keyDown(static_cast<uint8_t>(0x68 + pos - 1)));
    TEST_ASSERT_EQUAL_HEX8(0, sim.host().mods());
    sim.release(pos);
    sim.run(kSettle);
  }
}

SIM_TEST(test_mac_daily_key_9_sends_cmd_h) {
  SimDeck sim;
  bootWithMacDaily(sim);
  sim.press(9);
  sim.run(kSettle);
  TEST_ASSERT_EQUAL_HEX8(0x08, sim.host().mods());  // left GUI = bit 3
  TEST_ASSERT_TRUE(sim.host().keyDown(0x0B));       // H
}

SIM_TEST(test_mac_daily_keys_10_to_12_send_ctrl_alt_cmd_f13_to_f15) {
  SimDeck sim;
  bootWithMacDaily(sim);
  for (uint8_t pos = 10; pos <= 12; pos++) {
    sim.press(pos);
    sim.run(kSettle);
    TEST_ASSERT_EQUAL_HEX8(0x0D, sim.host().mods());  // ctrl bit0 | alt bit2 | GUI bit3
    TEST_ASSERT_TRUE(sim.host().keyDown(static_cast<uint8_t>(0x68 + pos - 10)));
    sim.release(pos);
    sim.run(kSettle);
    TEST_ASSERT_TRUE(sim.host().allKeysUp());
  }
}

// The Mac must see the modifier before the key and the key gone before the modifier,
// or a chord can arrive as a bare key.
SIM_TEST(test_chord_modifier_before_key_and_after_key_on_release) {
  SimDeck sim;
  bootWithMacDaily(sim);
  size_t at = sim.host().kb.size();
  sim.press(9);
  sim.run(kSettle);
  sim.release(9);
  sim.run(kSettle);
  const std::vector<KbReport> &r = sim.host().kb;
  TEST_ASSERT_EQUAL(at + 4, r.size());
  TEST_ASSERT_EQUAL_HEX8(0x08, r[at].mods);
  TEST_ASSERT_EQUAL_HEX8(0x00, r[at].keys[0]);
  TEST_ASSERT_EQUAL_HEX8(0x08, r[at + 1].mods);
  TEST_ASSERT_EQUAL_HEX8(0x0B, r[at + 1].keys[0]);
  TEST_ASSERT_EQUAL_HEX8(0x08, r[at + 2].mods);
  TEST_ASSERT_EQUAL_HEX8(0x00, r[at + 2].keys[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, r[at + 3].mods);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_harness_a_mutates_board);
  RUN_TEST(test_harness_b_sees_fresh_board);
  RUN_TEST(test_boot_configures_mcp_inputs_pullups_and_output_only_pins);
  RUN_TEST(test_default_map_each_key_sends_its_f_key);
  RUN_TEST(test_bounce_shorter_than_debounce_sends_nothing);
  RUN_TEST(test_two_keys_held_then_one_released);
  RUN_TEST(test_mac_daily_keys_1_to_8_send_f13_to_f20);
  RUN_TEST(test_mac_daily_key_9_sends_cmd_h);
  RUN_TEST(test_mac_daily_keys_10_to_12_send_ctrl_alt_cmd_f13_to_f15);
  RUN_TEST(test_chord_modifier_before_key_and_after_key_on_release);
  return UNITY_END();
}
