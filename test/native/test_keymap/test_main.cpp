#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "keystroke.h"
#include "keymap.h"
#include "icons.h"

void setUp() {}
void tearDown() {}

static Keystroke ks(const char *t, bool expectOk = true) {
  Keystroke k{0xFF, 0xFF};
  TEST_ASSERT_EQUAL(expectOk, parseKeystroke(t, k));
  return k;
}

void test_ks_single_letter() {
  Keystroke k = ks("a");
  TEST_ASSERT_EQUAL_HEX8(0x00, k.mods);
  TEST_ASSERT_EQUAL_HEX8(0x04, k.usage);
}

void test_ks_cmd_shift_4() {
  Keystroke k = ks("cmd+shift+4");
  TEST_ASSERT_EQUAL_HEX8(0x0A, k.mods);  // shift bit1 | cmd bit3
  TEST_ASSERT_EQUAL_HEX8(0x21, k.usage);
}

void test_ks_all_modifiers_case_insensitive() {
  Keystroke k = ks("CTRL+Alt+Shift+Cmd+z");
  TEST_ASSERT_EQUAL_HEX8(0x0F, k.mods);
  TEST_ASSERT_EQUAL_HEX8(0x1D, k.usage);
}

void test_ks_digits_and_named_keys() {
  TEST_ASSERT_EQUAL_HEX8(0x1E, ks("1").usage);
  TEST_ASSERT_EQUAL_HEX8(0x27, ks("0").usage);
  TEST_ASSERT_EQUAL_HEX8(0x28, ks("enter").usage);
  TEST_ASSERT_EQUAL_HEX8(0x29, ks("esc").usage);
  TEST_ASSERT_EQUAL_HEX8(0x2B, ks("tab").usage);
  TEST_ASSERT_EQUAL_HEX8(0x2C, ks("space").usage);
}

void test_ks_slash_for_help_menu() {
  Keystroke k = ks("cmd+shift+/");  // macOS Help menu search (US layout position)
  TEST_ASSERT_EQUAL_HEX8(0x0A, k.mods);
  TEST_ASSERT_EQUAL_HEX8(0x38, k.usage);
}

void test_ks_function_keys() {
  TEST_ASSERT_EQUAL_HEX8(0x3A, ks("f1").usage);
  TEST_ASSERT_EQUAL_HEX8(0x45, ks("f12").usage);
  TEST_ASSERT_EQUAL_HEX8(0x68, ks("f13").usage);
  TEST_ASSERT_EQUAL_HEX8(0x73, ks("f24").usage);
}

void test_ks_rejects_bad_input() {
  ks("", false);
  ks("cmd", false);            // no key
  ks("a+b", false);            // two keys
  ks("cmd++a", false);         // empty token
  ks("hyper+a", false);        // unknown modifier
  ks("f25", false);
  ks("f0", false);
  ks("cmd+shift+4 ", false);   // trailing space
  ks("\xc3\xa9", false);       // non-ASCII
}

void test_ks_rejects_overlong_input() {
  char longText[80];
  for (int i = 0; i < 79; i++) longText[i] = 'a';
  longText[79] = 0;
  ks(longText, false);
}

// --- KeyMap ---

void test_keymap_defaults_are_f13_to_f24() {
  KeyMap m;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    TEST_ASSERT_TRUE(m.entry(i).type == KeyType::Keystroke);
    TEST_ASSERT_EQUAL_HEX8(0x68 + i, m.entry(i).ks.usage);
    TEST_ASSERT_EQUAL_HEX8(0, m.entry(i).ks.mods);
  }
  TEST_ASSERT_EQUAL_STRING("f13", m.entry(0).value);
}

void test_keymap_set_app_url_shortcut() {
  KeyMap m;
  TEST_ASSERT_TRUE(m.set(0, KeyType::App, "Calculator") == KeyMapError::None);
  TEST_ASSERT_TRUE(m.set(1, KeyType::Url, "https://www.linkedin.com") == KeyMapError::None);
  TEST_ASSERT_TRUE(m.set(2, KeyType::Shortcut, "Start focus") == KeyMapError::None);
  TEST_ASSERT_EQUAL_STRING("Calculator", m.entry(0).value);
  TEST_ASSERT_TRUE(m.entry(1).type == KeyType::Url);
}

void test_keymap_set_keystroke_parses() {
  KeyMap m;
  TEST_ASSERT_TRUE(m.set(7, KeyType::Keystroke, "cmd+shift+4") == KeyMapError::None);
  TEST_ASSERT_EQUAL_HEX8(0x0A, m.entry(7).ks.mods);
  TEST_ASSERT_EQUAL_HEX8(0x21, m.entry(7).ks.usage);
}

