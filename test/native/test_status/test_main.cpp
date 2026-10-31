#include <unity.h>
#include <string.h>
#include "deck_clock.h"
#include "status_board.h"

void setUp() {}
void tearDown() {}

// --- DeckClock ---

static constexpr uint32_t kDay = 20000u * 86400u;  // some UTC midnight

void test_clock_unknown_until_time() {
  DeckClock c;
  TEST_ASSERT_FALSE(c.known());
  TEST_ASSERT_EQUAL_INT16(-1, c.minuteOfDay(12345));
}

void test_clock_utc_and_offsets() {
  DeckClock c;
  c.onTime(kDay + 12 * 3600 + 34 * 60, 0, 1000);
  TEST_ASSERT_TRUE(c.known());
  TEST_ASSERT_EQUAL_INT16(12 * 60 + 34, c.minuteOfDay(1000));
  c.onTime(kDay + 12 * 3600 + 34 * 60, 60, 1000);
  TEST_ASSERT_EQUAL_INT16(13 * 60 + 34, c.minuteOfDay(1000));
  c.onTime(kDay + 12 * 3600 + 34 * 60, -720, 1000);
  TEST_ASSERT_EQUAL_INT16(34, c.minuteOfDay(1000));
}

void test_clock_runs_on_millis() {
  DeckClock c;
  c.onTime(kDay + 12 * 3600 + 34 * 60, 0, 1000);
  TEST_ASSERT_EQUAL_INT16(12 * 60 + 34, c.minuteOfDay(1000 + 59999 - 0));  // 12:34:59
  TEST_ASSERT_EQUAL_INT16(13 * 60, c.minuteOfDay(1000 + 26u * 60u * 1000u));
}

void test_clock_midnight_wrap_and_negative_local() {
  DeckClock c;
  c.onTime(kDay + 23 * 3600 + 59 * 60, 0, 0);
  TEST_ASSERT_EQUAL_INT16(0, c.minuteOfDay(60000));
  c.onTime(0, -60, 0);  // 1970-01-01 00:00 UTC = 23:00 the day before
  TEST_ASSERT_EQUAL_INT16(23 * 60, c.minuteOfDay(0));
}

void test_clock_millis_wrap() {
  DeckClock c;
  c.onTime(kDay, 0, 0xFFFFFF00u);
  TEST_ASSERT_EQUAL_INT16(0, c.minuteOfDay(0x00001000u));  // ~4.3 s later
}

void test_format_hhmm() {
  char b[6];
  formatHhmm(12 * 60 + 34, b);
  TEST_ASSERT_EQUAL_STRING("12:34", b);
  formatHhmm(5, b);
  TEST_ASSERT_EQUAL_STRING("00:05", b);
  formatHhmm(1439, b);
  TEST_ASSERT_EQUAL_STRING("23:59", b);
  formatHhmm(-1, b);
  TEST_ASSERT_EQUAL_STRING("--:--", b);
  formatHhmm(1440, b);
  TEST_ASSERT_EQUAL_STRING("--:--", b);
}

// --- StatusBoard ---

static CardMsg card(uint8_t slot, const char *t, uint16_t ttl = 0, uint8_t icon = 1) {
  CardMsg c{};
  c.slot = slot;
  c.icon = icon;
  c.ttlS = ttl;
  strncpy(c.text, t, kCardTextMax);
  return c;
}

void test_board_empty() {
  StatusBoard b;
  b.tick(0);
  TEST_ASSERT_NULL(b.top());
  TEST_ASSERT_NULL(b.current());
  TEST_ASSERT_EQUAL_UINT8(0, b.count());
  TEST_ASSERT_EQUAL_UINT8(0, b.index());
}

void test_board_slot0_is_top_not_rotation() {
  StatusBoard b;
  b.onCard(card(0, "Standup in 12m", 0, 2), 0);
  b.tick(0);
  TEST_ASSERT_NOT_NULL(b.top());
  TEST_ASSERT_EQUAL_STRING("Standup in 12m", b.top()->text);
  TEST_ASSERT_EQUAL_UINT8(2, b.top()->icon);
  TEST_ASSERT_NULL(b.current());
  TEST_ASSERT_EQUAL_UINT8(0, b.count());
}

void test_board_single_card_shows() {
  StatusBoard b;
  b.onCard(card(2, "CPU 12%"), 0);
  b.tick(0);
  TEST_ASSERT_EQUAL_STRING("CPU 12%", b.current()->text);
  TEST_ASSERT_EQUAL_UINT8(1, b.index());
  TEST_ASSERT_EQUAL_UINT8(1, b.count());
}

