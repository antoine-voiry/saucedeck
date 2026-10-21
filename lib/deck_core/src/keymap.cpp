#include "keymap.h"
#include <stdio.h>
#include <string.h>

bool parseKeyType(const char *s, KeyType &out) {
  if (strcmp(s, "key") == 0) { out = KeyType::Keystroke; return true; }
  if (strcmp(s, "app") == 0) { out = KeyType::App; return true; }
  if (strcmp(s, "url") == 0) { out = KeyType::Url; return true; }
  if (strcmp(s, "shortcut") == 0) { out = KeyType::Shortcut; return true; }
  return false;
}

const char *keyTypeName(KeyType t) {
  switch (t) {
    case KeyType::Keystroke: return "key";
    case KeyType::App: return "app";
    case KeyType::Url: return "url";
    case KeyType::Shortcut: return "shortcut";
  }
  return "";
}

KeyMap::KeyMap() {
  for (uint8_t i = 0; i < kKeyCount; i++) {
    char v[8];
    snprintf(v, sizeof v, "f%u", 13u + i);
    entries_[i] = KeyEntry{};
    set(i, KeyType::Keystroke, v);
  }
}

KeyMapError KeyMap::set(uint8_t i, KeyType t, const char *value) {
  if (i >= kKeyCount) return KeyMapError::BadIndex;
  if (t != KeyType::Keystroke && t != KeyType::App && t != KeyType::Url && t != KeyType::Shortcut)
    return KeyMapError::BadType;
  size_t n = strnlen(value, kKeyValueMax + 1);
  if (n == 0) return KeyMapError::Empty;
  if (n > kKeyValueMax) return KeyMapError::TooLong;
  for (size_t k = 0; k < n; k++)
    if (value[k] < 0x20 || value[k] > 0x7E) return KeyMapError::NotPrintable;

  Keystroke ks{0, 0};
  if (t == KeyType::Url && strncmp(value, "http://", 7) != 0 && strncmp(value, "https://", 8) != 0)
    return KeyMapError::BadUrl;
  if (t == KeyType::Keystroke && !parseKeystroke(value, ks)) return KeyMapError::BadKeystroke;

  KeyEntry &e = entries_[i];
  e.type = t;
  memcpy(e.value, value, n);
  e.value[n] = 0;
  e.ks = ks;
  return KeyMapError::None;
}

KeyMapError KeyMap::setFromStrings(uint8_t i, const char *type, const char *value) {
  KeyType t;
  if (!parseKeyType(type, t)) return KeyMapError::BadType;
  return set(i, t, value);
}

bool KeyMap::operator==(const KeyMap &o) const {
  for (uint8_t i = 0; i < kKeyCount; i++)
    if (entries_[i].type != o.entries_[i].type || strcmp(entries_[i].value, o.entries_[i].value) != 0)
      return false;
  return true;
}
