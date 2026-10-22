#pragma once
#include <stddef.h>
#include <stdint.h>
#include "keymap.h"
#include "wifi_session.h"

// WiFiManager settings portal. The ONLY Wi-Fi code in the firmware (CI: tools/check_radio_scope.sh).
bool portalStart(const KeyMap &current);  // false = could not start (Wi-Fi failed)
void portalStop();
void portalPoll();
WifiState portalState();
bool portalTakeStaged(KeyMap &out, uint8_t &badKey);  // badKey = 1..12 if a field was invalid, 0 = ok
const char *portalApPassword();
void portalInfo(char *out, size_t n);  // joined: "<ip> <rssi>dBm"; else "status <WiFi.status()>"
