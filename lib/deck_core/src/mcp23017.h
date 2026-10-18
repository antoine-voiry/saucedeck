#pragma once
#include <stddef.h>
#include <stdint.h>

// MCP23017 I2C expander, IOCON.BANK = 0 (POR default). Source: Microchip DS20001952D Table 3-1.
enum : uint8_t {
  MCP_IODIRA = 0x00,
  MCP_IODIRB = 0x01,
  MCP_IOCON = 0x0A,
  MCP_GPPUA = 0x0C,
  MCP_GPPUB = 0x0D,
  MCP_GPIOA = 0x12,  // SEQOP=0 (default): 2-byte read from here returns GPIOA then GPIOB
  MCP_GPIOB = 0x13,
  MCP_OLATA = 0x14,
  MCP_OLATB = 0x15,
};

// Deck bits (active-high pressed mask): bit 0..11 = K1..K12, then the knob switches.
enum : uint16_t {
  DECK_KEYS = 0x0FFF,
  DECK_ENC1_SW = 1u << 12,
  DECK_ENC2_SW = 1u << 13,
};

struct McpWrite {
  uint8_t reg;
  uint8_t val;
};

// Register writes for setup. GPA7/GPB7 are output-only on MCP23017, so they are driven
// LOW (latch first, then direction); every other pin is an input with its weak pull-up.
const McpWrite *mcpInitWrites();
constexpr size_t kMcpInitCount = 6;

// raw = GPIOA | GPIOB << 8 (0 = pin LOW = pressed) -> deck bits.
uint16_t deckPressed(uint16_t raw);

// MCP pin label for a deck bit, "GP??" if out of range.
const char *deckPinName(uint8_t deckBit);
