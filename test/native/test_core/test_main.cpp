#include <unity.h>
#include "quad_decoder.h"
#include "button_diff.h"
#include "debounce.h"
#include "detent_counter.h"

void setUp() {}
void tearDown() {}

// --- QuadDecoder: EC11, 4 transitions per detent, idle state A=1 B=1 ---

// CW sequence from rest (AB): 11 -> 01 -> 00 -> 10 -> 11
static int feed(QuadDecoder &q, const uint8_t (*seq)[2], int n) {
  int sum = 0;
  for (int i = 0; i < n; i++) sum += q.step(seq[i][0], seq[i][1]);
  return sum;
}

static const uint8_t CW[4][2]  = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
static const uint8_t CCW[4][2] = {{1, 0}, {0, 0}, {0, 1}, {1, 1}};

void test_quad_one_detent_cw_returns_plus_one() {
  QuadDecoder q(1, 1);
  TEST_ASSERT_EQUAL_INT(1, feed(q, CW, 4));
}

void test_quad_one_detent_ccw_returns_minus_one() {
  QuadDecoder q(1, 1);
  TEST_ASSERT_EQUAL_INT(-1, feed(q, CCW, 4));
}

void test_quad_partial_detent_emits_nothing() {
  QuadDecoder q(1, 1);
  TEST_ASSERT_EQUAL_INT(0, feed(q, CW, 3));
}

void test_quad_bounce_back_and_forth_emits_nothing() {
  QuadDecoder q(1, 1);
  int sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += q.step(0, 1);
    sum += q.step(1, 1);
  }
  TEST_ASSERT_EQUAL_INT(0, sum);
}

void test_quad_invalid_double_transition_ignored() {
  QuadDecoder q(1, 1);
  TEST_ASSERT_EQUAL_INT(0, q.step(0, 0));  // both phases flip: illegal, no count
}

void test_quad_repeated_same_state_emits_nothing() {
  QuadDecoder q(1, 1);
  TEST_ASSERT_EQUAL_INT(0, q.step(1, 1));
}

void test_quad_two_detents_cw() {
  QuadDecoder q(1, 1);
  TEST_ASSERT_EQUAL_INT(2, feed(q, CW, 4) + feed(q, CW, 4));
}

// --- diffButtons: active-low, bit=0 means pressed ---

void test_diff_no_change() {
  ButtonEvents e = diffButtons(0xFFFF, 0xFFFF);
  TEST_ASSERT_EQUAL_HEX16(0, e.pressed);
  TEST_ASSERT_EQUAL_HEX16(0, e.released);
}

void test_diff_press_detected_on_falling_bit() {
  ButtonEvents e = diffButtons(0xFFFF, 0xFFFE);
  TEST_ASSERT_EQUAL_HEX16(0x0001, e.pressed);
  TEST_ASSERT_EQUAL_HEX16(0, e.released);
}

void test_diff_release_detected_on_rising_bit() {
  ButtonEvents e = diffButtons(0xFF7F, 0xFFFF);
  TEST_ASSERT_EQUAL_HEX16(0, e.pressed);
  TEST_ASSERT_EQUAL_HEX16(0x0080, e.released);
}

void test_diff_simultaneous_press_and_release() {
  ButtonEvents e = diffButtons(0xFFFE, 0xF7FF);  // btn1 up, btn12 down
  TEST_ASSERT_EQUAL_HEX16(0x0800, e.pressed);
  TEST_ASSERT_EQUAL_HEX16(0x0001, e.released);
}

// --- MaskDebouncer: bit must hold its new level for settleMs before it changes ---

void test_debounce_starts_released() {
  MaskDebouncer d;
  TEST_ASSERT_EQUAL_HEX16(0x0000, d.update(0x0000, 0));
}

void test_debounce_press_needs_settle_time() {
  MaskDebouncer d(25);
  d.update(0x0001, 100);
  TEST_ASSERT_EQUAL_HEX16(0x0000, d.update(0x0001, 124));
  TEST_ASSERT_EQUAL_HEX16(0x0001, d.update(0x0001, 125));
}

void test_debounce_bounce_shorter_than_settle_ignored() {
  MaskDebouncer d(25);
  d.update(0x0001, 0);
  d.update(0x0000, 10);
  d.update(0x0001, 20);
  TEST_ASSERT_EQUAL_HEX16(0x0000, d.update(0x0001, 30));  // restarted at 20
  TEST_ASSERT_EQUAL_HEX16(0x0001, d.update(0x0001, 45));
}

void test_debounce_release_needs_settle_time() {
  MaskDebouncer d(25);
  d.update(0x0001, 0);
  d.update(0x0001, 25);
  d.update(0x0000, 100);
  TEST_ASSERT_EQUAL_HEX16(0x0001, d.update(0x0000, 124));
  TEST_ASSERT_EQUAL_HEX16(0x0000, d.update(0x0000, 125));
}

