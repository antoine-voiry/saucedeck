#include "mcp23017.h"

// MCP raw bit (GPA0..7 = 0..7, GPB0..7 = 8..15) for each deck bit. Deck bits 0..11 are
// key positions in physical order. Positions 5-7 and 9-12 follow the 2026-10-20 rewire of
// the 7 bottom keys (bench-verified). See wiring.md.
static const uint8_t kRawBit[14] = {
    8, 9, 10, 11,  // positions 1-4  = GPB0-GPB3
    2, 1, 0, 14,   // positions 5-8  = GPA2, GPA1, GPA0, GPB6
    3, 6, 5, 4,    // positions 9-12 = GPA3, GPA6, GPA5, GPA4
    12, 13,        // knob 1 / knob 2 switch = GPB4 / GPB5
};
static const char *kNames[14] = {"GPB0", "GPB1", "GPB2", "GPB3", "GPA2", "GPA1", "GPA0",
                                 "GPB6", "GPA3", "GPA6", "GPA5", "GPA4", "GPB4", "GPB5"};

static const McpWrite kInit[kMcpInitCount] = {
    {MCP_OLATA, 0x00}, {MCP_OLATB, 0x00}, {MCP_IODIRA, 0x7F},
    {MCP_IODIRB, 0x7F}, {MCP_GPPUA, 0x7F}, {MCP_GPPUB, 0x7F},
};

const McpWrite *mcpInitWrites() { return kInit; }

uint16_t deckPressed(uint16_t raw) {
  uint16_t low = static_cast<uint16_t>(~raw);
  uint16_t out = 0;
  for (uint8_t i = 0; i < 14; i++)
    if (low & (1u << kRawBit[i])) out |= 1u << i;
  return out;
}

const char *deckPinName(uint8_t deckBit) { return deckBit < 14 ? kNames[deckBit] : "GP??"; }
