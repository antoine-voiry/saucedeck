#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

// Arduino-ESP32 Preferences subset over board().nvs. Mirrors the core: begin() read-only on a
// namespace that does not exist fails (nvs_open NOT_FOUND); getBytes returns 0 if the value
// is larger than the buffer; writes fail when opened read-only.
class Preferences {
 public:
  bool begin(const char *name, bool readOnly = false);
  void end();
  bool isKey(const char *key);
  size_t getBytes(const char *key, void *buf, size_t maxLen);
  size_t putBytes(const char *key, const void *value, size_t len);

 private:
  std::string ns_;
  bool open_ = false, readOnly_ = false;
};
