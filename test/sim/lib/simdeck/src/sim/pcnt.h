#pragma once
#include <stdint.h>

// ESP32-S3 PCNT units as the IDF 5 driver exposes them. Plan 1: configuration is stored and
// the count stays where tests put it. Plan 2 adds the edge/level counting model.
struct SimPcntChannel {
  int edgePin = -1, levelPin = -1;
  int posEdge = 0, negEdge = 0;   // pcnt_channel_edge_action_t
  int highLevel = 0, lowLevel = 0;  // pcnt_channel_level_action_t
};

struct SimPcntUnit {
  bool used = false, enabled = false, started = false;
  int low = 0, high = 0, count = 0;
  uint32_t glitchNs = 0;
  SimPcntChannel ch[2];
  int channels = 0;
};

struct SimPcnt {
  static constexpr int kUnits = 4;  // SOC_PCNT_UNITS_PER_GROUP (docs/kb/esp32s3.md)
  SimPcntUnit unit[kUnits];
  bool failNewUnit = false;  // fault: driver refuses a unit -> firmware falls back to polling
};
