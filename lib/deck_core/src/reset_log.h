#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Why the deck last reset, hardware-free. reason = esp_reset_reason_t value (ESP-IDF 5.x
// esp_system.h, same order); upS = how long the previous run had been up, 0 = unknown.
struct ResetEntry {
  uint8_t reason;
  uint32_t upS;
};

inline const char *resetName(uint8_t reason) {
  static const char *const kNames[] = {"unknown", "power on", "ext pin", "software", "panic", "int wdt",
                                       "task wdt", "wdt", "deep sleep", "brownout", "sdio", "usb",
                                       "jtag", "efuse", "power glitch", "cpu lockup"};
  return reason < sizeof kNames / sizeof kNames[0] ? kNames[reason] : kNames[0];
}

// Power-on is the normal boot; everything else is worth a banner.
inline bool resetNotable(uint8_t reason) { return reason != 1; }

// "panic, up 18.2 h" / "brownout, up 59 min" / "software, up 59 s" / "brownout"
inline void formatEntry(const ResetEntry &e, char *out, size_t n) {
  const char *name = resetName(e.reason);
  if (e.upS == 0) {
    snprintf(out, n, "%s", name);
  } else if (e.upS >= 3600) {
    uint32_t tenths = e.upS / 360;
    snprintf(out, n, "%s, up %lu.%lu h", name, static_cast<unsigned long>(tenths / 10),
             static_cast<unsigned long>(tenths % 10));
  } else if (e.upS >= 60) {
    snprintf(out, n, "%s, up %lu min", name, static_cast<unsigned long>(e.upS / 60));
  } else {
    snprintf(out, n, "%s, up %lu s", name, static_cast<unsigned long>(e.upS));
  }
}

inline void formatReset(const ResetEntry &e, char *out, size_t n) {
  char entry[32];
  formatEntry(e, entry, sizeof entry);
  snprintf(out, n, "Reset: %s", entry);
}

// Last kSize resets, newest first. Blob: version, count, then count x (reason, upS LE).
class ResetLog {
 public:
  static constexpr uint8_t kSize = 8;
  static constexpr uint8_t kVersion = 1;
  static constexpr size_t kBytes = 2 + kSize * 5;

  void push(const ResetEntry &e) {
    if (count_ < kSize) count_++;
    memmove(&items_[1], &items_[0], (count_ - 1) * sizeof items_[0]);
    items_[0] = e;
  }

  uint8_t count() const { return count_; }
  const ResetEntry &at(uint8_t i) const { return items_[i]; }

  size_t serialize(uint8_t *buf, size_t n) const {
    if (n < kBytes) return 0;
    memset(buf, 0, kBytes);
    buf[0] = kVersion;
    buf[1] = count_;
    for (uint8_t i = 0; i < count_; i++) {
      uint8_t *p = buf + 2 + i * 5;
      p[0] = items_[i].reason;
      for (uint8_t b = 0; b < 4; b++) p[1 + b] = static_cast<uint8_t>(items_[i].upS >> (8 * b));
    }
    return kBytes;
  }

  bool load(const uint8_t *buf, size_t n) {
    count_ = 0;
    if (n < kBytes || buf[0] != kVersion || buf[1] > kSize) return false;
    for (uint8_t i = 0; i < buf[1]; i++) {
      const uint8_t *p = buf + 2 + i * 5;
      uint32_t up = 0;
      for (uint8_t b = 0; b < 4; b++) up |= static_cast<uint32_t>(p[1 + b]) << (8 * b);
      items_[i] = {p[0], up};
    }
    count_ = buf[1];
    return true;
  }

  // "panic, up 18.2 h; power on" (newest first), "none" when empty. Truncated to n - 1.
  void summary(char *out, size_t n) const {
    if (n == 0) return;
    snprintf(out, n, "%s", count_ ? "" : "none");
    for (uint8_t i = 0; i < count_; i++) {
      char entry[32];
      formatEntry(items_[i], entry, sizeof entry);
      size_t used = strlen(out);
      snprintf(out + used, n - used, "%s%s", i ? "; " : "", entry);
    }
  }

 private:
  ResetEntry items_[kSize] = {};
  uint8_t count_ = 0;
};
