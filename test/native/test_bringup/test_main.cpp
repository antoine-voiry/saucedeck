#include <string.h>
#include <unity.h>
#include "mcp23017.h"
#include "bringup.h"

void setUp() {}
void tearDown() {}

static const BringupStep kSteps[] = {
    {StepKind::Button, 0x0001, 0, 0, "Press button 1"},
    {StepKind::Button, 0x1000, 0, 0, "Press encoder 1 knob"},
    {StepKind::EncoderTurn, 0, 0, +1, "Turn encoder 1 CW"},
    {StepKind::EncoderTurn, 0, 1, -1, "Turn encoder 2 CCW"},
};
static const size_t kCount = sizeof(kSteps) / sizeof(kSteps[0]);

static ButtonEvents press(uint16_t bits) { return {bits, 0}; }

void test_starts_on_first_step() {
  BringupSequencer s(kSteps, kCount);
  TEST_ASSERT_EQUAL_PTR(&kSteps[0], s.current());
  TEST_ASSERT_FALSE(s.done());
}

void test_correct_button_passes_and_advances() {
  BringupSequencer s(kSteps, kCount);
  StepResult r = s.onButtons(press(0x0001));
  TEST_ASSERT_EQUAL(Verdict::Pass, r.verdict);
  TEST_ASSERT_EQUAL_PTR(&kSteps[1], s.current());
}

void test_release_only_is_pending() {
  BringupSequencer s(kSteps, kCount);
  StepResult r = s.onButtons({0, 0x0001});
  TEST_ASSERT_EQUAL(Verdict::Pending, r.verdict);
  TEST_ASSERT_EQUAL_PTR(&kSteps[0], s.current());
}

void test_wrong_button_fails_with_bits_and_advances() {
  BringupSequencer s(kSteps, kCount);
  StepResult r = s.onButtons(press(0x0020));
  TEST_ASSERT_EQUAL(Verdict::Fail, r.verdict);
  TEST_ASSERT_EQUAL_HEX16(0x0020, r.gotBits);
  TEST_ASSERT_EQUAL_PTR(&kSteps[1], s.current());
}

void test_expected_plus_extra_bit_fails() {
  BringupSequencer s(kSteps, kCount);
  StepResult r = s.onButtons(press(0x0003));
  TEST_ASSERT_EQUAL(Verdict::Fail, r.verdict);
}

void test_multi_bit_mask_passes_on_any_bit_in_mask() {
  BringupSequencer s(kSteps, kCount);
  s.onButtons(press(0x0001));
  TEST_ASSERT_EQUAL(Verdict::Pass, s.onButtons(press(0x1000)).verdict);
}

void test_encoder_ignored_during_button_step() {
  BringupSequencer s(kSteps, kCount);
  TEST_ASSERT_EQUAL(Verdict::Pending, s.onEncoder(0, 1).verdict);
  TEST_ASSERT_EQUAL_PTR(&kSteps[0], s.current());
}

static void advanceToEncoderStep(BringupSequencer &s) {
  s.onButtons(press(0x0001));
  s.onButtons(press(0x1000));
}

void test_encoder_correct_direction_passes() {
  BringupSequencer s(kSteps, kCount);
  advanceToEncoderStep(s);
  TEST_ASSERT_EQUAL(Verdict::Pass, s.onEncoder(0, +1).verdict);
}

void test_encoder_reversed_direction_fails() {
  BringupSequencer s(kSteps, kCount);
  advanceToEncoderStep(s);
  StepResult r = s.onEncoder(0, -1);
  TEST_ASSERT_EQUAL(Verdict::Fail, r.verdict);
  TEST_ASSERT_EQUAL_INT8(-1, r.gotDir);
}

void test_wrong_encoder_fails() {
  BringupSequencer s(kSteps, kCount);
  advanceToEncoderStep(s);
  StepResult r = s.onEncoder(1, +1);
  TEST_ASSERT_EQUAL(Verdict::Fail, r.verdict);
  TEST_ASSERT_EQUAL_UINT8(1, r.gotEnc);
}

