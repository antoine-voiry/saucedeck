#include <unity.h>
#include "deck.h"
#include "mcp23017.h"

void setUp() {}
void tearDown() {}

// Helpers: feed a stable mask long enough to pass the 25 ms debounce.
static ActionList hold(Deck &d, uint16_t mask, uint32_t &t) {
  ActionList a;
  d.onKeys(mask, t, a);
  t += 30;
  d.onKeys(mask, t, a);
  t += 1;
  return a;
}

// Expander reads failing continuously for Deck::kBusLostMs -> bus declared lost.
static void loseBus(Deck &d, ActionList &a, uint32_t &t) {
  d.onReadFailed(t, a);
  t += Deck::kBusLostMs;
  d.onReadFailed(t, a);
  t += 1;
}

static bool has(const ActionList &a, ActionType type, uint8_t value = 0) {
  for (size_t i = 0; i < a.count; i++)
    if (a.items[i].type == type && a.items[i].value == value) return true;
  return false;
}

void test_deck_key_press_and_release() {
  Deck d;
  uint32_t t = 0;
  ActionList a = hold(d, 1u << 3, t);
  TEST_ASSERT_EQUAL(1, a.count);
  TEST_ASSERT_TRUE(has(a, ActionType::KeyDown, 3));
  a = hold(d, 0, t);
  TEST_ASSERT_EQUAL(1, a.count);
  TEST_ASSERT_TRUE(has(a, ActionType::KeyUp, 3));
}

void test_deck_bounce_emits_nothing() {
  Deck d;
  ActionList a;
  d.onKeys(1u << 0, 0, a);
  d.onKeys(0, 5, a);
  d.onKeys(0, 100, a);
  TEST_ASSERT_EQUAL(0, a.count);
}

void test_deck_knob1_release_mutes_speaker() {
  Deck d;
  uint32_t t = 0;
  hold(d, DECK_ENC1_SW, t);
  ActionList a = hold(d, 0, t);
  TEST_ASSERT_TRUE(has(a, ActionType::Consumer, (uint8_t)ConsumerCode::Mute));
  TEST_ASSERT_TRUE(d.speaker().muted());
}

void test_deck_knob2_release_toggles_mic() {
  Deck d;
  uint32_t t = 0;
  hold(d, DECK_ENC2_SW, t);
  ActionList a = hold(d, 0, t);
  TEST_ASSERT_TRUE(has(a, ActionType::MicMute));
  TEST_ASSERT_TRUE(d.micMuted());
  hold(d, DECK_ENC2_SW, t);
  hold(d, 0, t);
  TEST_ASSERT_FALSE(d.micMuted());
}

void test_deck_knob_switches_never_emit_keys() {
  Deck d;
  uint32_t t = 0;
  ActionList a = hold(d, DECK_ENC1_SW | DECK_ENC2_SW, t);
  for (size_t i = 0; i < a.count; i++) {
    TEST_ASSERT_TRUE(a.items[i].type != ActionType::KeyDown);
  }
}

void test_deck_sync_resets_speaker_and_mic_without_hid() {
  Deck d;
  uint32_t t = 0;
  ActionList a;
  d.onEncoders(3, 0, a);
  hold(d, DECK_ENC2_SW, t);
  hold(d, 0, t);  // mic muted
  a = hold(d, DECK_ENC1_SW | DECK_ENC2_SW, t);
  TEST_ASSERT_EQUAL(0, a.count);  // nothing until release
  a = hold(d, 0, t);
  TEST_ASSERT_EQUAL(1, a.count);  // local Sync only, no HID action
  TEST_ASSERT_TRUE(has(a, ActionType::Sync));
  TEST_ASSERT_EQUAL_UINT8(0, d.speaker().steps());
  TEST_ASSERT_FALSE(d.micMuted());
  a = hold(d, 0, t);
  TEST_ASSERT_EQUAL(0, a.count);
}

void test_deck_knob1_rotation_sends_volume_and_tracks() {
  Deck d;
  ActionList a;
  d.onEncoders(1, 0, a);
  d.onEncoders(1, 0, a);
  d.onEncoders(-1, 0, a);
  TEST_ASSERT_EQUAL(3, a.count);
  TEST_ASSERT_TRUE(a.items[0].type == ActionType::Consumer);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)ConsumerCode::VolumeUp, a.items[0].value);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)ConsumerCode::VolumeDown, a.items[2].value);
  TEST_ASSERT_EQUAL_UINT8(1, d.speaker().steps());
}

void test_deck_multi_detent_sends_one_report_per_step() {
  Deck d;
  ActionList a;
  d.onEncoders(3, 0, a);
  TEST_ASSERT_EQUAL(3, a.count);
}

void test_deck_knob2_rotation_skips_tracks() {
  Deck d;
  ActionList a;
  d.onEncoders(0, 1, a);
  d.onEncoders(0, -1, a);
  TEST_ASSERT_EQUAL(2, a.count);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)ConsumerCode::NextTrack, a.items[0].value);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)ConsumerCode::PrevTrack, a.items[1].value);
  TEST_ASSERT_EQUAL_UINT8(0, d.speaker().steps());
}

