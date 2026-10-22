#include <unity.h>
#include "wifi_session.h"

void setUp() {}
void tearDown() {}

void test_wifi_starts_off() {
  WifiSession w;
  TEST_ASSERT_TRUE(w.state() == WifiState::Off);
  TEST_ASSERT_FALSE(w.on());
}

void test_wifi_toggle_on_then_states() {
  WifiSession w;
  w.toggle(0);
  TEST_ASSERT_TRUE(w.state() == WifiState::Starting);
  w.onPortal();
  TEST_ASSERT_TRUE(w.state() == WifiState::Portal);
  w.onJoined();
  TEST_ASSERT_TRUE(w.state() == WifiState::Joined);
  TEST_ASSERT_TRUE(w.on());
}

void test_wifi_toggle_again_turns_off() {
  WifiSession w;
  w.toggle(0);
  w.onJoined();
  w.toggle(100);
  TEST_ASSERT_TRUE(w.state() == WifiState::Off);
}

void test_wifi_auto_off_after_10_min_once() {
  WifiSession w;
  w.toggle(1000);
  w.onJoined();
  TEST_ASSERT_FALSE(w.tick(1000 + WifiSession::kOnMs - 1));
  TEST_ASSERT_TRUE(w.tick(1000 + WifiSession::kOnMs));
  TEST_ASSERT_TRUE(w.state() == WifiState::Off);
  TEST_ASSERT_FALSE(w.tick(1000 + WifiSession::kOnMs + 1));
}

void test_wifi_failed_goes_off() {
  WifiSession w;
  w.toggle(0);
  w.onFailed();
  TEST_ASSERT_FALSE(w.on());
}

void test_wifi_events_ignored_when_off() {
  WifiSession w;
  w.onJoined();
  w.onPortal();
  TEST_ASSERT_TRUE(w.state() == WifiState::Off);
}

void test_wifi_lost_link_goes_back_to_starting() {
  // Bug 2026-10-22: banner said "joined" after the link dropped
  WifiSession w;
  w.toggle(0);
  w.onJoined();
  w.onLost();
  TEST_ASSERT_TRUE(w.state() == WifiState::Starting);
  TEST_ASSERT_TRUE(w.on());
  w.onJoined();
  TEST_ASSERT_TRUE(w.state() == WifiState::Joined);
}

void test_wifi_lost_ignored_unless_joined() {
  WifiSession w;
  w.onLost();
  TEST_ASSERT_TRUE(w.state() == WifiState::Off);
  w.toggle(0);
  w.onPortal();
  w.onLost();
  TEST_ASSERT_TRUE(w.state() == WifiState::Portal);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_wifi_starts_off);
  RUN_TEST(test_wifi_toggle_on_then_states);
  RUN_TEST(test_wifi_toggle_again_turns_off);
  RUN_TEST(test_wifi_auto_off_after_10_min_once);
  RUN_TEST(test_wifi_failed_goes_off);
  RUN_TEST(test_wifi_events_ignored_when_off);
  RUN_TEST(test_wifi_lost_link_goes_back_to_starting);
  RUN_TEST(test_wifi_lost_ignored_unless_joined);
  return UNITY_END();
}
