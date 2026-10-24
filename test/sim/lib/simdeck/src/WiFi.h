#pragma once
#include <stdint.h>
#include "Arduino.h"

typedef enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 } wifi_mode_t;
typedef enum { WIFI_POWER_11dBm = 44, WIFI_POWER_19_5dBm = 78 } wifi_power_t;
typedef enum {
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_DISCONNECTED = 6,
} wl_status_t;

class IPAddress {
 public:
  explicit IPAddress(const uint8_t o[4]) {
    for (int i = 0; i < 4; i++) o_[i] = o[i];
  }
  String toString() const;

 private:
  uint8_t o_[4];
};

class WiFiClass {
 public:
  bool mode(wifi_mode_t m);
  bool setTxPower(wifi_power_t p);
  bool disconnect(bool wifiOff = false);
  wl_status_t status();
  IPAddress localIP();
  int8_t RSSI();
};
extern WiFiClass WiFi;
