#pragma once
#include <stddef.h>
#include <stdint.h>
#include "keystroke.h"

enum class KeyType : uint8_t { Keystroke = 0, App = 1, Url = 2, Shortcut = 3 };
constexpr size_t kKeyCount = 12;
constexpr size_t kKeyValueMax = 56;  // fits one 63-byte vendor HID report (helper_proto.h)
constexpr size_t kKeyLabelMax = 16;  // legend text on the LCD tile (2 lines of ~8 chars)

struct KeyEntry {
  KeyType type;
  char value[kKeyValueMax + 1];
  Keystroke ks;  // valid only when type == Keystroke
  char label[kKeyLabelMax + 1];  // empty = derive from value (legend.h)
  uint8_t icon;  // icons.h id, 0 = none
};

enum class KeyMapError : uint8_t { None, BadIndex, BadType, Empty, TooLong, NotPrintable, BadUrl, BadKeystroke, BadIcon };

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
  KeyMapError setLabel(uint8_t i, const char *label);  // printable ASCII, may be empty
  KeyMapError setIcon(uint8_t i, const char *name);  // icons.h name, empty/null = none
  KeyMapError setIconId(uint8_t i, uint8_t id);  // id 0..kIconCount (blob load)
  bool operator==(const KeyMap &o) const;

 private:
  KeyEntry entries_[kKeyCount];
};

uint32_t crc32(const uint8_t *data, size_t n);  // IEEE 802.3
constexpr size_t kKeyMapBlobMax = 4 + 1 + kKeyCount * (4 + kKeyValueMax + kKeyLabelMax) + 4;
// NVS blob: 'SDKM', version 3, 12 x [type, vlen, value, llen, label, icon], CRC32 LE.
// Versions 1 (no labels) and 2 (no icons) still load. Load re-validates every entry.
size_t serializeKeyMap(const KeyMap &m, uint8_t *buf, size_t cap);  // 0 if cap too small
bool deserializeKeyMap(const uint8_t *buf, size_t n, KeyMap &out);  // false -> out untouched
