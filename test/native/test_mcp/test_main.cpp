#include <unity.h>
#include "mcp23017.h"

void setUp() {}
void tearDown() {}

// raw = GPIOA | GPIOB << 8, active-low (pull-ups on, buttons to GND)
static uint16_t lowOn(uint16_t bits) { return static_cast<uint16_t>(~bits); }

static uint16_t GPA(int n) { return 1u << n; }
static uint16_t GPB(int n) { return 1u << (8 + n); }

void test_idle_reads_nothing_pressed() {
  TEST_ASSERT_EQUAL_HEX16(0, deckPressed(0xFFFF));
}

// Physical key order (bench 2026-10-18): position 1..12 -> MCP pin.
static const uint16_t kPhysical[12] = {
    GPB(0), GPB(1), GPB(2), GPB(3),  // positions 1-4
    GPA(2), GPA(1), GPA(0), GPB(6),  // positions 5-8  (rewired 2026-10-20)
    GPA(3), GPA(6), GPA(5), GPA(4),  // positions 9-12 (rewired 2026-10-20)
};

void test_keys_follow_physical_order() {
  for (int k = 0; k < 12; k++) TEST_ASSERT_EQUAL_HEX16(1u << k, deckPressed(lowOn(kPhysical[k])));
}

void test_encoder_switches_on_gpb4_gpb5() {
  TEST_ASSERT_EQUAL_HEX16(DECK_ENC1_SW, deckPressed(lowOn(GPB(4))));
  TEST_ASSERT_EQUAL_HEX16(DECK_ENC2_SW, deckPressed(lowOn(GPB(5))));
}

// GPA7/GPB7 are output-only on MCP23017 (DS20001952D): never reported.
void test_gpa7_gpb7_ignored() {
  TEST_ASSERT_EQUAL_HEX16(0, deckPressed(lowOn(GPA(7) | GPB(7))));
}

void test_all_pressed_gives_14_bits() {
  TEST_ASSERT_EQUAL_HEX16(0x3FFF, deckPressed(0x0000));
}

void test_init_bit7_outputs_rest_inputs_with_pullups() {
  const McpWrite *w = mcpInitWrites();
  struct { uint8_t reg, val; } expect[] = {
      {MCP_OLATA, 0x00}, {MCP_OLATB, 0x00}, {MCP_IODIRA, 0x7F},
      {MCP_IODIRB, 0x7F}, {MCP_GPPUA, 0x7F}, {MCP_GPPUB, 0x7F},
  };
  TEST_ASSERT_EQUAL_UINT(6, kMcpInitCount);
  for (size_t i = 0; i < kMcpInitCount; i++) {
    TEST_ASSERT_EQUAL_HEX8(expect[i].reg, w[i].reg);
    TEST_ASSERT_EQUAL_HEX8(expect[i].val, w[i].val);
  }
}

void test_register_addresses_bank0() {
  TEST_ASSERT_EQUAL_HEX8(0x00, MCP_IODIRA);
  TEST_ASSERT_EQUAL_HEX8(0x0C, MCP_GPPUA);
  TEST_ASSERT_EQUAL_HEX8(0x12, MCP_GPIOA);
  TEST_ASSERT_EQUAL_HEX8(0x14, MCP_OLATA);
}

void test_pin_names_follow_deck_bits() {
  TEST_ASSERT_EQUAL_STRING("GPB0", deckPinName(0));
  TEST_ASSERT_EQUAL_STRING("GPB3", deckPinName(3));
  TEST_ASSERT_EQUAL_STRING("GPA2", deckPinName(4));
  TEST_ASSERT_EQUAL_STRING("GPB6", deckPinName(7));
  TEST_ASSERT_EQUAL_STRING("GPA3", deckPinName(8));
  TEST_ASSERT_EQUAL_STRING("GPA4", deckPinName(11));
  TEST_ASSERT_EQUAL_STRING("GPB4", deckPinName(12));
  TEST_ASSERT_EQUAL_STRING("GPB5", deckPinName(13));
  TEST_ASSERT_EQUAL_STRING("GP??", deckPinName(14));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_reads_nothing_pressed);
  RUN_TEST(test_keys_follow_physical_order);
  RUN_TEST(test_encoder_switches_on_gpb4_gpb5);
  RUN_TEST(test_gpa7_gpb7_ignored);
  RUN_TEST(test_all_pressed_gives_14_bits);
  RUN_TEST(test_init_bit7_outputs_rest_inputs_with_pullups);
  RUN_TEST(test_register_addresses_bank0);
  RUN_TEST(test_pin_names_follow_deck_bits);
  return UNITY_END();
}
