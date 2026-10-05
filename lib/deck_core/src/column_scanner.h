#pragma once
#include <stdint.h>

// Single-return-line key scan for an open-drain expander (PCF8575C has no pull-ups).
// Each expander pin is a column, driven LOW one at a time; all button commons join on
// one MCU input with pull-up. Sense LOW while column N is driven => button N pressed.
class ColumnScanner {
 public:
  explicit ColumnScanner(uint16_t columnMask) : mask_(columnMask) { column_ = next(15); }

  uint8_t column() const { return column_; }

  // Value to write to the expander: every pin released except the active column.
  uint16_t driveWord() const { return mask_ ? static_cast<uint16_t>(~(1u << column_)) : 0xFFFF; }

  // Sense level read while driveWord() was applied. Returns true when a full scan completed.
  bool feed(bool senseLow) {
    if (!mask_) return true;
    if (senseLow) scratch_ |= 1u << column_;
    uint8_t n = next(column_);
    bool wrapped = n <= column_;
    column_ = n;
    if (wrapped) {
      pressed_ = scratch_;
      scratch_ = 0;
    }
    return wrapped;
  }

  // Active-high mask of pressed columns from the last complete scan.
  uint16_t pressed() const { return pressed_; }

 private:
  uint8_t next(uint8_t from) const {
    if (!mask_) return 0;
    for (uint8_t i = 1; i <= 16; i++) {
      uint8_t c = (from + i) & 15;
      if (mask_ & (1u << c)) return c;
    }
    return 0;
  }

  uint16_t mask_;
  uint8_t column_ = 0;
  uint16_t scratch_ = 0;
  uint16_t pressed_ = 0;
};
