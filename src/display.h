#pragma once
#include <stdint.h>
#include "ui_layout.h"

// ST7789 volume screen. Logic (what changed, bar strips) lives in deck_core/ui_layout.h.
void displayBegin();
void displayUpdate(const ScreenState &state, uint32_t nowMs);
