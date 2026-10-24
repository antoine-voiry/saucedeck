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

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_harness_a_mutates_board);
  RUN_TEST(test_harness_b_sees_fresh_board);
  RUN_TEST(test_boot_configures_mcp_inputs_pullups_and_output_only_pins);
  return UNITY_END();
}
