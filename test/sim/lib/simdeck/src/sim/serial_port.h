#pragma once
#include <stdint.h>
#include <deque>
#include <string>
#include <vector>

// USB CDC as both ends see it. rx = host -> deck, tx = deck -> host.
struct SimSerialPort {
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  uint32_t baud = 0;
  std::string text() const { return std::string(tx.begin(), tx.end()); }
};
