#include <Arduino.h>
#include <Wire.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"
#include "pins.h"
#include "deck.h"
#include "display.h"
#include "leds.h"
#include "mcp_io.h"
#include "encoders.h"

USBHIDKeyboard Keyboard;
USBHIDConsumerControl Consumer;

// Deck buttons 1-12 -> F13-F24 (unused by OSes, easy to bind in any app)
static const uint8_t kButtonKeys[12] = {
    KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17, KEY_F18,
    KEY_F19, KEY_F20, KEY_F21, KEY_F22, KEY_F23, KEY_F24,
};

// Knob 2 push: mic mute toggle. Cmd+Shift+M = Teams on macOS (unverified); change here.
static const uint8_t kMicMuteChord[] = {KEY_LEFT_GUI, KEY_LEFT_SHIFT, 'm'};

static const uint16_t kConsumerCodes[] = {
    CONSUMER_CONTROL_MUTE,            // ConsumerCode::Mute
    CONSUMER_CONTROL_VOLUME_INCREMENT,  // ConsumerCode::VolumeUp
    CONSUMER_CONTROL_VOLUME_DECREMENT,  // ConsumerCode::VolumeDown
    CONSUMER_CONTROL_SCAN_NEXT,       // ConsumerCode::NextTrack
    CONSUMER_CONTROL_SCAN_PREVIOUS,   // ConsumerCode::PrevTrack
};
static_assert(sizeof(kConsumerCodes) / sizeof(kConsumerCodes[0]) ==
                  static_cast<size_t>(ConsumerCode::PrevTrack) + 1,
              "kConsumerCodes must cover every ConsumerCode");

static Deck deck(16);  // macOS: 16 VOL+/- steps full range (unverified)
static bool mcpOk = false;

static void run(const ActionList &actions) {
  for (size_t i = 0; i < actions.count; i++) {
    const Action &a = actions.items[i];
    ledsOnAction(a, millis());
    switch (a.type) {
      case ActionType::KeyDown: Keyboard.press(kButtonKeys[a.value]); break;
      case ActionType::KeyUp: Keyboard.release(kButtonKeys[a.value]); break;
      case ActionType::ReleaseAll:
        Keyboard.releaseAll();
        Consumer.release();
        break;
      case ActionType::Consumer:
        Consumer.press(kConsumerCodes[a.value]);
        Consumer.release();
        break;
      case ActionType::Sync: break;  // local only, no HID report
      case ActionType::MicMute:
        // Release only the chord keys: deck F-keys may still be held
        for (uint8_t k : kMicMuteChord) Keyboard.press(k);
        for (uint8_t k : kMicMuteChord) Keyboard.release(k);
        break;
    }
  }
}

static void scanButtons(ActionList &out) {
  uint16_t raw;
  if (!mcpOk) mcpOk = mcpBegin();  // retry until the expander answers
  if (!mcpOk || !mcpReadRaw(raw)) {
    mcpOk = false;
    deck.onBusLost(out);
    return;
  }
  deck.onKeys(deckPressed(raw), millis(), out);
}

void setup() {
  // USB strings: see build_flags (USB already started before setup())
  Keyboard.begin();
  Consumer.begin();
  USB.begin();
  Serial.begin(115200);

  displayBegin();
  ledsBegin();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  mcpOk = mcpBegin();

  encodersBegin();
}

void loop() {
  ActionList actions;
  scanButtons(actions);
  // Knob 1 -> volume, knob 2 -> next/previous track
  int d1, d2;
  encodersRead(d1, d2);
  if (d1 || d2) deck.onEncoders(d1, d2, actions);
  run(actions);

  const VolumeState &spk = deck.speaker();
  displayUpdate({spk.percent(), spk.muted(), deck.micMuted()}, millis());
  ledsUpdate({spk.percent(), spk.muted(), deck.micMuted()}, millis());
}
