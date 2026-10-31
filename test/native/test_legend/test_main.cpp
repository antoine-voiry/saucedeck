#include <unity.h>
#include <string.h>
#include "keymap.h"
#include "legend.h"

void setUp() {}
void tearDown() {}

// --- displayLabel: label if set, else readable text derived from the value ---

static void shown(const KeyMap &m, uint8_t i, const char *expected, size_t cap = kLegendLineMax + 1) {
  char out[kLegendLineMax + 1];
  displayLabel(m.entry(i), out, cap);
  TEST_ASSERT_EQUAL_STRING(expected, out);
}

void test_label_default_fkey_uppercased() {
  KeyMap m;
  shown(m, 0, "F13");
  shown(m, 11, "F24");
}

void test_label_keystroke_chord_as_written() {
  KeyMap m;
  m.set(8, KeyType::Keystroke, "cmd+h");
  shown(m, 8, "cmd+h");
}

void test_label_user_label_wins() {
  KeyMap m;
  m.setLabel(1, "Chrome");
  shown(m, 1, "Chrome");
}

void test_label_url_host_without_www_and_path() {
  KeyMap m;
  m.set(7, KeyType::Url, "https://www.youtube.com/watch?v=x");
  shown(m, 7, "youtube.com");
  m.set(2, KeyType::Url, "http://mail.google.com");
  shown(m, 2, "mail.google.com");
}

void test_label_app_and_shortcut_show_value() {
  KeyMap m;
  m.set(0, KeyType::App, "Calculator");
  m.set(3, KeyType::Shortcut, "Focus on");
  shown(m, 0, "Calculator");
  shown(m, 3, "Focus on");
}

void test_label_truncates_to_cap() {
  KeyMap m;
  m.set(0, KeyType::App, "Calculator");
  shown(m, 0, "Calc", 5);
}

// --- wrapLabel: up to 2 lines that fit maxPx, split at a space when possible ---

static uint8_t w10(char) { return 10; }
static uint8_t narrowI(char c) { return c == 'i' ? 4 : 10; }

static LabelLines wrap(const char *t, uint16_t maxPx = 80, GlyphWidthFn w = w10) {
  LabelLines l;
  wrapLabel(t, maxPx, w, l);
  return l;
}

void test_wrap_fits_one_line() {
  LabelLines l = wrap("Chrome");
  TEST_ASSERT_EQUAL_STRING("Chrome", l.line1);
  TEST_ASSERT_EQUAL_STRING("", l.line2);
}

void test_wrap_exact_width_is_one_line() {
  LabelLines l = wrap("Telegram");  // 8 x 10 = 80
  TEST_ASSERT_EQUAL_STRING("Telegram", l.line1);
  TEST_ASSERT_EQUAL_STRING("", l.line2);
}

void test_wrap_splits_at_last_fitting_space() {
  LabelLines l = wrap("System Settings");
  TEST_ASSERT_EQUAL_STRING("System", l.line1);
  TEST_ASSERT_EQUAL_STRING("Settings", l.line2);
}

void test_wrap_space_right_after_fit_is_used() {
  LabelLines l = wrap("Gmail in Chrome");
  TEST_ASSERT_EQUAL_STRING("Gmail in", l.line1);
  TEST_ASSERT_EQUAL_STRING("Chrome", l.line2);
}

void test_wrap_hard_cut_without_space() {
  LabelLines l = wrap("Screenshot");
  TEST_ASSERT_EQUAL_STRING("Screensh", l.line1);
  TEST_ASSERT_EQUAL_STRING("ot", l.line2);
}

void test_wrap_second_line_truncated_with_dots() {
  LabelLines l = wrap("Autodesk Fusion 360 Pro");
  TEST_ASSERT_EQUAL_STRING("Autodesk", l.line1);
  TEST_ASSERT_EQUAL_STRING("Fusio...", l.line2);  // 5 x 10 + "..." 30 = 80
}

void test_wrap_uses_glyph_widths() {
  LabelLines l = wrap("iiiiiiiiiiii", 80, narrowI);  // 12 x 4 = 48
  TEST_ASSERT_EQUAL_STRING("iiiiiiiiiiii", l.line1);
  TEST_ASSERT_EQUAL_STRING("", l.line2);
}

void test_wrap_empty() {
  LabelLines l = wrap("");
  TEST_ASSERT_EQUAL_STRING("", l.line1);
  TEST_ASSERT_EQUAL_STRING("", l.line2);
}

void test_wrap_longest_value_stays_in_buffers() {
  char t[kLegendLineMax + 1];
  memset(t, 'x', kLegendLineMax);
  t[kLegendLineMax] = 0;
  LabelLines l = wrap(t, 1000);  // 56 x 10 = 560: one line
  TEST_ASSERT_EQUAL_size_t(kLegendLineMax, strlen(l.line1));
  l = wrap(t, 30);
  TEST_ASSERT_EQUAL_STRING("xxx", l.line1);
  TEST_ASSERT_EQUAL_STRING("...", l.line2);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_label_default_fkey_uppercased);
  RUN_TEST(test_label_keystroke_chord_as_written);
  RUN_TEST(test_label_user_label_wins);
  RUN_TEST(test_label_url_host_without_www_and_path);
  RUN_TEST(test_label_app_and_shortcut_show_value);
  RUN_TEST(test_label_truncates_to_cap);
  RUN_TEST(test_wrap_fits_one_line);
  RUN_TEST(test_wrap_exact_width_is_one_line);
  RUN_TEST(test_wrap_splits_at_last_fitting_space);
  RUN_TEST(test_wrap_space_right_after_fit_is_used);
  RUN_TEST(test_wrap_hard_cut_without_space);
  RUN_TEST(test_wrap_second_line_truncated_with_dots);
  RUN_TEST(test_wrap_uses_glyph_widths);
  RUN_TEST(test_wrap_empty);
  RUN_TEST(test_wrap_longest_value_stays_in_buffers);
  return UNITY_END();
}
