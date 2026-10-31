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

// --- barDeltaH: only the span between old and new fill is redrawn (bar grows rightward) ---

void test_delta_grow_paints_span_after_old_fill() {
  Span s = barDeltaH(10, 30, 5);
  TEST_ASSERT_TRUE(s.filled);
  TEST_ASSERT_EQUAL_UINT16(15, s.x);
  TEST_ASSERT_EQUAL_UINT16(20, s.w);
}

void test_delta_shrink_clears_span() {
  Span s = barDeltaH(30, 10, 5);
  TEST_ASSERT_FALSE(s.filled);
  TEST_ASSERT_EQUAL_UINT16(15, s.x);
  TEST_ASSERT_EQUAL_UINT16(20, s.w);
}

void test_delta_no_change_is_empty() {
  TEST_ASSERT_EQUAL_UINT16(0, barDeltaH(42, 42, 5).w);
}

// --- ScreenModel v2: dirty flags + tiles to repaint ---

static ScreenState base() {
  ScreenState s{};
  s.volumePercent = 20;
  s.banner = BANNER_NONE;
  s.clockMinute = -1;
  return s;
}

void test_model_first_frame_all_dirty_all_tiles() {
  ScreenModel m;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_ALL, m.update(base(), 0));
  TEST_ASSERT_EQUAL_HEX16(0x0FFF, m.tileMask());
}

void test_model_same_state_clean() {
  ScreenModel m;
  m.update(base(), 0);
  TEST_ASSERT_EQUAL_HEX16(DIRTY_NONE, m.update(base(), 0));
  TEST_ASSERT_EQUAL_HEX16(0, m.tileMask());
}

void test_model_volume_change_marks_bar_and_text() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.volumePercent = 26;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_BAR | DIRTY_VOLUME_TEXT, m.update(s, 0));
}

void test_model_speaker_mute_marks_color_and_text() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.speakerMuted = true;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_BAR_COLOR | DIRTY_VOLUME_TEXT, m.update(s, 0));
}

void test_model_mic_changes_mark_mic_only() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.micMuted = true;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_MIC, m.update(s, 0));
  s.micLevelKnown = true;
  s.micPercent = 45;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_MIC, m.update(s, 0));
}

void test_model_banner_change_marks_banner() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.banner = BANNER_APPLY;
  s.bannerArg = 30;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_BANNER, m.update(s, 0));
  s.bannerArg = 29;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_BANNER, m.update(s, 0));
}

void test_model_helper_and_wifi_mark_top_bar() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.helperPresent = true;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TOPBAR, m.update(s, 0));
  s.wifiOn = true;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TOPBAR, m.update(s, 0));
}

void test_model_clock_minute_marks_top_bar() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.clockMinute = 14 * 60 + 32;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TOPBAR, m.update(s, 0));
}

void test_model_status_gen_marks_top_bar_and_line2() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.statusGen = 1;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TOPBAR | DIRTY_BANNER, m.update(s, 0));
}

void test_model_tile_changes_pass_through() {
  ScreenModel m;
  m.update(base(), 0);
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TILES, m.update(base(), (1u << 3) | (1u << 9)));
  TEST_ASSERT_EQUAL_HEX16((1u << 3) | (1u << 9), m.tileMask());
}

void test_model_keymap_change_repaints_every_tile() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.keymapGen = 1;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TILES, m.update(s, 0));
  TEST_ASSERT_EQUAL_HEX16(0x0FFF, m.tileMask());
}

void test_model_previous_tracks_last_drawn() {
  ScreenModel m;
  ScreenState s = base();
  s.volumePercent = 50;
  m.update(s, 0);
  s.volumePercent = 75;
  m.update(s, 0);
  TEST_ASSERT_EQUAL_UINT8(75, m.current().volumePercent);
  TEST_ASSERT_EQUAL_UINT8(50, m.previous().volumePercent);
}

void test_model_invalidate_forces_full_redraw() {
  ScreenModel m;
  m.update(base(), 0);
  m.invalidate();
  TEST_ASSERT_EQUAL_HEX16(DIRTY_ALL, m.update(base(), 0));
  TEST_ASSERT_EQUAL_HEX16(0x0FFF, m.tileMask());
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
  RUN_TEST(test_delta_grow_paints_span_after_old_fill);
  RUN_TEST(test_delta_shrink_clears_span);
  RUN_TEST(test_delta_no_change_is_empty);
  RUN_TEST(test_model_first_frame_all_dirty_all_tiles);
  RUN_TEST(test_model_same_state_clean);
  RUN_TEST(test_model_volume_change_marks_bar_and_text);
  RUN_TEST(test_model_speaker_mute_marks_color_and_text);
  RUN_TEST(test_model_mic_changes_mark_mic_only);
  RUN_TEST(test_model_banner_change_marks_banner);
  RUN_TEST(test_model_helper_and_wifi_mark_top_bar);
  RUN_TEST(test_model_clock_minute_marks_top_bar);
  RUN_TEST(test_model_status_gen_marks_top_bar_and_line2);
  RUN_TEST(test_model_tile_changes_pass_through);
  RUN_TEST(test_model_keymap_change_repaints_every_tile);
  RUN_TEST(test_model_previous_tracks_last_drawn);
  RUN_TEST(test_model_invalidate_forces_full_redraw);
  RUN_TEST(test_dimmer_full_until_fade_start);
  RUN_TEST(test_dimmer_linear_fade_and_off);
  RUN_TEST(test_dimmer_activity_wakes_instantly);
  RUN_TEST(test_dimmer_millis_wrap);
  return UNITY_END();
}