void test_keymap_rejections_leave_entry_unchanged() {
  KeyMap m;
  TEST_ASSERT_TRUE(m.set(12, KeyType::App, "x") == KeyMapError::BadIndex);
  TEST_ASSERT_TRUE(m.set(0, KeyType::App, "") == KeyMapError::Empty);
  TEST_ASSERT_TRUE(m.set(0, KeyType::App, "Calc\nulator") == KeyMapError::NotPrintable);
  TEST_ASSERT_TRUE(m.set(0, KeyType::Url, "ftp://x") == KeyMapError::BadUrl);
  TEST_ASSERT_TRUE(m.set(0, KeyType::Url, "javascript:alert(1)") == KeyMapError::BadUrl);
  TEST_ASSERT_TRUE(m.set(0, KeyType::Keystroke, "a+b") == KeyMapError::BadKeystroke);
  TEST_ASSERT_TRUE(m.set(0, static_cast<KeyType>(9), "x") == KeyMapError::BadType);
  TEST_ASSERT_TRUE(m.entry(0).type == KeyType::Keystroke);
  TEST_ASSERT_EQUAL_STRING("f13", m.entry(0).value);
}

void test_keymap_value_length_limit() {
  KeyMap m;
  char v[kKeyValueMax + 2];
  memset(v, 'a', kKeyValueMax);
  v[kKeyValueMax] = 0;
  TEST_ASSERT_TRUE(m.set(0, KeyType::App, v) == KeyMapError::None);
  v[kKeyValueMax] = 'a';
  v[kKeyValueMax + 1] = 0;
  TEST_ASSERT_TRUE(m.set(0, KeyType::App, v) == KeyMapError::TooLong);
}

void test_keymap_set_from_strings() {
  KeyMap m;
  TEST_ASSERT_TRUE(m.setFromStrings(3, "app", "Visual Studio Code") == KeyMapError::None);
  TEST_ASSERT_TRUE(m.entry(3).type == KeyType::App);
  TEST_ASSERT_TRUE(m.setFromStrings(3, "macro", "x") == KeyMapError::BadType);
  KeyType t;
  TEST_ASSERT_TRUE(parseKeyType("shortcut", t) && t == KeyType::Shortcut);
  TEST_ASSERT_EQUAL_STRING("url", keyTypeName(KeyType::Url));
}

void test_keymap_equality() {
  KeyMap a, b;
  TEST_ASSERT_TRUE(a == b);
  b.set(5, KeyType::App, "Notes");
  TEST_ASSERT_FALSE(a == b);
}

// --- KeyMap blob ---

// Same 12 entries as presets/mac-daily.json (Hammerspoon: tools/mac/hammerspoon.lua).
// macOS drops F21-F24 (no virtual keycode), so keys 10-12 use ctrl+alt+cmd+F13..F15.
void test_keymap_mac_daily_preset_is_valid() {
  static const char *const kPreset[kKeyCount] = {
      "f13", "f14", "f15", "f16", "f17", "f18", "f19", "f20",
      "cmd+h", "ctrl+alt+cmd+f13", "ctrl+alt+cmd+f14", "ctrl+alt+cmd+f15",
  };
  KeyMap m;
  for (uint8_t i = 0; i < kKeyCount; i++)
    TEST_ASSERT_EQUAL_MESSAGE(KeyMapError::None, m.setFromStrings(i, "key", kPreset[i]), kPreset[i]);
  TEST_ASSERT_EQUAL_HEX8(0x0D, m.entry(9).ks.mods);  // ctrl bit0 | alt bit2 | cmd bit3
  TEST_ASSERT_EQUAL_HEX8(0x68, m.entry(9).ks.usage);  // F13
}

void test_crc32_check_value() {
  const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32(s, sizeof s));
}

void test_blob_roundtrip() {
  KeyMap m;
  m.set(0, KeyType::App, "Calculator");
  m.set(4, KeyType::Url, "https://www.linkedin.com");
  m.set(7, KeyType::Keystroke, "cmd+shift+4");
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  KeyMap back;
  TEST_ASSERT_TRUE(deserializeKeyMap(buf, n, back));
  TEST_ASSERT_TRUE(back == m);
  TEST_ASSERT_EQUAL_HEX8(0x21, back.entry(7).ks.usage);  // keystroke re-parsed
}

