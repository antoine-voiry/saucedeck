#include <unity.h>
#include "column_scanner.h"

void setUp() {}
void tearDown() {}

// Feed one full scan; `low` = active-high mask of columns whose sense reads LOW.
static bool fullScan(ColumnScanner &s, uint16_t low) {
  bool done = false;
  for (int guard = 0; guard < 16 && !done; guard++) done = s.feed(low & (1u << s.column()));
  return done;
}

void test_first_drive_word_pulls_lowest_column_low() {
  ColumnScanner s(0x0FFF);
  TEST_ASSERT_EQUAL_UINT8(0, s.column());
  TEST_ASSERT_EQUAL_HEX16(0xFFFE, s.driveWord());
}

void test_columns_outside_mask_are_skipped() {
  ColumnScanner s(0x0005);  // columns 0 and 2
  s.feed(false);
  TEST_ASSERT_EQUAL_UINT8(2, s.column());
  TEST_ASSERT_EQUAL_HEX16(0xFFFB, s.driveWord());
}

void test_feed_returns_true_only_on_last_column() {
  ColumnScanner s(0x0007);
  TEST_ASSERT_FALSE(s.feed(false));
  TEST_ASSERT_FALSE(s.feed(false));
  TEST_ASSERT_TRUE(s.feed(false));
  TEST_ASSERT_EQUAL_UINT8(0, s.column());  // wraps
}

void test_idle_scan_reports_nothing_pressed() {
  ColumnScanner s(0x0FFF);
  TEST_ASSERT_TRUE(fullScan(s, 0));
  TEST_ASSERT_EQUAL_HEX16(0, s.pressed());
}

void test_single_press_reported_after_full_scan() {
  ColumnScanner s(0x0FFF);
  fullScan(s, 1u << 3);
  TEST_ASSERT_EQUAL_HEX16(0x0008, s.pressed());
}

void test_multiple_presses_reported() {
  ColumnScanner s(0x0FFF);
  fullScan(s, 0x0801);
  TEST_ASSERT_EQUAL_HEX16(0x0801, s.pressed());
}

void test_pressed_not_updated_mid_scan() {
  ColumnScanner s(0x0FFF);
  fullScan(s, 0x0001);
  s.feed(false);  // column 0 now released, scan incomplete
  TEST_ASSERT_EQUAL_HEX16(0x0001, s.pressed());
}

void test_release_clears_on_next_scan() {
  ColumnScanner s(0x0FFF);
  fullScan(s, 0x0010);
  fullScan(s, 0);
  TEST_ASSERT_EQUAL_HEX16(0, s.pressed());
}

void test_sense_on_masked_out_column_ignored() {
  ColumnScanner s(0x0003);
  fullScan(s, 0xFFFF);
  TEST_ASSERT_EQUAL_HEX16(0x0003, s.pressed());
}

void test_empty_mask_is_inert() {
  ColumnScanner s(0);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, s.driveWord());
  TEST_ASSERT_TRUE(s.feed(true));
  TEST_ASSERT_EQUAL_HEX16(0, s.pressed());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_first_drive_word_pulls_lowest_column_low);
  RUN_TEST(test_columns_outside_mask_are_skipped);
  RUN_TEST(test_feed_returns_true_only_on_last_column);
  RUN_TEST(test_idle_scan_reports_nothing_pressed);
  RUN_TEST(test_single_press_reported_after_full_scan);
  RUN_TEST(test_multiple_presses_reported);
  RUN_TEST(test_pressed_not_updated_mid_scan);
  RUN_TEST(test_release_clears_on_next_scan);
  RUN_TEST(test_sense_on_masked_out_column_ignored);
  RUN_TEST(test_empty_mask_is_inert);
  return UNITY_END();
}
