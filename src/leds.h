#pragma once
#include <stdint.h>
#include "led_effects.h"

// WS2812 strip glue. Effects + power limit live in deck_core/led_effects.h.
void ledsBegin();
void ledsOnAction(const Action &a, uint32_t nowMs);
void ledsUpdate(const LedInputs &in, uint32_t nowMs);
void ledsSetDim(uint8_t level);
void ledsSetWifi(bool on);
