#pragma once
// Thin Wire glue for the MCP23017. Register logic lives in deck_core/mcp23017.h.
#include <Wire.h>
#include "mcp23017.h"
#include "pins.h"

inline bool mcpWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MCP_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// All writes must ACK. Safe to call again after a hot-plug or brown-out.
inline bool mcpBegin() {
  const McpWrite *w = mcpInitWrites();
  for (size_t i = 0; i < kMcpInitCount; i++)
    if (!mcpWriteReg(w[i].reg, w[i].val)) return false;
  return true;
}

// raw = GPIOA | GPIOB << 8 (SEQOP=0 auto-increment).
inline bool mcpReadRaw(uint16_t &raw) {
  Wire.beginTransmission(MCP_I2C_ADDR);
  Wire.write(MCP_GPIOA);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MCP_I2C_ADDR, (uint8_t)2) != 2) return false;
  uint16_t a = Wire.read();
  uint16_t b = Wire.read();
  raw = a | (b << 8);
  return true;
}
