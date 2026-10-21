#include <unity.h>
#include "ui_layout.h"
#include "idle_dimmer.h"

void setUp() {}
void tearDown() {}

// --- barFill: percent -> filled pixels of the bar interior ---

void test_fill_zero_and_full() {
  TEST_ASSERT_EQUAL_UINT16(0, barFill(0, 178));
  TEST_ASSERT_EQUAL_UINT16(178, barFill(100, 178));
}

void test_fill_rounds_nearest() {
  TEST_ASSERT_EQUAL_UINT16(89, barFill(50, 178));
  TEST_ASSERT_EQUAL_UINT16(11, barFill(6, 178));  // 10.68 -> 11
}

void test_fill_clamps_over_100() {
  TEST_ASSERT_EQUAL_UINT16(178, barFill(150, 178));
}

// --- barDelta: only the strip between old and new fill is redrawn (bar grows upward) ---

void test_delta_grow_paints_strip_above_old_fill() {
  // interior y = 31..208 (top 31, h 178); bottom = 209
  Strip s = barDelta(10, 30, 31, 178);
  TEST_ASSERT_TRUE(s.filled);
  TEST_ASSERT_EQUAL_UINT16(209 - 30, s.y);
  TEST_ASSERT_EQUAL_UINT16(20, s.h);
}

void test_delta_shrink_clears_strip() {
  Strip s = barDelta(30, 10, 31, 178);
  TEST_ASSERT_FALSE(s.filled);
  TEST_ASSERT_EQUAL_UINT16(209 - 30, s.y);
  TEST_ASSERT_EQUAL_UINT16(20, s.h);
}

void test_delta_no_change_is_empty() {
  Strip s = barDelta(42, 42, 31, 178);
  TEST_ASSERT_EQUAL_UINT16(0, s.h);
}

// --- ScreenModel: dirty flags so the glue redraws only what changed ---

void test_model_first_frame_all_dirty() {
  ScreenModel m;
  TEST_ASSERT_EQUAL_HEX8(DIRTY_ALL, m.update({0, false, false, false, 0, BANNER_NONE, 0}));
}

void test_model_same_state_clean() {
  ScreenModel m;
  m.update({20, false, false, false, 0, BANNER_NONE, 0});
  TEST_ASSERT_EQUAL_HEX8(DIRTY_NONE, m.update({20, false, false, false, 0, BANNER_NONE, 0}));
}

void test_model_volume_change_marks_bar_and_text() {
  ScreenModel m;
  m.update({20, false, false, false, 0, BANNER_NONE, 0});
  TEST_ASSERT_EQUAL_HEX8(DIRTY_BAR | DIRTY_VOLUME_TEXT, m.update({26, false, false, false, 0, BANNER_NONE, 0}));
}

void test_model_speaker_mute_marks_color_and_icon() {
  // Muted bar keeps its level but is drawn grey: full bar repaint
  ScreenModel m;
  m.update({20, false, false, false, 0, BANNER_NONE, 0});
  TEST_ASSERT_EQUAL_HEX8(DIRTY_BAR_COLOR | DIRTY_SPEAKER_ICON, m.update({20, true, false, false, 0, BANNER_NONE, 0}));
}

void test_model_mic_change_marks_mic_only() {
  ScreenModel m;
  m.update({20, false, false, false, 0, BANNER_NONE, 0});
  TEST_ASSERT_EQUAL_HEX8(DIRTY_MIC, m.update({20, false, true, false, 0, BANNER_NONE, 0}));
}

void test_model_previous_fill_tracks_last_drawn() {
  ScreenModel m;
  m.update({50, false, false, false, 0, BANNER_NONE, 0});
  TEST_ASSERT_EQUAL_UINT8(50, m.current().volumePercent);
  m.update({75, false, false, false, 0, BANNER_NONE, 0});
  TEST_ASSERT_EQUAL_UINT8(75, m.current().volumePercent);
  TEST_ASSERT_EQUAL_UINT8(50, m.previous().volumePercent);
}