void test_blob_rejects_corruption_and_keeps_target() {
  KeyMap m;
  m.set(0, KeyType::App, "Calculator");
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  KeyMap target;
  target.set(1, KeyType::App, "Notes");
  KeyMap before = target;

  buf[10] ^= 0x01;  // payload bit flip -> CRC mismatch
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target));
  buf[10] ^= 0x01;
  buf[0] = 'X';  // magic
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target));
  buf[0] = 'S';
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n - 1, target));  // truncated
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, 0, target));
  TEST_ASSERT_TRUE(target == before);
}

void test_blob_rejects_valid_crc_but_invalid_entry() {
  // Attacker-crafted blob with a correct CRC must still pass KeyMap validation
  KeyMap m;
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  buf[5] = 2;  // key 0 type -> Url, value "f13" is not http(s)
  uint32_t c = crc32(buf, n - 4);
  buf[n - 4] = c & 0xFF; buf[n - 3] = (c >> 8) & 0xFF; buf[n - 2] = (c >> 16) & 0xFF; buf[n - 1] = c >> 24;
  KeyMap target;
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target));
}

void test_blob_small_buffer_returns_zero() {
  KeyMap m;
  uint8_t buf[16];
  TEST_ASSERT_EQUAL(0, serializeKeyMap(m, buf, sizeof buf));
}

// --- labels (legend text on the LCD) ---

void test_label_default_empty() {
  KeyMap m;
  for (uint8_t i = 0; i < kKeyCount; i++) TEST_ASSERT_EQUAL_STRING("", m.entry(i).label);
}

void test_label_set_and_clear() {
  KeyMap m;
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setLabel(2, "Chrome"));
  TEST_ASSERT_EQUAL_STRING("Chrome", m.entry(2).label);
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setLabel(2, ""));
  TEST_ASSERT_EQUAL_STRING("", m.entry(2).label);
}

void test_label_rejections_leave_label_unchanged() {
  KeyMap m;
  m.setLabel(1, "Mail");
  TEST_ASSERT_EQUAL(KeyMapError::TooLong, m.setLabel(1, "abcdefghijklmnopq"));  // 17
  TEST_ASSERT_EQUAL(KeyMapError::NotPrintable, m.setLabel(1, "Ma\x01il"));
  TEST_ASSERT_EQUAL(KeyMapError::NotPrintable, m.setLabel(1, "Caf\xC3\xA9"));  // UTF-8
  TEST_ASSERT_EQUAL(KeyMapError::BadIndex, m.setLabel(12, "x"));
  // WiFiManager inserts param values into HTML unescaped.
  const char *const kBad[] = {"a\"b", "a<b", "a>b", "a&b"};
  for (const char *bad : kBad)
    TEST_ASSERT_EQUAL(KeyMapError::NotPrintable, m.setLabel(1, bad));
  TEST_ASSERT_EQUAL_STRING("Mail", m.entry(1).label);
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setLabel(1, "abcdefghijklmnop"));  // 16 = max
}

void test_label_survives_value_change() {
  KeyMap m;
  m.setLabel(0, "Shot");
  TEST_ASSERT_EQUAL(KeyMapError::None, m.set(0, KeyType::App, "Screenshot"));
  TEST_ASSERT_EQUAL_STRING("Shot", m.entry(0).label);
}

void test_keymap_equality_includes_label() {
  KeyMap a, b;
  b.setLabel(5, "Settings");
  TEST_ASSERT_FALSE(a == b);
  a.setLabel(5, "Settings");
  TEST_ASSERT_TRUE(a == b);
}

void test_blob_roundtrip_keeps_labels() {
  KeyMap m;
  m.set(1, KeyType::App, "Google Chrome");
  m.setLabel(1, "Chrome");
  m.setLabel(11, "Bambu Studio");
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_UINT8(3, buf[4]);  // blob version
  KeyMap back;
  TEST_ASSERT_TRUE(deserializeKeyMap(buf, n, back));
  TEST_ASSERT_TRUE(back == m);
  TEST_ASSERT_EQUAL_STRING("Bambu Studio", back.entry(11).label);
}

void test_blob_full_map_fits_max() {
  // Every value and label at its maximum length must still serialize.
  KeyMap m;
  char v[kKeyValueMax + 1], l[kKeyLabelMax + 1];
  memset(v, 'a', kKeyValueMax);
  v[kKeyValueMax] = 0;
  memset(l, 'b', kKeyLabelMax);
  l[kKeyLabelMax] = 0;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    TEST_ASSERT_EQUAL(KeyMapError::None, m.set(i, KeyType::App, v));
    TEST_ASSERT_EQUAL(KeyMapError::None, m.setLabel(i, l));
  }
  uint8_t buf[kKeyMapBlobMax];
  TEST_ASSERT_EQUAL(kKeyMapBlobMax, serializeKeyMap(m, buf, sizeof buf));
}

