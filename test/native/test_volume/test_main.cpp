#include <unity.h>
#include "volume_state.h"

void setUp() {}
void tearDown() {}

// Level is kept in host volume steps (macOS: 16 per full range, unverified).

void test_volume_starts_at_zero_unmuted() {
  VolumeState v(16);
  TEST_ASSERT_EQUAL_UINT8(0, v.steps());
  TEST_ASSERT_EQUAL_UINT8(0, v.percent());
  TEST_ASSERT_FALSE(v.muted());
}

void test_volume_rotate_up_and_down() {
  VolumeState v(16);
  v.rotate(3);
  TEST_ASSERT_EQUAL_UINT8(3, v.steps());
  v.rotate(-1);
  TEST_ASSERT_EQUAL_UINT8(2, v.steps());
}

void test_volume_clamps_at_zero_and_max() {
  VolumeState v(16);
  v.rotate(-5);
  TEST_ASSERT_EQUAL_UINT8(0, v.steps());
  v.rotate(40);
  TEST_ASSERT_EQUAL_UINT8(16, v.steps());
}

void test_volume_percent_scales_to_max() {
  VolumeState v(16);
  v.rotate(4);
  TEST_ASSERT_EQUAL_UINT8(25, v.percent());
  v.rotate(12);
  TEST_ASSERT_EQUAL_UINT8(100, v.percent());
  VolumeState w(50);  // Windows-like 2 % steps
  w.rotate(1);
  TEST_ASSERT_EQUAL_UINT8(2, w.percent());
}

void test_volume_percent_rounds_nearest() {
  VolumeState v(16);
  v.rotate(1);  // 6.25 %
  TEST_ASSERT_EQUAL_UINT8(6, v.percent());
  v.rotate(1);  // 12.5 %
  TEST_ASSERT_EQUAL_UINT8(13, v.percent());
}

void test_volume_mute_toggles() {
  VolumeState v(16);
  v.toggleMute();
  TEST_ASSERT_TRUE(v.muted());
  v.toggleMute();
  TEST_ASSERT_FALSE(v.muted());
}

void test_volume_mute_keeps_level() {
  VolumeState v(16);
  v.rotate(5);
  v.toggleMute();
  TEST_ASSERT_EQUAL_UINT8(5, v.steps());
}

void test_volume_up_while_muted_unmutes() {
  // Matches upstream SauceDeck; host behaviour on macOS unverified
  VolumeState v(16);
  v.rotate(5);
  v.toggleMute();
  v.rotate(1);
  TEST_ASSERT_FALSE(v.muted());
  TEST_ASSERT_EQUAL_UINT8(6, v.steps());
}

void test_volume_down_while_muted_stays_muted() {
  VolumeState v(16);
  v.rotate(5);
  v.toggleMute();
  v.rotate(-1);
  TEST_ASSERT_TRUE(v.muted());
  TEST_ASSERT_EQUAL_UINT8(4, v.steps());
}

void test_volume_sync_resets_to_zero_unmuted() {
  VolumeState v(16);
  v.rotate(9);
  v.toggleMute();
  v.sync();
  TEST_ASSERT_EQUAL_UINT8(0, v.steps());
  TEST_ASSERT_FALSE(v.muted());
}

void test_volume_zero_max_does_not_divide_by_zero() {
  VolumeState v(0);
  v.rotate(1);
  TEST_ASSERT_EQUAL_UINT8(100, v.percent());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_volume_starts_at_zero_unmuted);
  RUN_TEST(test_volume_rotate_up_and_down);
  RUN_TEST(test_volume_clamps_at_zero_and_max);
  RUN_TEST(test_volume_percent_scales_to_max);
  RUN_TEST(test_volume_percent_rounds_nearest);
  RUN_TEST(test_volume_mute_toggles);
  RUN_TEST(test_volume_mute_keeps_level);
  RUN_TEST(test_volume_up_while_muted_unmutes);
  RUN_TEST(test_volume_down_while_muted_stays_muted);
  RUN_TEST(test_volume_sync_resets_to_zero_unmuted);
  RUN_TEST(test_volume_zero_max_does_not_divide_by_zero);
  return UNITY_END();
}
