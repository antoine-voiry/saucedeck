#pragma once
#include <stddef.h>
#include <stdint.h>
#include "keystroke.h"

enum class KeyType : uint8_t { Keystroke = 0, App = 1, Url = 2, Shortcut = 3 };
constexpr size_t kKeyCount = 12;
constexpr size_t kKeyValueMax = 56;  // fits one 63-byte vendor HID report (helper_proto.h)

struct KeyEntry {
  KeyType type;
  char value[kKeyValueMax + 1];
  Keystroke ks;  // valid only when type == Keystroke
};

enum class KeyMapError : uint8_t { None, BadIndex, BadType, Empty, TooLong, NotPrintable, BadUrl, BadKeystroke };

bool parseKeyType(const char *s, KeyType &out);
const char *keyTypeName(KeyType t);

// What each deck key does. Values are validated here so nothing downstream (HID, helper)
// ever sees free-form input: printable ASCII only, URLs http(s) only, keystrokes parsed.
class KeyMap {
 public:
  KeyMap();
  const KeyEntry &entry(uint8_t i) const { return entries_[i < kKeyCount ? i : 0]; }
  KeyMapError set(uint8_t i, KeyType t, const char *value);
  KeyMapError setFromStrings(uint8_t i, const char *type, const char *value);
  bool operator==(const KeyMap &o) const;

 private:
  KeyEntry entries_[kKeyCount];
};
