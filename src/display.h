#pragma once
#include <stdint.h>
#include "keymap.h"
#include "status_board.h"
#include "tile_anim.h"
#include "ui_layout.h"

// ST7789 screen: top bar, 4x3 key legends, status strip. Logic (what changed, label text,
// press animation) lives in deck_core: ui_layout.h, legend.h, tile_anim.h.
void displayBegin();
// anim.tick() runs here, once per drawn frame, so no animation step is lost between frames.
void displayUpdate(const ScreenState &state, const KeyMap &keys, TileAnim &anim, const StatusBoard &status,
                   uint32_t nowMs);
void displaySetDim(uint8_t level);  // 255 full .. 0 backlight off + panel sleep
void displaySetApPassword(const char *p);  // shown in the SAUCEDECK-setup banner
void displaySetWifiInfo(const char *info);  // shown with the Wi-Fi joined / starting banners