void test_board_rotates_every_5s_and_wraps() {
  StatusBoard b;
  b.onCard(card(1, "A"), 0);
  b.onCard(card(3, "B"), 0);
  b.tick(0);
  TEST_ASSERT_EQUAL_STRING("A", b.current()->text);
  TEST_ASSERT_EQUAL_UINT8(1, b.index());
  TEST_ASSERT_EQUAL_UINT8(2, b.count());
  b.tick(4999);
  TEST_ASSERT_EQUAL_STRING("A", b.current()->text);
  b.tick(5000);
  TEST_ASSERT_EQUAL_STRING("B", b.current()->text);
  TEST_ASSERT_EQUAL_UINT8(2, b.index());
  b.tick(10000);
  TEST_ASSERT_EQUAL_STRING("A", b.current()->text);
}

void test_board_ttl_expires() {
  StatusBoard b;
  b.onCard(card(1, "Song", 2), 0);
  b.tick(1999);
  TEST_ASSERT_NOT_NULL(b.current());
  b.tick(2000);
  TEST_ASSERT_NULL(b.current());
  TEST_ASSERT_EQUAL_UINT8(0, b.count());
}

void test_board_resend_refreshes_ttl() {
  StatusBoard b;
  b.onCard(card(1, "Song", 2), 0);
  b.onCard(card(1, "Song", 2), 1500);
  b.tick(3000);
  TEST_ASSERT_NOT_NULL(b.current());
  b.tick(3500);
  TEST_ASSERT_NULL(b.current());
}

void test_board_empty_text_clears_slot() {
  StatusBoard b;
  b.onCard(card(4, "Mail 3"), 0);
  b.tick(0);
  b.onCard(card(4, ""), 100);
  b.tick(100);
  TEST_ASSERT_NULL(b.current());
}

void test_board_current_cleared_moves_on_without_waiting() {
  StatusBoard b;
  b.onCard(card(1, "A"), 0);
  b.onCard(card(2, "B"), 0);
  b.tick(0);
  b.onCard(card(1, ""), 100);
  b.tick(100);
  TEST_ASSERT_EQUAL_STRING("B", b.current()->text);
  TEST_ASSERT_EQUAL_UINT8(1, b.index());
  TEST_ASSERT_EQUAL_UINT8(1, b.count());
}

void test_board_gen_changes_only_when_shown_content_changes() {
  StatusBoard b;
  uint8_t g0 = b.gen();
  b.onCard(card(1, "A"), 0);
  b.tick(0);
  uint8_t g1 = b.gen();
  TEST_ASSERT_NOT_EQUAL(g0, g1);
  b.onCard(card(1, "A"), 1000);  // same content resent
  b.tick(1000);
  TEST_ASSERT_EQUAL_UINT8(g1, b.gen());
  b.onCard(card(1, "A2"), 2000);
  TEST_ASSERT_NOT_EQUAL(g1, b.gen());
}

void test_board_clear_is_idempotent() {
  StatusBoard b;
  b.onCard(card(0, "T"), 0);
  b.onCard(card(1, "A"), 0);
  b.tick(0);
  uint8_t g = b.gen();
  b.clear();
  TEST_ASSERT_NULL(b.top());
  TEST_ASSERT_NULL(b.current());
  TEST_ASSERT_NOT_EQUAL(g, b.gen());
  g = b.gen();
  b.clear();
  b.tick(10);
  TEST_ASSERT_EQUAL_UINT8(g, b.gen());
}

void test_board_ignores_bad_slot() {
  StatusBoard b;
  uint8_t g = b.gen();
  b.onCard(card(kCardSlots, "X"), 0);
  b.tick(0);
  TEST_ASSERT_EQUAL_UINT8(g, b.gen());
  TEST_ASSERT_NULL(b.current());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_clock_unknown_until_time);
  RUN_TEST(test_clock_utc_and_offsets);
  RUN_TEST(test_clock_runs_on_millis);
  RUN_TEST(test_clock_midnight_wrap_and_negative_local);
  RUN_TEST(test_clock_millis_wrap);
  RUN_TEST(test_format_hhmm);
  RUN_TEST(test_board_empty);
  RUN_TEST(test_board_slot0_is_top_not_rotation);
  RUN_TEST(test_board_single_card_shows);
  RUN_TEST(test_board_rotates_every_5s_and_wraps);
  RUN_TEST(test_board_ttl_expires);
  RUN_TEST(test_board_resend_refreshes_ttl);
  RUN_TEST(test_board_empty_text_clears_slot);
  RUN_TEST(test_board_current_cleared_moves_on_without_waiting);
  RUN_TEST(test_board_gen_changes_only_when_shown_content_changes);
  RUN_TEST(test_board_clear_is_idempotent);
  RUN_TEST(test_board_ignores_bad_slot);
  return UNITY_END();
}
