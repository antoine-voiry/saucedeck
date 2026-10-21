// Spike: vendor-defined HID (usage page 0xFF00) next to keyboard + consumer. Echoes each
// 63-byte output report back as an input report and logs it on Serial.
#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"
#include "USBHIDVendor.h"

USBHIDKeyboard Keyboard;
USBHIDConsumerControl Consumer;
USBHIDVendor Vendor(63, false);

void setup() {
  Keyboard.begin();
  Consumer.begin();
  Vendor.begin();
  USB.begin();
  Serial.begin(115200);
}

void loop() {
  if (Vendor.available() >= 63) {
    uint8_t buf[63];
    size_t n = Vendor.read(buf, sizeof buf);
    Serial.printf("RX %u bytes: %02X %02X %02X %02X\n", (unsigned)n, buf[0], buf[1], buf[2], buf[3]);
    Vendor.write(buf, n);
  }
}
