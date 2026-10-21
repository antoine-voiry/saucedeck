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

uint32_t crc32(const uint8_t *data, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= data[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static const uint8_t kMagic[4] = {'S', 'D', 'K', 'M'};
static const uint8_t kBlobVersion = 1;

size_t serializeKeyMap(const KeyMap &m, uint8_t *buf, size_t cap) {
  size_t need = 4 + 1 + 4;
  for (uint8_t i = 0; i < kKeyCount; i++) need += 2 + strlen(m.entry(i).value);
  if (cap < need) return 0;
  memcpy(buf, kMagic, 4);
  size_t p = 4;
  buf[p++] = kBlobVersion;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    const KeyEntry &e = m.entry(i);
    size_t len = strlen(e.value);
    buf[p++] = static_cast<uint8_t>(e.type);
    buf[p++] = static_cast<uint8_t>(len);
    memcpy(buf + p, e.value, len);
    p += len;
  }
  uint32_t c = crc32(buf, p);
  for (int k = 0; k < 4; k++) buf[p++] = static_cast<uint8_t>(c >> (8 * k));
  return p;
}

bool deserializeKeyMap(const uint8_t *buf, size_t n, KeyMap &out) {
  if (n < 4 + 1 + 4 || memcmp(buf, kMagic, 4) != 0 || buf[4] != kBlobVersion) return false;
  uint32_t stored = buf[n - 4] | (buf[n - 3] << 8) | (buf[n - 2] << 16) |
                    (static_cast<uint32_t>(buf[n - 1]) << 24);
  if (crc32(buf, n - 4) != stored) return false;
  KeyMap m;
  size_t p = 5;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    if (p + 2 > n - 4) return false;
    uint8_t type = buf[p++];
    uint8_t len = buf[p++];
    if (len > kKeyValueMax || p + len > n - 4) return false;
    char v[kKeyValueMax + 1];
    memcpy(v, buf + p, len);
    v[len] = 0;
    p += len;
    if (m.set(i, static_cast<KeyType>(type), v) != KeyMapError::None) return false;
  }
  if (p != n - 4) return false;
  out = m;
  return true;
}
