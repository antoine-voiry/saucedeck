#include <unity.h>
#include "led_effects.h"

void setUp() {}
void tearDown() {}

static const size_t N = 16;
static const LedInputs kIdle = {50, false, false};

static bool allEqual(const Rgb *px, size_t n, Rgb c) {
  for (size_t i = 0; i < n; i++)
    if (px[i].r != c.r || px[i].g != c.g || px[i].b != c.b) return false;
  return true;
}
static bool isOff(Rgb c) { return c.r == 0 && c.g == 0 && c.b == 0; }

// --- power limiter ---

void test_power_estimate_full_white() {
  Rgb px[4];
  for (auto &p : px) p = {255, 255, 255};
  TEST_ASSERT_EQUAL_UINT32(240, estimateMilliamps(px, 4));  // 4 x 60 mA
}

void test_power_limit_scales_down_over_budget() {
  Rgb px[N];
  for (auto &p : px) p = {255, 255, 255};  // 960 mA
  limitPower(px, N, 250);
  TEST_ASSERT_TRUE(estimateMilliamps(px, N) <= 250);
  TEST_ASSERT_TRUE(px[0].r > 0);
}

void test_power_limit_leaves_low_frames_untouched() {
  Rgb px[N];
  for (auto &p : px) p = {10, 0, 0};
  limitPower(px, N, 250);
  TEST_ASSERT_EQUAL_UINT8(10, px[0].r);
}

// --- effects ---

void test_idle_is_dim_not_off_not_bright() {
  LedEffects e;
  Rgb px[N];
  e.render(kIdle, 10000, px, N);
  TEST_ASSERT_FALSE(isOff(px[0]));
  // Bench 2026-10-19: {0,4,12} was too bright at idle -> keep every channel <= 4
  TEST_ASSERT_TRUE(px[0].r <= 4 && px[0].g <= 4 && px[0].b <= 4);
  TEST_ASSERT_TRUE(allEqual(px, N, px[0]));
}

void test_mic_muted_idle_is_red() {
  LedEffects e;
  Rgb px[N];
  e.render({50, false, true}, 10000, px, N);
  TEST_ASSERT_TRUE(px[0].r > 0 && px[0].r <= 4);
  TEST_ASSERT_EQUAL_UINT8(0, px[0].g);
  TEST_ASSERT_EQUAL_UINT8(0, px[0].b);
}

void test_key_flash_then_fades_to_idle() {
  LedEffects e;
  Rgb idle[N], px[N];
  e.render(kIdle, 0, idle, N);
  e.onAction({ActionType::KeyDown, 0}, 1000);
  e.render(kIdle, 1000, px, N);
  TEST_ASSERT_TRUE(px[0].r + px[0].g + px[0].b > idle[0].r + idle[0].g + idle[0].b + 100);
  e.render(kIdle, 1000 + 1000, px, N);
  TEST_ASSERT_TRUE(allEqual(px, N, idle[0]));
}

void test_keys_flash_different_colors() {
  LedEffects a, b;
  Rgb pa[N], pb[N];
  a.onAction({ActionType::KeyDown, 0}, 0);
  b.onAction({ActionType::KeyDown, 6}, 0);
  a.render(kIdle, 0, pa, N);
  b.render(kIdle, 0, pb, N);
  TEST_ASSERT_FALSE(pa[0].r == pb[0].r && pa[0].g == pb[0].g && pa[0].b == pb[0].b);
}

void test_key_release_does_not_flash() {
  LedEffects e;
  Rgb idle[N], px[N];
  e.render(kIdle, 0, idle, N);
  e.onAction({ActionType::KeyUp, 0}, 1000);
  e.render(kIdle, 1000, px, N);
  TEST_ASSERT_TRUE(allEqual(px, N, idle[0]));
}

void test_mic_mute_flash_is_red() {
  LedEffects e;
  Rgb px[N];
  e.onAction({ActionType::MicMute, 0}, 0);
  e.render(kIdle, 0, px, N);
  TEST_ASSERT_TRUE(px[0].r > 100);
  TEST_ASSERT_TRUE(px[0].g < 40 && px[0].b < 40);
}

void test_sync_flash_is_green() {
  LedEffects e;
  Rgb px[N];
  e.onAction({ActionType::Sync, 0}, 0);
  e.render(kIdle, 0, px, N);
  TEST_ASSERT_TRUE(px[0].g > 100);
  TEST_ASSERT_TRUE(px[0].r < 40 && px[0].b < 40);
}

