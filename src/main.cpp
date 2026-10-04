#include <Arduino.h>
#include <Wire.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"
#include "pins.h"
#include "button_diff.h"
#include "quad_decoder.h"

USBHIDKeyboard Keyboard;
USBHIDConsumerControl Consumer;

// Deck buttons 1-12 -> F13-F24 (unused by OSes, easy to bind in any app)
static const uint8_t kButtonKeys[12] = {
    KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17, KEY_F18,
    KEY_F19, KEY_F20, KEY_F21, KEY_F22, KEY_F23, KEY_F24,
};

static uint16_t lastPcf = 0xFFFF;

static uint16_t readPcf() {
  if (Wire.requestFrom((uint8_t)PCF_I2C_ADDR, (uint8_t)2) != 2) return lastPcf;
  uint16_t lo = Wire.read();
  uint16_t hi = Wire.read();
  return (hi << 8) | lo;
}

static void pollButtons() {
  uint16_t now = readPcf();
  ButtonEvents ev = diffButtons(lastPcf, now);
  lastPcf = now;

  for (int i = 0; i < 12; i++) {
    uint16_t bit = 1u << i;
    if (ev.pressed & bit)  Keyboard.press(kButtonKeys[i]);
    if (ev.released & bit) Keyboard.release(kButtonKeys[i]);
  }
  // Encoder push switches: mute (enc 1), play/pause (enc 2)
  if (ev.pressed & PCF_MASK_ENC1_SW) { Consumer.press(CONSUMER_CONTROL_MUTE); Consumer.release(); }
  if (ev.pressed & PCF_MASK_ENC2_SW) { Consumer.press(CONSUMER_CONTROL_PLAY_PAUSE); Consumer.release(); }
}

static QuadDecoder enc1(1, 1), enc2(1, 1);

void setup() {
  USB.manufacturerName("SAUCEDECK");
  USB.productName("SAUCEDECK");
  Keyboard.begin();
  Consumer.begin();
  USB.begin();
  Serial.begin(115200);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  Wire.beginTransmission(PCF_I2C_ADDR);  // all pins high = inputs
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.endTransmission();
  pinMode(PCF_INT_PIN, INPUT_PULLUP);

  for (uint8_t pin : {ENC1_PHASE_A_PIN, ENC1_PHASE_B_PIN, ENC2_PHASE_A_PIN, ENC2_PHASE_B_PIN})
    pinMode(pin, INPUT_PULLUP);
  enc1 = QuadDecoder(digitalRead(ENC1_PHASE_A_PIN), digitalRead(ENC1_PHASE_B_PIN));
  enc2 = QuadDecoder(digitalRead(ENC2_PHASE_A_PIN), digitalRead(ENC2_PHASE_B_PIN));
}

void loop() {
  if (digitalRead(PCF_INT_PIN) == LOW) pollButtons();

  // Encoder 1 -> volume, encoder 2 -> next/previous track
  int d1 = enc1.step(digitalRead(ENC1_PHASE_A_PIN), digitalRead(ENC1_PHASE_B_PIN));
  if (d1) {
    Consumer.press(d1 > 0 ? CONSUMER_CONTROL_VOLUME_INCREMENT : CONSUMER_CONTROL_VOLUME_DECREMENT);
    Consumer.release();
  }
  int d2 = enc2.step(digitalRead(ENC2_PHASE_A_PIN), digitalRead(ENC2_PHASE_B_PIN));
  if (d2) {
    Consumer.press(d2 > 0 ? CONSUMER_CONTROL_SCAN_NEXT : CONSUMER_CONTROL_SCAN_PREVIOUS);
    Consumer.release();
  }
}
