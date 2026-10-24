#pragma once
#include <stdint.h>

// Sim time. Firmware sees millis()/micros()/delay(); tests move time with SimDeck::run().
struct SimClock {
  uint64_t us = 0;
  uint32_t millis() const { return static_cast<uint32_t>(us / 1000); }
  uint32_t micros() const { return static_cast<uint32_t>(us); }
  void advanceMs(uint32_t ms) { us += static_cast<uint64_t>(ms) * 1000; }
  void advanceUs(uint32_t u) { us += u; }
};