// Factory map as the firmware stored it before 2026-10-31 (blob version 1, no labels).
static size_t v1Blob(uint8_t *buf) {
  const uint8_t head[] = {'S', 'D', 'K', 'M', 1};
  memcpy(buf, head, sizeof head);
  size_t p = sizeof head;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    char v[8];
    size_t n = static_cast<size_t>(snprintf(v, sizeof v, "f%u", 13u + i));
    buf[p++] = 0;  // KeyType::Keystroke
    buf[p++] = static_cast<uint8_t>(n);
    memcpy(buf + p, v, n);
    p += n;
  }
  uint32_t c = crc32(buf, p);
  for (int k = 0; k < 4; k++) buf[p++] = static_cast<uint8_t>(c >> (8 * k));
  return p;
}

void test_blob_v1_still_loads_with_empty_labels() {
  uint8_t buf[kKeyMapBlobMax];
  size_t n = v1Blob(buf);
  KeyMap back;
  back.setLabel(0, "junk");
  TEST_ASSERT_TRUE(deserializeKeyMap(buf, n, back));
  TEST_ASSERT_EQUAL_STRING("f13", back.entry(0).value);
  TEST_ASSERT_EQUAL_STRING("f24", back.entry(11).value);
  TEST_ASSERT_EQUAL_STRING("", back.entry(0).label);
}

void test_blob_rejects_valid_crc_but_unprintable_label() {
  KeyMap m;
  m.setLabel(0, "ab");
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  // key 0: [5]=type [6]=vlen [7..9]="f13" [10]=llen [11..12]="ab"
  TEST_ASSERT_EQUAL_UINT8(2, buf[10]);
  buf[11] = 0x01;
  uint32_t c = crc32(buf, n - 4);
  buf[n - 4] = c & 0xFF; buf[n - 3] = (c >> 8) & 0xFF; buf[n - 2] = (c >> 16) & 0xFF; buf[n - 1] = c >> 24;
  KeyMap target;
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target));
}

void test_blob_rejects_unknown_version() {
  KeyMap m;
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  buf[4] = 4;
  uint32_t c = crc32(buf, n - 4);
  buf[n - 4] = c & 0xFF; buf[n - 3] = (c >> 8) & 0xFF; buf[n - 2] = (c >> 16) & 0xFF; buf[n - 1] = c >> 24;
  KeyMap target;
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target));
}

// --- icons (blob v3, 2026-11-08) ---

static void fixCrc(uint8_t *buf, size_t n) {
  uint32_t c = crc32(buf, n - 4);
  for (int k = 0; k < 4; k++) buf[n - 4 + k] = static_cast<uint8_t>(c >> (8 * k));
}

void test_icon_default_zero() {
  KeyMap m;
  for (uint8_t i = 0; i < kKeyCount; i++) TEST_ASSERT_EQUAL_UINT8(0, m.entry(i).icon);
}

void test_icon_set_known_case_insensitive_and_clear() {
  KeyMap m;
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setIcon(3, "camera"));
  TEST_ASSERT_EQUAL_UINT8(iconByName("camera"), m.entry(3).icon);
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setIcon(3, "EYE-OFF"));
  TEST_ASSERT_EQUAL_UINT8(iconByName("eye-off"), m.entry(3).icon);
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setIcon(3, ""));
  TEST_ASSERT_EQUAL_UINT8(0, m.entry(3).icon);
  m.setIcon(3, "bolt");
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setIcon(3, nullptr));
  TEST_ASSERT_EQUAL_UINT8(0, m.entry(3).icon);
}

void test_icon_rejections() {
  KeyMap m;
  m.setIcon(2, "gear");
  TEST_ASSERT_EQUAL(KeyMapError::BadIcon, m.setIcon(2, "nope"));
  TEST_ASSERT_EQUAL_UINT8(iconByName("gear"), m.entry(2).icon);  // unchanged
  TEST_ASSERT_EQUAL(KeyMapError::BadIndex, m.setIcon(12, "gear"));
}

void test_keymap_equality_includes_icon() {
  KeyMap a, b;
  b.setIcon(5, "gear");
  TEST_ASSERT_FALSE(a == b);
  a.setIcon(5, "gear");
  TEST_ASSERT_TRUE(a == b);
}

