#include <unity.h>
#include <string.h>
#include "keymap.h"
#include "legend.h"
#include "tile_anim.h"

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

// --- TileAnim: held = full fill, release fades 3/4, 2/4, 1/4, 0 at 60 ms steps ---

void test_anim_idle_nothing_dirty() {
  TileAnim a;
  TEST_ASSERT_EQUAL_HEX16(0, a.tick(0));
  TEST_ASSERT_EQUAL_UINT8(0, a.level(0));
}

void test_anim_press_fills_tile_once() {
  TileAnim a;
  a.onKeys(1u << 2, 0, 100);
  TEST_ASSERT_EQUAL_HEX16(1u << 2, a.tick(100));
  TEST_ASSERT_EQUAL_UINT8(TileAnim::kFull, a.level(2));
  TEST_ASSERT_FALSE(a.failed(2));
  a.onKeys(1u << 2, 0, 110);
  TEST_ASSERT_EQUAL_HEX16(0, a.tick(110));  // still held: no repaint
}

void test_anim_release_fades_in_steps() {
  TileAnim a;
  a.onKeys(1u << 0, 0, 0);
  a.tick(0);
  a.onKeys(0, 0, 200);
  TEST_ASSERT_EQUAL_HEX16(1u << 0, a.tick(200));
  TEST_ASSERT_EQUAL_UINT8(3, a.level(0));
  TEST_ASSERT_EQUAL_HEX16(0, a.tick(259));
  TEST_ASSERT_EQUAL_HEX16(1u << 0, a.tick(260));
  TEST_ASSERT_EQUAL_UINT8(2, a.level(0));
  a.tick(320);
  TEST_ASSERT_EQUAL_UINT8(1, a.level(0));
  TEST_ASSERT_EQUAL_HEX16(1u << 0, a.tick(380));
  TEST_ASSERT_EQUAL_UINT8(0, a.level(0));
  TEST_ASSERT_EQUAL_HEX16(0, a.tick(1000));
}

void test_anim_steps_between_frames_merge() {
  TileAnim a;
  a.onKeys(1u << 4, 0, 0);
  a.tick(0);
  a.onKeys(0, 0, 10);
  TEST_ASSERT_EQUAL_HEX16(1u << 4, a.tick(500));  // 4 -> 0 in one repaint
  TEST_ASSERT_EQUAL_UINT8(0, a.level(4));
}

void test_anim_failed_press_until_next_good_press() {
  TileAnim a;
  a.onKeys(1u << 5, 1u << 5, 0);
  a.tick(0);
  TEST_ASSERT_TRUE(a.failed(5));
  a.onKeys(0, 0, 100);
  a.tick(400);
  a.onKeys(1u << 5, 0, 500);
  a.tick(500);
  TEST_ASSERT_FALSE(a.failed(5));
}

void test_anim_press_during_fade_refills() {
  TileAnim a;
  a.onKeys(1u << 1, 0, 0);
  a.tick(0);
  a.onKeys(0, 0, 100);
  a.tick(160);
  TEST_ASSERT_EQUAL_UINT8(2, a.level(1));
  a.onKeys(1u << 1, 0, 170);
  TEST_ASSERT_EQUAL_HEX16(1u << 1, a.tick(170));
  TEST_ASSERT_EQUAL_UINT8(TileAnim::kFull, a.level(1));
}

void test_anim_ignores_bits_above_12() {
  TileAnim a;
  a.onKeys(0xF000, 0xF000, 0);
  TEST_ASSERT_EQUAL_HEX16(0, a.tick(0));
}

// --- blend565: RGB565 mix, num/den of b ---

void test_blend_endpoints_and_half() {
  TEST_ASSERT_EQUAL_HEX16(0x0000, blend565(0x0000, 0xFFFF, 0, 4));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, blend565(0x0000, 0xFFFF, 4, 4));
  TEST_ASSERT_EQUAL_HEX16(0x7800, blend565(0x0000, 0xF800, 2, 4));  // red 31 * 2/4 = 15
  TEST_ASSERT_EQUAL_HEX16(0x03E0, blend565(0x0000, 0x07E0, 2, 4));  // green 63 * 2/4 = 31
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
  RUN_TEST(test_anim_idle_nothing_dirty);
  RUN_TEST(test_anim_press_fills_tile_once);
  RUN_TEST(test_anim_release_fades_in_steps);
  RUN_TEST(test_anim_steps_between_frames_merge);
  RUN_TEST(test_anim_failed_press_until_next_good_press);
  RUN_TEST(test_anim_press_during_fade_refills);
  RUN_TEST(test_anim_ignores_bits_above_12);
  RUN_TEST(test_blend_endpoints_and_half);
  return UNITY_END();
}
