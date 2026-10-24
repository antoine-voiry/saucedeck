#pragma once
#include <stdint.h>
#include <string>

class WiFiManager;

// Wi-Fi radio + WiFiManager as the firmware sees them. Behaviour knobs are set by tests
// (plan 3 adds form submit and link drops).
struct SimWifi {
  bool joinOk = false;     // saved credentials work
  bool portalOk = true;    // SAUCEDECK-setup AP comes up
  bool connected = false;
  bool portalActive = false;
  bool webPortal = false;
  int mode = 0;            // wifi_mode_t
  int txPower = 0;
  int8_t rssi = -55;
  uint8_t ip[4] = {192, 168, 1, 50};
  std::string mdnsName;
  std::string apSsid, apPass;
  WiFiManager *manager = nullptr;
};
