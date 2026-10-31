#pragma once
#include <stdint.h>
#include "deck_clock.h"
#include "host_link.h"
#include "keymap.h"
#include "status_board.h"

// Mac helper link over the USB CDC serial port (framed HelperProto, deck_core/frame.h).
void helperBegin();
void helperPoll(uint32_t nowMs);
const HostLink &helperLink();
const DeckClock &helperClock();  // last TIME from the helper (display only)
StatusBoard &helperStatus();     // CARD slots (display only); main ticks and clears it
void helperSendAction(uint8_t key, const KeyEntry &e);
void helperSendMicDelta(int8_t steps);
void helperSendMicMute();
