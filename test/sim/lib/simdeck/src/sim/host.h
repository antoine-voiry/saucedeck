#pragma once
#include <stdint.h>
#include <vector>

// What the Mac receives. Boot keyboard report: modifiers + 6 key slots (USB HID 1.11, B.1).
struct KbReport {
  uint8_t mods;
  uint8_t keys[6];
};

struct SimHost {
  std::vector<KbReport> kb;
  std::vector<uint16_t> consumer;  // usage on press, 0 on release

  KbReport current() const { return kb.empty() ? KbReport{0, {0, 0, 0, 0, 0, 0}} : kb.back(); }
  bool keyDown(uint8_t usage) const {
    KbReport r = current();
    for (uint8_t k : r.keys)
      if (k == usage) return true;
    return false;
  }
  uint8_t mods() const { return current().mods; }
  bool allKeysUp() const {
    KbReport r = current();
    if (r.mods) return false;
    for (uint8_t k : r.keys)
      if (k) return false;
    return true;
  }
};
