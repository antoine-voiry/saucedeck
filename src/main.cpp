#include <Arduino.h>
#include <Wire.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"
#include "pins.h"
#include "button_diff.h"
#include "debounce.h"
#include "knob_chord.h"
#include "mcp_io.h"
#include "quad_decoder.h"
#include "volume_state.h"

USBHIDKeyboard Keyboard;
USBHIDConsumerControl Consumer;

// Deck buttons 1-12 -> F13-F24 (unused by OSes, easy to bind in any app)
static const uint8_t kButtonKeys[12] = {
    KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17, KEY_F18,
    KEY_F19, KEY_F20, KEY_F21, KEY_F22, KEY_F23, KEY_F24,
};

static uint16_t lastPressed = 0;
static MaskDebouncer debouncer;  // 25 ms settle, matches upstream SauceDeck
static bool mcpOk = false;
static KnobChord knobs(DECK_ENC1_SW, DECK_ENC2_SW);
static VolumeState speaker(16);  // macOS: 16 VOL+/- steps full range (unverified)

static void scanButtons() {
  uint16_t raw;
  if (!mcpOk) mcpOk = mcpBegin();  // retry until the expander answers
  if (!mcpOk || !mcpReadRaw(raw)) { mcpOk = false; return; }

  uint16_t now = debouncer.update(deckPressed(raw), millis());
  ButtonEvents ev = diffButtons(~lastPressed, ~now);  // diffButtons expects active-low
  lastPressed = now;

  for (int i = 0; i < 12; i++) {
    uint16_t bit = 1u << i;
    if (ev.pressed & bit)  Keyboard.press(kButtonKeys[i]);
    if (ev.released & bit) Keyboard.release(kButtonKeys[i]);
  }
  // Knob switches (fire on release): mute (knob 1), play/pause (knob 2); both = sync
  uint8_t chord = knobs.update(now);
  if (chord & CHORD_KNOB1) {
    Consumer.press(CONSUMER_CONTROL_MUTE);
    Consumer.release();
    speaker.toggleMute();
  }
  if (chord & CHORD_KNOB2) { Consumer.press(CONSUMER_CONTROL_PLAY_PAUSE); Consumer.release(); }
  if (chord & CHORD_SYNC) speaker.sync();  // user has set the host to 0 % first
}

static QuadDecoder enc1(1, 1), enc2(1, 1);

void setup() {
  // USB strings: see build_flags (USB already started before setup())
  Keyboard.begin();
  Consumer.begin();
  USB.begin();
  Serial.begin(115200);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  mcpOk = mcpBegin();

  for (uint8_t pin : {ENC1_PHASE_A_PIN, ENC1_PHASE_B_PIN, ENC2_PHASE_A_PIN, ENC2_PHASE_B_PIN})
    pinMode(pin, INPUT_PULLUP);
  enc1 = QuadDecoder(digitalRead(ENC1_PHASE_A_PIN), digitalRead(ENC1_PHASE_B_PIN));
  enc2 = QuadDecoder(digitalRead(ENC2_PHASE_A_PIN), digitalRead(ENC2_PHASE_B_PIN));
}

void loop() {
  scanButtons();

  // Encoder 1 -> volume, encoder 2 -> next/previous track
  int d1 = enc1.step(digitalRead(ENC1_PHASE_A_PIN), digitalRead(ENC1_PHASE_B_PIN));
  if (d1) {
    speaker.rotate(d1);
    Consumer.press(d1 > 0 ? CONSUMER_CONTROL_VOLUME_INCREMENT : CONSUMER_CONTROL_VOLUME_DECREMENT);
    Consumer.release();
  }
  int d2 = enc2.step(digitalRead(ENC2_PHASE_A_PIN), digitalRead(ENC2_PHASE_B_PIN));
  if (d2) {
    Consumer.press(d2 > 0 ? CONSUMER_CONTROL_SCAN_NEXT : CONSUMER_CONTROL_SCAN_PREVIOUS);
    Consumer.release();
  }
}