void test_deck_bus_lost_releases_held_keys() {
  // Bug: expander drops while F-key held -> host kept key pressed
  Deck d;
  uint32_t t = 0;
  hold(d, 1u << 5, t);
  ActionList a;
  loseBus(d, a, t);
  TEST_ASSERT_EQUAL(1, a.count);
  TEST_ASSERT_TRUE(has(a, ActionType::ReleaseAll));
}

void test_deck_bus_back_with_key_held_presses_again() {
  Deck d;
  uint32_t t = 0;
  hold(d, 1u << 5, t);
  ActionList a;
  loseBus(d, a, t);
  a = hold(d, 1u << 5, t);
  TEST_ASSERT_TRUE(has(a, ActionType::KeyDown, 5));
}

void test_deck_bus_lost_during_knob_hold_fires_no_knob_action() {
  Deck d;
  uint32_t t = 0;
  hold(d, DECK_ENC1_SW, t);
  ActionList a;
  loseBus(d, a, t);
  a = hold(d, 0, t);
  TEST_ASSERT_EQUAL(0, a.count);
  TEST_ASSERT_FALSE(d.speaker().muted());
}

void test_deck_bus_lost_idle_sends_release_all_once() {
  Deck d;
  ActionList a;
  uint32_t t = 0;
  loseBus(d, a, t);
  loseBus(d, a, t);
  TEST_ASSERT_EQUAL(1, a.count);
}

void test_deck_action_list_caps_without_overflow() {
  ActionList a;
  for (int i = 0; i < 40; i++) a.push(ActionType::KeyUp, 0);
  TEST_ASSERT_EQUAL(ActionList::kMax, a.count);
}

void test_deck_single_failed_read_keeps_keys_held() {
  // Bug: one I2C glitch released a held F-key and re-pressed it 25 ms later (double press)
  Deck d;
  uint32_t t = 0;
  hold(d, 1u << 9, t);
  ActionList a;
  d.onReadFailed(t, a);
  a = hold(d, 1u << 9, t);
  TEST_ASSERT_EQUAL(0, a.count);
}

void test_deck_failures_shorter_than_limit_emit_nothing() {
  Deck d;
  uint32_t t = 0;
  hold(d, 1u << 2, t);
  ActionList a;
  d.onReadFailed(t, a);
  d.onReadFailed(t + Deck::kBusLostMs - 1, a);
  TEST_ASSERT_EQUAL(0, a.count);
  d.onReadFailed(t + Deck::kBusLostMs, a);
  TEST_ASSERT_TRUE(has(a, ActionType::ReleaseAll));
}

void test_deck_good_read_restarts_failure_timer() {
  Deck d;
  uint32_t t = 0;
  hold(d, 1u << 2, t);
  ActionList a;
  d.onReadFailed(t, a);
  d.onKeys(1u << 2, t + 10, a);
  d.onReadFailed(t + 15, a);
  d.onReadFailed(t + 15 + Deck::kBusLostMs - 1, a);  // 34 ms after the first failure
  TEST_ASSERT_EQUAL(0, a.count);
}

void test_deck_volume_steps_match_reports_sent() {
  // Bug: rotate() counted every detent but ActionList drops past kMax -> display drift
  Deck d;
  ActionList a;
  d.onEncoders(40, 0, a);
  TEST_ASSERT_EQUAL(ActionList::kMax, a.count);
  TEST_ASSERT_EQUAL_UINT8(16, d.speaker().steps());  // clamped by maxSteps anyway
  Deck e(64);
  ActionList b;
  e.onEncoders(40, 0, b);
  TEST_ASSERT_EQUAL_UINT8(b.count, e.speaker().steps());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_deck_key_press_and_release);
  RUN_TEST(test_deck_bounce_emits_nothing);
  RUN_TEST(test_deck_knob1_release_mutes_speaker);
  RUN_TEST(test_deck_knob2_release_toggles_mic);
  RUN_TEST(test_deck_knob_switches_never_emit_keys);
  RUN_TEST(test_deck_sync_resets_speaker_and_mic_without_hid);
  RUN_TEST(test_deck_knob1_rotation_sends_volume_and_tracks);
  RUN_TEST(test_deck_multi_detent_sends_one_report_per_step);
  RUN_TEST(test_deck_knob2_rotation_skips_tracks);
  RUN_TEST(test_deck_bus_lost_releases_held_keys);
  RUN_TEST(test_deck_bus_back_with_key_held_presses_again);
  RUN_TEST(test_deck_bus_lost_during_knob_hold_fires_no_knob_action);
  RUN_TEST(test_deck_bus_lost_idle_sends_release_all_once);
  RUN_TEST(test_deck_action_list_caps_without_overflow);
  RUN_TEST(test_deck_single_failed_read_keeps_keys_held);
  RUN_TEST(test_deck_failures_shorter_than_limit_emit_nothing);
  RUN_TEST(test_deck_good_read_restarts_failure_timer);
  RUN_TEST(test_deck_volume_steps_match_reports_sent);
  return UNITY_END();
}