void test_debounce_bits_independent() {
  MaskDebouncer d(25);
  d.update(0x0001, 0);
  d.update(0x0003, 10);  // bit 1 starts later
  TEST_ASSERT_EQUAL_HEX16(0x0001, d.update(0x0003, 25));
  TEST_ASSERT_EQUAL_HEX16(0x0003, d.update(0x0003, 35));
}

void test_debounce_single_glitch_all_pressed_ignored() {
  // Garbage read (e.g. 0x0000 raw -> all deck bits pressed) for one sample must not emit keys
  MaskDebouncer d(25);
  d.update(0x0000, 0);
  d.update(0x3FFF, 1);
  TEST_ASSERT_EQUAL_HEX16(0x0000, d.update(0x0000, 2));
  TEST_ASSERT_EQUAL_HEX16(0x0000, d.update(0x0000, 100));
}

void test_debounce_millis_wrap() {
  MaskDebouncer d(25);
  d.update(0x0001, 0xFFFFFFF0u);
  TEST_ASSERT_EQUAL_HEX16(0x0000, d.update(0x0001, 0x00000008u));  // 24 ms
  TEST_ASSERT_EQUAL_HEX16(0x0001, d.update(0x0001, 0x00000009u));  // 25 ms
}

void test_debounce_stable_matches_last_update() {
  MaskDebouncer d(25);
  d.update(0x0004, 0);
  d.update(0x0004, 30);
  TEST_ASSERT_EQUAL_HEX16(0x0004, d.stable());
}

// --- DetentCounter: PCNT x4 running count -> detents (EC11: 4 counts per detent) ---

void test_detent_four_counts_one_detent() {
  DetentCounter d(4);
  TEST_ASSERT_EQUAL_INT(0, d.update(3));
  TEST_ASSERT_EQUAL_INT(1, d.update(4));
}

void test_detent_negative_direction() {
  DetentCounter d(4);
  TEST_ASSERT_EQUAL_INT(-1, d.update(-4));
}

void test_detent_many_at_once() {
  // Fast spin between two loop passes: hardware counted everything
  DetentCounter d(4);
  TEST_ASSERT_EQUAL_INT(3, d.update(13));
  TEST_ASSERT_EQUAL_INT(0, d.update(14));
  TEST_ASSERT_EQUAL_INT(1, d.update(16));
}

void test_detent_bounce_back_and_forth_cancels() {
  DetentCounter d(4);
  TEST_ASSERT_EQUAL_INT(0, d.update(2));
  TEST_ASSERT_EQUAL_INT(0, d.update(-1));
  TEST_ASSERT_EQUAL_INT(0, d.update(1));
}

void test_detent_reverse_after_partial() {
  DetentCounter d(4);
  d.update(2);
  TEST_ASSERT_EQUAL_INT(-1, d.update(-4));  // -4 from the last detent boundary (0)
}

void test_detent_starts_from_nonzero_total() {
  DetentCounter d(4, 100);  // reference = count at init
  TEST_ASSERT_EQUAL_INT(0, d.update(103));
  TEST_ASSERT_EQUAL_INT(1, d.update(104));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_quad_one_detent_cw_returns_plus_one);
  RUN_TEST(test_quad_one_detent_ccw_returns_minus_one);
  RUN_TEST(test_quad_partial_detent_emits_nothing);
  RUN_TEST(test_quad_bounce_back_and_forth_emits_nothing);
  RUN_TEST(test_quad_invalid_double_transition_ignored);
  RUN_TEST(test_quad_repeated_same_state_emits_nothing);
  RUN_TEST(test_quad_two_detents_cw);
  RUN_TEST(test_diff_no_change);
  RUN_TEST(test_diff_press_detected_on_falling_bit);
  RUN_TEST(test_diff_release_detected_on_rising_bit);
  RUN_TEST(test_diff_simultaneous_press_and_release);
  RUN_TEST(test_debounce_starts_released);
  RUN_TEST(test_debounce_press_needs_settle_time);
  RUN_TEST(test_debounce_bounce_shorter_than_settle_ignored);
  RUN_TEST(test_debounce_release_needs_settle_time);
  RUN_TEST(test_debounce_bits_independent);
  RUN_TEST(test_debounce_single_glitch_all_pressed_ignored);
  RUN_TEST(test_debounce_millis_wrap);
  RUN_TEST(test_debounce_stable_matches_last_update);
  RUN_TEST(test_detent_four_counts_one_detent);
  RUN_TEST(test_detent_negative_direction);
  RUN_TEST(test_detent_many_at_once);
  RUN_TEST(test_detent_bounce_back_and_forth_cancels);
  RUN_TEST(test_detent_reverse_after_partial);
  RUN_TEST(test_detent_starts_from_nonzero_total);
  return UNITY_END();
}
