#include <unity.h>
#include "quad_decoder.h"
#include "button_diff.h"

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

// --- diffButtons: PCF8575 active-low, bit=0 means pressed ---

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
  return UNITY_END();
}
