#pragma once
#include <stdint.h>
// Deterministic in the sim (xorshift32), so the AP password is reproducible in tests.
uint32_t esp_random();
