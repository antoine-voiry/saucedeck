#pragma once
#include <stdint.h>
#include "ui_layout.h"

// ST7789 volume screen. Logic (what changed, bar strips) lives in deck_core/ui_layout.h.
void displayBegin();
void displayUpdate(const ScreenState &state, uint32_t nowMs);
void displaySetDim(uint8_t level);  // 255 full .. 0 backlight off + panel sleep
void displaySetApPassword(const char *p);  // shown in the SAUCEDECK-setup banner