void test_model_invalidate_forces_full_redraw() {
  ScreenModel m;
  m.update({20, false, false, false, 0, BANNER_NONE, 0});
  m.invalidate();
  TEST_ASSERT_EQUAL_HEX8(DIRTY_ALL, m.update({20, false, false, false, 0, BANNER_NONE, 0}));
}

void test_model_mic_level_change_marks_mic() {
  ScreenModel m;
  m.update({20, false, false, true, 40, BANNER_NONE, 0});
  TEST_ASSERT_EQUAL_HEX8(DIRTY_MIC, m.update({20, false, false, true, 45, BANNER_NONE, 0}));
}

void test_model_banner_change_marks_banner() {
  ScreenModel m;
  m.update({20, false, false, false, 0, BANNER_NONE, 0});
  TEST_ASSERT_EQUAL_HEX8(DIRTY_BANNER, m.update({20, false, false, false, 0, BANNER_APPLY, 30}));
  TEST_ASSERT_EQUAL_HEX8(DIRTY_BANNER, m.update({20, false, false, false, 0, BANNER_APPLY, 29}));
}

// --- IdleDimmer: full 120 s, linear fade, off at 240 s ---

void test_dimmer_full_until_fade_start() {
  IdleDimmer d;
  d.onActivity(1000);
  TEST_ASSERT_EQUAL_UINT8(255, d.level(1000));
  TEST_ASSERT_EQUAL_UINT8(255, d.level(1000 + IdleDimmer::kFadeStartMs - 1));
}

void test_dimmer_linear_fade_and_off() {
  IdleDimmer d;
  d.onActivity(0);
  TEST_ASSERT_EQUAL_UINT8(255, d.level(IdleDimmer::kFadeStartMs));
  TEST_ASSERT_EQUAL_UINT8(127, d.level(180000));  // halfway
  TEST_ASSERT_EQUAL_UINT8(0, d.level(IdleDimmer::kOffMs));
  TEST_ASSERT_EQUAL_UINT8(0, d.level(IdleDimmer::kOffMs + 999999));
}

void test_dimmer_activity_wakes_instantly() {
  IdleDimmer d;
  d.onActivity(0);
  TEST_ASSERT_EQUAL_UINT8(0, d.level(300000));
  d.onActivity(300000);
  TEST_ASSERT_EQUAL_UINT8(255, d.level(300000));
}

void test_dimmer_millis_wrap() {
  IdleDimmer d;
  d.onActivity(0xFFFFFF00u);
  TEST_ASSERT_EQUAL_UINT8(255, d.level(0x00001000u));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_fill_zero_and_full);
  RUN_TEST(test_fill_rounds_nearest);
  RUN_TEST(test_fill_clamps_over_100);
  RUN_TEST(test_delta_grow_paints_strip_above_old_fill);
  RUN_TEST(test_delta_shrink_clears_strip);
  RUN_TEST(test_delta_no_change_is_empty);
  RUN_TEST(test_model_first_frame_all_dirty);
  RUN_TEST(test_model_same_state_clean);
  RUN_TEST(test_model_volume_change_marks_bar_and_text);
  RUN_TEST(test_model_speaker_mute_marks_color_and_icon);
  RUN_TEST(test_model_mic_change_marks_mic_only);
  RUN_TEST(test_model_previous_fill_tracks_last_drawn);
  RUN_TEST(test_model_invalidate_forces_full_redraw);
  RUN_TEST(test_model_mic_level_change_marks_mic);
  RUN_TEST(test_model_banner_change_marks_banner);
  RUN_TEST(test_dimmer_full_until_fade_start);
  RUN_TEST(test_dimmer_linear_fade_and_off);
  RUN_TEST(test_dimmer_activity_wakes_instantly);
  RUN_TEST(test_dimmer_millis_wrap);
  return UNITY_END();
}
