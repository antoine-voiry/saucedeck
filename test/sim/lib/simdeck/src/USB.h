#pragma once
#include <stdint.h>
#include "sim_board.h"

typedef enum {
  ARDUINO_USB_STARTED_EVENT,
  ARDUINO_USB_STOPPED_EVENT,
  ARDUINO_USB_SUSPEND_EVENT,
  ARDUINO_USB_RESUME_EVENT,
} arduino_usb_event_t;

class ESPUSB {
 public:
  bool begin();
  void onEvent(arduino_usb_event_t event, sim_usb_handler_t handler);
};
extern ESPUSB USB;
