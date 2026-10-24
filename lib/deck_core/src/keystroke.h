#pragma once
#include <stdint.h>
#include <string.h>

// Keystroke for a deck key: modifiers + exactly one key, e.g. "cmd+shift+4".
// mods: bit0 Ctrl, bit1 Shift, bit2 Alt, bit3 Cmd -> HID modifier usage 0xE0 + bit.
// usage: HID keyboard usage ID (USB HID Usage Tables, Keyboard page 0x07).
struct Keystroke {
  uint8_t mods;
  uint8_t usage;
};

namespace keystroke_detail {
inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline bool tokenIs(const char *t, size_t n, const char *word) {
  return strlen(word) == n && strncmp(t, word, n) == 0;
}

// Returns modifier bit (0..3) or -1.
inline int modifierBit(const char *t, size_t n) {
  if (tokenIs(t, n, "ctrl")) return 0;
  if (tokenIs(t, n, "shift")) return 1;
  if (tokenIs(t, n, "alt")) return 2;
  if (tokenIs(t, n, "cmd")) return 3;
  return -1;
}

// Returns HID usage or 0.
inline uint8_t keyUsage(const char *t, size_t n) {
  if (n == 1 && t[0] >= 'a' && t[0] <= 'z') return static_cast<uint8_t>(0x04 + (t[0] - 'a'));
  if (n == 1 && t[0] >= '1' && t[0] <= '9') return static_cast<uint8_t>(0x1E + (t[0] - '1'));
  if (n == 1 && t[0] == '0') return 0x27;
  if (tokenIs(t, n, "enter")) return 0x28;
  if (tokenIs(t, n, "esc")) return 0x29;
  if (tokenIs(t, n, "tab")) return 0x2B;
  if (tokenIs(t, n, "space")) return 0x2C;
  if (n == 1 && t[0] == '/') return 0x38;  // US-layout position; cmd+shift+/ = macOS Help menu
  if (n >= 2 && n <= 3 && t[0] == 'f') {
    int num = 0;
    for (size_t i = 1; i < n; i++) {
      if (t[i] < '0' || t[i] > '9') return 0;
      num = num * 10 + (t[i] - '0');
    }
    if (num >= 1 && num <= 12) return static_cast<uint8_t>(0x3A + num - 1);
    if (num >= 13 && num <= 24) return static_cast<uint8_t>(0x68 + num - 13);
  }
  return 0;
}
}  // namespace keystroke_detail

inline bool parseKeystroke(const char *text, Keystroke &out) {
  using namespace keystroke_detail;
  char buf[48];
  size_t len = strnlen(text, sizeof buf);
  if (len == 0 || len >= sizeof buf) return false;
  for (size_t i = 0; i < len; i++) buf[i] = lower(text[i]);
  buf[len] = 0;

  uint8_t mods = 0, usage = 0;
  size_t start = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i < len && buf[i] != '+') continue;
    size_t n = i - start;
    if (n == 0) return false;
    const char *tok = buf + start;
    int bit = modifierBit(tok, n);
    if (bit >= 0) {
      mods |= static_cast<uint8_t>(1u << bit);
    } else {
      uint8_t u = keyUsage(tok, n);
      if (u == 0 || usage != 0) return false;  // unknown token, or a second key
      usage = u;
    }
    start = i + 1;
  }
  if (usage == 0) return false;
  out = {mods, usage};
  return true;
}
