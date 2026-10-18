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
  TEST_ASSERT_TRUE(px[0].r < 32 && px[0].g < 32 && px[0].b < 32);
  TEST_ASSERT_TRUE(allEqual(px, N, px[0]));
}

void test_mic_muted_idle_is_red() {
  LedEffects e;
  Rgb px[N];
  e.render({50, false, true}, 10000, px, N);
  TEST_ASSERT_TRUE(px[0].r > 0);
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
  return UNITY_END();
}
