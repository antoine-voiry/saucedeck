#include "sim_board.h"

SimBoard &board() {
  static SimBoard b;
  return b;
}

uint8_t SimBoard::i2cWrite(uint8_t addr, const uint8_t *data, size_t n) {
  if (addr != mcp.addr || !mcp.present) return 2;
  if (n == 0) return 0;  // address-only probe (bus scan)
  mcp.ptr = data[0] % SimMcp23017::kRegs;
  for (size_t i = 1; i < n; i++) {
    mcp.writeReg(mcp.ptr, data[i]);
    mcp.next();
  }
  return 0;
}

size_t SimBoard::i2cRead(uint8_t addr, uint8_t *out, size_t n) {
  if (addr != mcp.addr || !mcp.present) return 0;
  for (size_t i = 0; i < n; i++) {
    out[i] = mcp.readReg(mcp.ptr);
    mcp.next();
  }
  return n;
}
