#pragma once
#include <stdint.h>
#include "sim_board.h"

void setup();  // src/main.cpp
void loop();

struct SimFaults {
  void mcpAbsent(bool absent) { board().mcp.present = !absent; }
};

// Test-facing view of the simulated deck. Key positions are physical (1..12, wiring.md).
class SimDeck {
 public:
  void boot() { setup(); }
  // One loop() per simulated millisecond (timing logic only compares millis() values).
  void run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
      loop();
      board().clock.advanceMs(1);
    }
  }
  void press(uint8_t pos) { board().mcp.extLow |= static_cast<uint16_t>(1u << mcpBitForKey(pos)); }
  void release(uint8_t pos) { board().mcp.extLow &= static_cast<uint16_t>(~(1u << mcpBitForKey(pos))); }
  void knobPress(uint8_t n) { board().mcp.extLow |= static_cast<uint16_t>(1u << knobBit(n)); }
  void knobRelease(uint8_t n) { board().mcp.extLow &= static_cast<uint16_t>(~(1u << knobBit(n))); }

  SimHost &host() { return board().host; }
  SimMcp23017 &mcp() { return board().mcp; }
  SimFaults fault() { return SimFaults{}; }

  // Writes a presets/*.json file into NVS the way the firmware stores a confirmed key map.
  bool loadPresetIntoNvs(const char *path);

  // MCP pin bit (GPA0..7 = 0..7, GPB0..7 = 8..15) per physical key, from wiring.md
  // (independent of the firmware's own table in lib/deck_core/src/mcp23017.cpp).
  static uint8_t mcpBitForKey(uint8_t pos);
  static uint8_t knobBit(uint8_t n) { return n == 1 ? 12 : 13; }  // GPB4 / GPB5
};