void test_volume_turn_shows_gauge() {
  LedEffects e;
  Rgb px[N];
  e.onAction({ActionType::Consumer, (uint8_t)ConsumerCode::VolumeUp}, 0);
  e.render({50, false, false}, 10, px, N);
  // 50 % of 16 = first 8 lit cyan, rest off
  TEST_ASSERT_TRUE(px[7].b > 100 && px[7].g > 100 && px[7].r == 0);
  TEST_ASSERT_TRUE(isOff(px[8]));
}

void test_volume_gauge_zero_and_full() {
  LedEffects e;
  Rgb px[N];
  e.onAction({ActionType::Consumer, (uint8_t)ConsumerCode::VolumeDown}, 0);
  e.render({0, false, false}, 10, px, N);
  TEST_ASSERT_TRUE(isOff(px[0]));
  e.render({100, false, false}, 20, px, N);
  TEST_ASSERT_FALSE(isOff(px[N - 1]));
}

void test_volume_gauge_grey_when_muted() {
  LedEffects e;
  Rgb px[N];
  e.onAction({ActionType::Consumer, (uint8_t)ConsumerCode::VolumeUp}, 0);
  e.render({50, true, false}, 10, px, N);
  TEST_ASSERT_TRUE(px[0].r == px[0].g && px[0].g == px[0].b && px[0].r > 0);
}

void test_volume_gauge_times_out() {
  LedEffects e;
  Rgb idle[N], px[N];
  e.render(kIdle, 0, idle, N);
  e.onAction({ActionType::Consumer, (uint8_t)ConsumerCode::VolumeUp}, 1000);
  e.render(kIdle, 1000 + 1600, px, N);
  TEST_ASSERT_TRUE(allEqual(px, N, idle[0]));
}

void test_render_respects_power_budget() {
  LedEffects e;
  Rgb px[64];
  e.onAction({ActionType::Consumer, (uint8_t)ConsumerCode::NextTrack}, 0);  // white flash
  e.render(kIdle, 0, px, 64);
  TEST_ASSERT_TRUE(estimateMilliamps(px, 64) <= LedEffects::kBudgetMa);
}

void test_millis_wrap_flash_still_fades() {
  LedEffects e;
  Rgb idle[N], px[N];
  e.render(kIdle, 0, idle, N);
  e.onAction({ActionType::KeyDown, 1}, 0xFFFFFF00u);
  e.render(kIdle, 0x00001000u, px, N);  // ~4.3 s later across wrap
  TEST_ASSERT_TRUE(allEqual(px, N, idle[0]));
}

void test_wifi_mode_caps_power_and_keeps_indicator() {
  LedEffects e;
  e.setWifi(true);
  Rgb px[N];
  for (uint32_t t = 0; t < 2000; t += 50) {
    e.render(kIdle, t, px, N);
    TEST_ASSERT_TRUE(estimateMilliamps(px, N) <= LedEffects::kWifiBudgetMa);
    for (size_t i = 1; i < N; i++) TEST_ASSERT_TRUE(isOff(px[i]));  // idle: indicator only
    TEST_ASSERT_EQUAL_UINT8(0, px[0].r);
  }
}

void test_wifi_mode_key_press_still_shows_count_capped() {
  LedEffects e;
  e.setWifi(true);
  e.onAction({ActionType::KeyDown, 4}, 0);  // key 5
  Rgb px[N];
  e.render(kIdle, 0, px, N);
  TEST_ASSERT_TRUE(estimateMilliamps(px, N) <= LedEffects::kWifiBudgetMa);
  for (size_t i = 1; i < 5; i++) TEST_ASSERT_FALSE(isOff(px[i]));
  for (size_t i = 5; i < N; i++) TEST_ASSERT_TRUE(isOff(px[i]));
}

void test_wifi_indicator_pulses() {
  LedEffects e;
  e.setWifi(true);
  Rgb a[N], b[N];
  e.render(kIdle, 0, a, N);
  e.render(kIdle, 500, b, N);
  TEST_ASSERT_TRUE(a[0].b != b[0].b);
}

void test_helper_missing_flashes_orange() {
  LedEffects e;
  Rgb px[N];
  e.onAction({ActionType::HelperMissing, 3}, 0);
  e.render(kIdle, 0, px, N);
  TEST_ASSERT_TRUE(px[0].r > 150 && px[0].g < 80);
}

void test_dim_scales_frame_and_zero_is_off() {
  LedEffects e;
  Rgb full[N], half[N], off[N];
  e.onAction({ActionType::Sync, 0}, 0);
  e.render(kIdle, 0, full, N);
  e.setDim(128);
  e.render(kIdle, 0, half, N);
  TEST_ASSERT_TRUE(half[0].g <= full[0].g / 2 + 1 && half[0].g > 0);
  e.setDim(0);
  e.render(kIdle, 0, off, N);
  for (size_t i = 0; i < N; i++) TEST_ASSERT_TRUE(isOff(off[i]));
}

