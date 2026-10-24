#include "sim_deck.h"
#include <stdio.h>
#include <string>
#include "keymap.h"

uint8_t SimDeck::mcpBitForKey(uint8_t pos) {
  // wiring.md "MCP23017": positions 1-4 = GPB0-GPB3, 5/6/7 = GPA2/GPA1/GPA0, 8 = GPB6,
  // 9/10/11/12 = GPA3/GPA6/GPA5/GPA4 (bench-verified 2026-10-20).
  static const uint8_t kBit[13] = {0, 8, 9, 10, 11, 2, 1, 0, 14, 3, 6, 5, 4};
  return pos >= 1 && pos <= 12 ? kBit[pos] : 15;  // 15 = GPB7, never a key
}

// Pulls the next quoted string after `field` out of the preset file. The preset format is
// ours (presets/*.json: one object per key with "type" and "value"), so a scanner is enough.
static bool nextField(const std::string &s, size_t &pos, const char *field, std::string &out) {
  std::string key = std::string("\"") + field + "\"";
  size_t k = s.find(key, pos);
  if (k == std::string::npos) return false;
  size_t q1 = s.find('"', s.find(':', k) + 1);
  size_t q2 = s.find('"', q1 + 1);
  if (q1 == std::string::npos || q2 == std::string::npos) return false;
  out = s.substr(q1 + 1, q2 - q1 - 1);
  pos = q2 + 1;
  return true;
}

bool SimDeck::loadPresetIntoNvs(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  std::string s;
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);

  KeyMap m;
  size_t pos = s.find("\"keys\"");
  for (uint8_t i = 0; i < kKeyCount; i++) {
    std::string type, value;
    if (!nextField(s, pos, "type", type) || !nextField(s, pos, "value", value)) return false;
    if (m.setFromStrings(i, type.c_str(), value.c_str()) != KeyMapError::None) return false;
  }
  uint8_t blob[kKeyMapBlobMax];
  size_t len = serializeKeyMap(m, blob, sizeof blob);
  if (len == 0) return false;
  board().nvs.ns["saucedeck"]["keymap"].assign(blob, blob + len);  // src/keymap_store.cpp names
  return true;
}
