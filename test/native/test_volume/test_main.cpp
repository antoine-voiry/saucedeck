#include <unity.h>
#include "volume_state.h"
#include "knob_chord.h"

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

// --- KnobChord v2: single knob -> on release; both: release < 5 s = SYNC, 5 s held = WIFI ---

static const uint16_t S1 = 1u << 12, S2 = 1u << 13;

void test_chord_single_knob_fires_on_release() {
  KnobChord c(S1, S2);
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S1, 0));
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S1, 10));
  TEST_ASSERT_EQUAL_HEX8(CHORD_KNOB1, c.update(0, 20));
}

void test_chord_knob2_alone() {
  KnobChord c(S1, S2);
  c.update(S2, 0);
  TEST_ASSERT_EQUAL_HEX8(CHORD_KNOB2, c.update(0, 10));
}

void test_chord_sync_fires_on_release_before_5s() {
  KnobChord c(S1, S2);
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S1 | S2, 0));
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S1 | S2, 4999));
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S2, 5000));  // one up: chord still running
  TEST_ASSERT_EQUAL_HEX8(CHORD_SYNC, c.update(0, 5010));
}

void test_chord_wifi_fires_once_at_5s_while_held() {
  KnobChord c(S1, S2);
  c.update(S1 | S2, 100);
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S1 | S2, 5099));
  TEST_ASSERT_EQUAL_HEX8(CHORD_WIFI, c.update(S1 | S2, 5100));
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S1 | S2, 9000));
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(0, 9010));  // no SYNC after WIFI
}

void test_chord_releases_after_chord_fire_no_single_knob() {
  KnobChord c(S1, S2);
  c.update(S1 | S2, 0);
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S2, 100));
  TEST_ASSERT_EQUAL_HEX8(CHORD_SYNC, c.update(0, 200));
}

void test_chord_rearms_after_both_released() {
  KnobChord c(S1, S2);
  c.update(S1 | S2, 0);
  c.update(0, 10);
  c.update(S1, 20);
  TEST_ASSERT_EQUAL_HEX8(CHORD_KNOB1, c.update(0, 30));
}

void test_chord_still_held_partner_blocks_single_action() {
  // Knob 2 still held, knob 1 pressed again and released: still part of the chord
  KnobChord c(S1, S2);
  c.update(S1 | S2, 0);
  c.update(S2, 10);
  c.update(S1 | S2, 20);
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S2, 30));
}

void test_chord_ignores_other_bits() {
  KnobChord c(S1, S2);
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(0x0FFF, 0));
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(0, 10));
}

void test_chord_countdown() {
  KnobChord c(S1, S2);
  TEST_ASSERT_EQUAL_UINT32(0, c.bothHeldMs(0));
  c.update(S1 | S2, 1000);
  TEST_ASSERT_EQUAL_UINT32(1500, c.bothHeldMs(2500));
  c.update(S1, 2600);
  TEST_ASSERT_EQUAL_UINT32(0, c.bothHeldMs(2700));
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
  RUN_TEST(test_chord_single_knob_fires_on_release);
  RUN_TEST(test_chord_knob2_alone);
  RUN_TEST(test_chord_sync_fires_on_release_before_5s);
  RUN_TEST(test_chord_wifi_fires_once_at_5s_while_held);
  RUN_TEST(test_chord_releases_after_chord_fire_no_single_knob);
  RUN_TEST(test_chord_rearms_after_both_released);
  RUN_TEST(test_chord_still_held_partner_blocks_single_action);
  RUN_TEST(test_chord_ignores_other_bits);
  RUN_TEST(test_chord_countdown);
  return UNITY_END();
}