// --- key N lights N LEDs, rainbow (owner request 2026-10-22) ---

static bool brighterThan(Rgb a, Rgb b) { return a.r + a.g + a.b > b.r + b.g + b.b; }

void test_key_n_lights_first_n_leds() {
  LedEffects e;
  Rgb idle[N], px[N];
  e.render(kIdle, 0, idle, N);
  e.onAction({ActionType::KeyDown, 2}, 1000);  // key 3
  e.render(kIdle, 1000, px, N);
  for (size_t i = 0; i < 3; i++) TEST_ASSERT_TRUE(brighterThan(px[i], idle[i]));
  for (size_t i = 3; i < N; i++) TEST_ASSERT_TRUE(allEqual(&px[i], 1, idle[i]));
}

void test_key_12_lights_12_leds() {
  LedEffects e;
  Rgb idle[N], px[N];
  e.render(kIdle, 0, idle, N);
  e.onAction({ActionType::KeyDown, 11}, 1000);
  e.render(kIdle, 1000, px, N);
  TEST_ASSERT_TRUE(brighterThan(px[11], idle[11]));
  // 12 lit LEDs exceed the 250 mA cap, so the limiter scales the whole frame: LED 13 is not lit
  // (at most idle brightness), not necessarily equal to idle.
  TEST_ASSERT_FALSE(brighterThan(px[12], idle[12]));
}

void test_key_leds_are_rainbow_not_one_colour() {
  LedEffects e;
  Rgb px[N];
  e.onAction({ActionType::KeyDown, 5}, 0);  // 6 LEDs
  e.render(kIdle, 0, px, N);
  TEST_ASSERT_FALSE(px[0].r == px[5].r && px[0].g == px[5].g && px[0].b == px[5].b);
}

void test_helper_action_uses_same_count_pattern() {
  LedEffects e;
  Rgb idle[N], px[N];
  e.render(kIdle, 0, idle, N);
  e.onAction({ActionType::HelperAction, 1}, 1000);  // key 2
  e.render(kIdle, 1000, px, N);
  TEST_ASSERT_TRUE(brighterThan(px[1], idle[1]));
  TEST_ASSERT_TRUE(allEqual(&px[2], 1, idle[2]));
}

void test_hsv_primaries() {
  Rgb r = hsvToRgb(0, 255), g = hsvToRgb(120, 255), b = hsvToRgb(240, 255);
  TEST_ASSERT_TRUE(r.r == 255 && r.g == 0 && r.b == 0);
  TEST_ASSERT_TRUE(g.g == 255 && g.r == 0 && g.b == 0);
  TEST_ASSERT_TRUE(b.b == 255 && b.r == 0 && b.g == 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_power_estimate_full_white);
  RUN_TEST(test_power_limit_scales_down_over_budget);
  RUN_TEST(test_power_limit_leaves_low_frames_untouched);
  RUN_TEST(test_idle_is_dim_not_off_not_bright);
  RUN_TEST(test_mic_muted_idle_is_red);
  RUN_TEST(test_key_flash_then_fades_to_idle);
  RUN_TEST(test_keys_flash_different_colors);
  RUN_TEST(test_key_release_does_not_flash);
  RUN_TEST(test_mic_mute_flash_is_red);
  RUN_TEST(test_sync_flash_is_green);
  RUN_TEST(test_volume_turn_shows_gauge);
  RUN_TEST(test_volume_gauge_zero_and_full);
  RUN_TEST(test_volume_gauge_grey_when_muted);
  RUN_TEST(test_volume_gauge_times_out);
  RUN_TEST(test_render_respects_power_budget);
  RUN_TEST(test_millis_wrap_flash_still_fades);
  RUN_TEST(test_wifi_mode_caps_power_and_keeps_indicator);
  RUN_TEST(test_wifi_mode_key_press_still_shows_count_capped);
  RUN_TEST(test_wifi_indicator_pulses);
  RUN_TEST(test_helper_missing_flashes_orange);
  RUN_TEST(test_dim_scales_frame_and_zero_is_off);
  RUN_TEST(test_key_n_lights_first_n_leds);
  RUN_TEST(test_key_12_lights_12_leds);
  RUN_TEST(test_key_leds_are_rainbow_not_one_colour);
  RUN_TEST(test_helper_action_uses_same_count_pattern);
  RUN_TEST(test_hsv_primaries);
  return UNITY_END();
}