void test_blob_v3_roundtrip_keeps_icons() {
  KeyMap m;
  m.setLabel(0, "Shot");
  m.setIcon(0, "camera");
  m.setIcon(11, "home");
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  KeyMap back;
  TEST_ASSERT_TRUE(deserializeKeyMap(buf, n, back));
  TEST_ASSERT_TRUE(back == m);
  TEST_ASSERT_EQUAL_UINT8(iconByName("camera"), back.entry(0).icon);
  TEST_ASSERT_EQUAL_UINT8(iconByName("home"), back.entry(11).icon);
}

void test_blob_v2_still_loads_with_no_icons() {
  // Hand-built v2: magic, 2, 12 x [type, vlen, value, llen, label], CRC.
  uint8_t buf[kKeyMapBlobMax];
  const uint8_t head[] = {'S', 'D', 'K', 'M', 2};
  memcpy(buf, head, sizeof head);
  size_t p = sizeof head;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    char v[8];
    size_t n = static_cast<size_t>(snprintf(v, sizeof v, "f%u", 13u + i));
    buf[p++] = 0;
    buf[p++] = static_cast<uint8_t>(n);
    memcpy(buf + p, v, n);
    p += n;
    buf[p++] = 2;
    buf[p++] = 'o';
    buf[p++] = 'k';
  }
  p += 4;
  fixCrc(buf, p);
  KeyMap back;
  back.setIcon(0, "gear");
  TEST_ASSERT_TRUE(deserializeKeyMap(buf, p, back));
  TEST_ASSERT_EQUAL_STRING("ok", back.entry(0).label);
  TEST_ASSERT_EQUAL_STRING("f24", back.entry(11).value);
  for (uint8_t i = 0; i < kKeyCount; i++) TEST_ASSERT_EQUAL_UINT8(0, back.entry(i).icon);
}

void test_blob_v3_rejects_icon_id_above_count() {
  KeyMap m;
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  // key 0: [5]=type [6]=vlen [7..9]="f13" [10]=llen(0) [11]=icon
  TEST_ASSERT_EQUAL_UINT8(0, buf[10]);
  buf[11] = kIconCount;
  fixCrc(buf, n);
  KeyMap target;
  TEST_ASSERT_TRUE(deserializeKeyMap(buf, n, target));  // 20 = last valid id
  buf[11] = kIconCount + 1;
  fixCrc(buf, n);
  KeyMap target2;
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target2));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ks_single_letter);
  RUN_TEST(test_ks_cmd_shift_4);
  RUN_TEST(test_ks_all_modifiers_case_insensitive);
  RUN_TEST(test_ks_digits_and_named_keys);
  RUN_TEST(test_ks_slash_for_help_menu);
  RUN_TEST(test_ks_function_keys);
  RUN_TEST(test_ks_rejects_bad_input);
  RUN_TEST(test_ks_rejects_overlong_input);
  RUN_TEST(test_keymap_defaults_are_f13_to_f24);
  RUN_TEST(test_keymap_set_app_url_shortcut);
  RUN_TEST(test_keymap_set_keystroke_parses);
  RUN_TEST(test_keymap_rejections_leave_entry_unchanged);
  RUN_TEST(test_keymap_value_length_limit);
  RUN_TEST(test_keymap_set_from_strings);
  RUN_TEST(test_keymap_equality);
  RUN_TEST(test_keymap_mac_daily_preset_is_valid);
  RUN_TEST(test_label_default_empty);
  RUN_TEST(test_label_set_and_clear);
  RUN_TEST(test_label_rejections_leave_label_unchanged);
  RUN_TEST(test_label_survives_value_change);
  RUN_TEST(test_keymap_equality_includes_label);
  RUN_TEST(test_crc32_check_value);
  RUN_TEST(test_blob_roundtrip);
  RUN_TEST(test_blob_rejects_corruption_and_keeps_target);
  RUN_TEST(test_blob_rejects_valid_crc_but_invalid_entry);
  RUN_TEST(test_blob_small_buffer_returns_zero);
  RUN_TEST(test_blob_roundtrip_keeps_labels);
  RUN_TEST(test_blob_full_map_fits_max);
  RUN_TEST(test_blob_v1_still_loads_with_empty_labels);
  RUN_TEST(test_blob_rejects_valid_crc_but_unprintable_label);
  RUN_TEST(test_blob_rejects_unknown_version);
  RUN_TEST(test_icon_default_zero);
  RUN_TEST(test_icon_set_known_case_insensitive_and_clear);
  RUN_TEST(test_icon_rejections);
  RUN_TEST(test_keymap_equality_includes_icon);
  RUN_TEST(test_blob_v3_roundtrip_keeps_icons);
  RUN_TEST(test_blob_v2_still_loads_with_no_icons);
  RUN_TEST(test_blob_v3_rejects_icon_id_above_count);
  return UNITY_END();
}