void test_buttons_ignored_during_encoder_step() {
  BringupSequencer s(kSteps, kCount);
  advanceToEncoderStep(s);
  TEST_ASSERT_EQUAL(Verdict::Pending, s.onButtons(press(0x0001)).verdict);
}

void test_skip_advances_and_counts() {
  BringupSequencer s(kSteps, kCount);
  s.skip();
  TEST_ASSERT_EQUAL_PTR(&kSteps[1], s.current());
  TEST_ASSERT_EQUAL_UINT(1, s.skipped());
}

void test_done_after_last_step_and_tallies() {
  BringupSequencer s(kSteps, kCount);
  s.onButtons(press(0x0001));   // pass
  s.onButtons(press(0x0004));   // fail
  s.onEncoder(0, +1);           // pass
  s.skip();                     // skip
  TEST_ASSERT_TRUE(s.done());
  TEST_ASSERT_NULL(s.current());
  TEST_ASSERT_EQUAL_UINT(2, s.passed());
  TEST_ASSERT_EQUAL_UINT(1, s.failed());
  TEST_ASSERT_EQUAL_UINT(1, s.skipped());
}

void test_events_after_done_are_pending() {
  BringupSequencer s(kSteps, 1);
  s.onButtons(press(0x0001));
  TEST_ASSERT_EQUAL(Verdict::Pending, s.onButtons(press(0x0001)).verdict);
  s.skip();
  TEST_ASSERT_EQUAL_UINT(0, s.skipped());
}

// --- describeResult: human diagnosis for bench ---

static char buf[128];

void test_describe_pass() {
  describeResult(kSteps[0], {Verdict::Pass, 0, 0, 0}, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("PASS", buf);
}

void test_describe_wrong_button_names_pin() {
  describeResult(kSteps[0], {Verdict::Fail, 0x0020, 0, 0}, buf, sizeof buf);
  TEST_ASSERT_NOT_NULL(strstr(buf, deckPinName(5)));  // pin follows the key map
  TEST_ASSERT_NOT_NULL(strstr(buf, "crossed"));
}

void test_describe_extra_bits_suggests_short() {
  describeResult(kSteps[0], {Verdict::Fail, 0x0003, 0, 0}, buf, sizeof buf);
  TEST_ASSERT_NOT_NULL(strstr(buf, "short"));
}

void test_describe_reversed_encoder_suggests_swap() {
  describeResult(kSteps[2], {Verdict::Fail, 0, 0, -1}, buf, sizeof buf);
  TEST_ASSERT_NOT_NULL(strstr(buf, "A/B"));
}

void test_describe_wrong_encoder_says_crossed() {
  describeResult(kSteps[2], {Verdict::Fail, 0, 1, +1}, buf, sizeof buf);
  TEST_ASSERT_NOT_NULL(strstr(buf, "encoder 2"));
}

void test_describe_skip() {
  describeResult(kSteps[0], {Verdict::Skipped, 0, 0, 0}, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("SKIP: no signal - check wire, solder joint, GND", buf);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_on_first_step);
  RUN_TEST(test_correct_button_passes_and_advances);
  RUN_TEST(test_release_only_is_pending);
  RUN_TEST(test_wrong_button_fails_with_bits_and_advances);
  RUN_TEST(test_expected_plus_extra_bit_fails);
  RUN_TEST(test_multi_bit_mask_passes_on_any_bit_in_mask);
  RUN_TEST(test_encoder_ignored_during_button_step);
  RUN_TEST(test_encoder_correct_direction_passes);
  RUN_TEST(test_encoder_reversed_direction_fails);
  RUN_TEST(test_wrong_encoder_fails);
  RUN_TEST(test_buttons_ignored_during_encoder_step);
  RUN_TEST(test_skip_advances_and_counts);
  RUN_TEST(test_done_after_last_step_and_tallies);
  RUN_TEST(test_events_after_done_are_pending);
  RUN_TEST(test_describe_pass);
  RUN_TEST(test_describe_wrong_button_names_pin);
  RUN_TEST(test_describe_extra_bits_suggests_short);
  RUN_TEST(test_describe_reversed_encoder_suggests_swap);
  RUN_TEST(test_describe_wrong_encoder_says_crossed);
  RUN_TEST(test_describe_skip);
  return UNITY_END();
}
