#pragma once
#include <stdint.h>
#include "host_link.h"
#include "keymap.h"

// Mac helper link over the USB CDC serial port (framed HelperProto, deck_core/frame.h).
void helperBegin();
void helperPoll(uint32_t nowMs);
const HostLink &helperLink();
void helperSendAction(uint8_t key, const KeyEntry &e);
void helperSendMicDelta(int8_t steps);
void helperSendMicMute();
