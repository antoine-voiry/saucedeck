#include <unity.h>
#include "keystroke.h"

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

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ks_single_letter);
  RUN_TEST(test_ks_cmd_shift_4);
  RUN_TEST(test_ks_all_modifiers_case_insensitive);
  RUN_TEST(test_ks_digits_and_named_keys);
  RUN_TEST(test_ks_function_keys);
  RUN_TEST(test_ks_rejects_bad_input);
  RUN_TEST(test_ks_rejects_overlong_input);
  return UNITY_END();
}
