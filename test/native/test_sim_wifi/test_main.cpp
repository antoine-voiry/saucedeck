// Wi-Fi scenarios: the real src/ portal glue against the sim WiFiManager (2.0.17 server lifetime).
#include <unity.h>
#include <WiFi.h>
#include "knob_chord.h"
#include "sim_deck.h"
#include "sim_test.h"
#include "wifi_session.h"

void setUp() {}
void tearDown() {}

static constexpr uint32_t kAfterSplash = 1600;  // splash 1500 ms (src/display.cpp)
static constexpr uint32_t kSettle = 30;         // > 25 ms debounce

// Both knobs held past the Wi-Fi hold time: toggles Wi-Fi.
static void chord(SimDeck &sim) {
  sim.knobPress(1);
  sim.knobPress(2);
  sim.run(KnobChord::kWifiHoldMs + 200);
  sim.knobRelease(1);
  sim.knobRelease(2);
  sim.run(kSettle);
}

// Saved credentials work: autoConnect joins, then portalPoll starts the web portal (/param).
static void joinHomeWifi(SimDeck &sim) {
  board().wifi.joinOk = true;
  sim.boot();
  sim.run(kAfterSplash);
  chord(sim);
  sim.run(100);
  TEST_ASSERT_TRUE(board().wifi.connected);
  TEST_ASSERT_TRUE(board().wifi.webPortal);
}

// Bench 2026-11-08: Wi-Fi off while joined crashed the deck (null server in
// WiFiManager::shutdownConfigPortal, core dump). Knob toggle path.
SIM_TEST(test_wifi_off_by_knobs_while_joined_does_not_crash) {
  SimDeck sim;
  joinHomeWifi(sim);
  chord(sim);
  sim.run(100);
  TEST_ASSERT_FALSE(board().wifi.webPortal);
  TEST_ASSERT_FALSE(board().wifi.serverUp);
  TEST_ASSERT_EQUAL(WIFI_OFF, board().wifi.mode);
}

// Same crash through the 10 min auto-off.
SIM_TEST(test_wifi_auto_off_while_joined_does_not_crash) {
  SimDeck sim;
  joinHomeWifi(sim);
  sim.run(WifiSession::kOnMs + 1000);
  TEST_ASSERT_FALSE(board().wifi.webPortal);
  TEST_ASSERT_EQUAL(WIFI_OFF, board().wifi.mode);
}

// Joined, but switched off before portalPoll started the web portal (server never allocated).
SIM_TEST(test_wifi_off_right_after_join_does_not_crash) {
  board().wifi.joinOk = true;
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  sim.knobPress(1);
  sim.knobPress(2);
  sim.run(KnobChord::kWifiHoldMs + 200);  // portalStart runs here; release right away
  sim.knobRelease(1);
  sim.knobRelease(2);
  board().wifi.connected = false;  // link not up yet: no web portal
  sim.run(kSettle);
  chord(sim);
  sim.run(100);
  TEST_ASSERT_EQUAL(WIFI_OFF, board().wifi.mode);
}

// AP portal (no saved network): off by knobs stops the AP.
SIM_TEST(test_wifi_off_from_ap_portal_stops_ap) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  chord(sim);
  TEST_ASSERT_TRUE(board().wifi.portalActive);
  chord(sim);
  sim.run(100);
  TEST_ASSERT_FALSE(board().wifi.portalActive);
  TEST_ASSERT_FALSE(board().wifi.serverUp);
  TEST_ASSERT_EQUAL(WIFI_OFF, board().wifi.mode);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_wifi_off_by_knobs_while_joined_does_not_crash);
  RUN_TEST(test_wifi_auto_off_while_joined_does_not_crash);
  RUN_TEST(test_wifi_off_right_after_join_does_not_crash);
  RUN_TEST(test_wifi_off_from_ap_portal_stops_ap);
  return UNITY_END();
}
