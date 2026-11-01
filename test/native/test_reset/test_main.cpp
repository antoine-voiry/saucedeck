#include <unity.h>
#include <string.h>
#include "reset_log.h"

void setUp() {}
void tearDown() {}

// Codes are esp_reset_reason_t values (ESP-IDF 5.x esp_system.h): POWERON 1, SW 3, PANIC 4,
// INT_WDT 5, TASK_WDT 6, BROWNOUT 9, USB 11, CPU_LOCKUP 15.

void test_names_match_idf_codes() {
  TEST_ASSERT_EQUAL_STRING("power on", resetName(1));
  TEST_ASSERT_EQUAL_STRING("software", resetName(3));
  TEST_ASSERT_EQUAL_STRING("panic", resetName(4));
  TEST_ASSERT_EQUAL_STRING("int wdt", resetName(5));
  TEST_ASSERT_EQUAL_STRING("task wdt", resetName(6));
  TEST_ASSERT_EQUAL_STRING("brownout", resetName(9));
  TEST_ASSERT_EQUAL_STRING("usb", resetName(11));
  TEST_ASSERT_EQUAL_STRING("cpu lockup", resetName(15));
  TEST_ASSERT_EQUAL_STRING("unknown", resetName(0));
  TEST_ASSERT_EQUAL_STRING("unknown", resetName(200));
}

void test_power_on_is_not_notable() {
  TEST_ASSERT_FALSE(resetNotable(1));
  TEST_ASSERT_TRUE(resetNotable(4));
  TEST_ASSERT_TRUE(resetNotable(9));
  TEST_ASSERT_TRUE(resetNotable(0));
}

void test_format_with_uptime() {
  char s[40];
  formatReset({4, 65536}, s, sizeof s);
  TEST_ASSERT_EQUAL_STRING("Reset: panic, up 18.2 h", s);
  formatReset({6, 3600}, s, sizeof s);
  TEST_ASSERT_EQUAL_STRING("Reset: task wdt, up 1.0 h", s);
  formatReset({9, 3599}, s, sizeof s);
  TEST_ASSERT_EQUAL_STRING("Reset: brownout, up 59 min", s);
  formatReset({3, 59}, s, sizeof s);
  TEST_ASSERT_EQUAL_STRING("Reset: software, up 59 s", s);
}

void test_format_unknown_uptime() {
  char s[40];
  formatReset({9, 0}, s, sizeof s);
  TEST_ASSERT_EQUAL_STRING("Reset: brownout", s);
}

void test_log_keeps_newest_first_and_caps() {
  ResetLog log;
  TEST_ASSERT_EQUAL_UINT8(0, log.count());
  for (uint8_t i = 0; i < ResetLog::kSize + 3; i++) log.push({i, i * 10u});
  TEST_ASSERT_EQUAL_UINT8(ResetLog::kSize, log.count());
  TEST_ASSERT_EQUAL_UINT8(ResetLog::kSize + 2, log.at(0).reason);
  TEST_ASSERT_EQUAL_UINT32((ResetLog::kSize + 2) * 10u, log.at(0).upS);
  TEST_ASSERT_EQUAL_UINT8(3, log.at(ResetLog::kSize - 1).reason);
}

void test_log_round_trips_bytes() {
  ResetLog a;
  a.push({1, 0});
  a.push({4, 65536});
  a.push({9, 123456789});
  uint8_t buf[ResetLog::kBytes];
  TEST_ASSERT_EQUAL_size_t(ResetLog::kBytes, a.serialize(buf, sizeof buf));
  ResetLog b;
  TEST_ASSERT_TRUE(b.load(buf, sizeof buf));
  TEST_ASSERT_EQUAL_UINT8(3, b.count());
  TEST_ASSERT_EQUAL_UINT8(9, b.at(0).reason);
  TEST_ASSERT_EQUAL_UINT32(123456789u, b.at(0).upS);
  TEST_ASSERT_EQUAL_UINT8(4, b.at(1).reason);
  TEST_ASSERT_EQUAL_UINT32(65536u, b.at(1).upS);
  TEST_ASSERT_EQUAL_UINT8(1, b.at(2).reason);
}

void test_load_rejects_bad_blob() {
  ResetLog a;
  a.push({4, 1});
  uint8_t buf[ResetLog::kBytes];
  a.serialize(buf, sizeof buf);
  ResetLog b;
  TEST_ASSERT_FALSE(b.load(buf, sizeof buf - 1));  // short
  buf[0] ^= 0xFF;                                   // wrong version
  TEST_ASSERT_FALSE(b.load(buf, sizeof buf));
  buf[0] ^= 0xFF;
  buf[1] = ResetLog::kSize + 1;  // count out of range
  TEST_ASSERT_FALSE(b.load(buf, sizeof buf));
  TEST_ASSERT_EQUAL_UINT8(0, b.count());
  TEST_ASSERT_EQUAL_size_t(0, a.serialize(buf, sizeof buf - 1));  // buffer too small
}

void test_summary_lists_newest_first() {
  ResetLog log;
  log.push({1, 0});
  log.push({4, 65536});
  char s[200];
  log.summary(s, sizeof s);
  TEST_ASSERT_EQUAL_STRING("panic, up 18.2 h; power on", s);
  ResetLog empty;
  empty.summary(s, sizeof s);
  TEST_ASSERT_EQUAL_STRING("none", s);
  log.summary(s, 8);  // truncates, stays terminated
  TEST_ASSERT_EQUAL_size_t(7, strlen(s));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_names_match_idf_codes);
  RUN_TEST(test_power_on_is_not_notable);
  RUN_TEST(test_format_with_uptime);
  RUN_TEST(test_format_unknown_uptime);
  RUN_TEST(test_log_keeps_newest_first_and_caps);
  RUN_TEST(test_log_round_trips_bytes);
  RUN_TEST(test_load_rejects_bad_blob);
  RUN_TEST(test_summary_lists_newest_first);
  return UNITY_END();
}
