#pragma once
#include <stdint.h>
#include <string.h>

// MCP23017, IOCON.BANK = 0 (POR). Registers: Microchip DS20001952D Table 3-1 (docs/kb/peripherals.md).
// GPA7/GPB7 are output-only on MCP23017 (DS20001952D pin table): reading GPIO while IODIR bit 7
// is 1 sets outputOnlyAsInput so tests can catch it.
// Sequential mode (SEQOP = 0): the address pointer increments after each byte.
// [unverified: wrap point; this model wraps 0x15 -> 0x00]
// Input without pull-up and not driven reads 0 here (a real pin floats).
struct SimMcp23017 {
  static constexpr uint8_t kRegs = 0x16;
  uint8_t addr = 0x27;  // Waveshare board, A0-A2 open (bench-verified 2026-10-18)
  bool present = true;  // false: NACKs every transfer (unplugged / bad contact)
  uint8_t reg[kRegs] = {};
  uint8_t ptr = 0;
  uint16_t extLow = 0;  // bit n (GPA0..7 = 0..7, GPB0..7 = 8..15) pulled LOW from outside
  bool outputOnlyAsInput = false;

  SimMcp23017() { reset(); }
  void reset() {
    memset(reg, 0, sizeof reg);
    reg[0x00] = 0xFF;  // IODIRA POR
    reg[0x01] = 0xFF;  // IODIRB POR
    ptr = 0;
  }
  void writeReg(uint8_t r, uint8_t v) {
    if (r == 0x12) r = 0x14;  // write to GPIOA writes OLATA
    if (r == 0x13) r = 0x15;
    if (r < kRegs) reg[r] = v;
  }
  uint8_t port(int p) {
    uint8_t iodir = reg[0x00 + p], gppu = reg[0x0C + p], olat = reg[0x14 + p];
    if (iodir & 0x80) outputOnlyAsInput = true;
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
      uint8_t bit = 1u << i;
      bool level;
      if (!(iodir & bit)) level = olat & bit;
      else if (extLow & (1u << (p * 8 + i))) level = false;
      else level = gppu & bit;
      if (level) v |= bit;
    }
    return v;
  }
  uint8_t readReg(uint8_t r) {
    if (r == 0x12) return port(0);
    if (r == 0x13) return port(1);
    return r < kRegs ? reg[r] : 0;
  }
  void next() { ptr = static_cast<uint8_t>((ptr + 1) % kRegs); }
};
