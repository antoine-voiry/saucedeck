#include <unity.h>
#include <string.h>
#include "keystroke.h"
#include "keymap.h"

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

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ks_single_letter);
  RUN_TEST(test_ks_cmd_shift_4);
  RUN_TEST(test_ks_all_modifiers_case_insensitive);
  RUN_TEST(test_ks_digits_and_named_keys);
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
  return UNITY_END();
}
