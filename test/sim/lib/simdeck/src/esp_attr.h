#pragma once
// ESP-IDF esp_attr.h subset. On the chip RTC_NOINIT_ATTR places a variable in RTC slow memory
// that survives panic / watchdog / software resets; the sim forks a fresh process per test,
// so every sim boot sees it zeroed (= power-on state).
#define RTC_NOINIT_ATTR
