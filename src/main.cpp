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
#include "idle_dimmer.h"
#include "keymap_store.h"
#include <esp_system.h>

USBHIDKeyboard Keyboard;
USBHIDConsumerControl Consumer;


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
static bool keymapResetNotice = false;  // blob present but corrupt -> defaults (banner)
static bool brownoutNotice = false;     // last reset was a brownout (banner)
static IdleDimmer dimmer;  // LEDs + LCD fade after 120 s idle, off at 240 s

static void run(const ActionList &actions) {
  for (size_t i = 0; i < actions.count; i++) {
    const Action &a = actions.items[i];
    ledsOnAction(a, millis());
    switch (a.type) {
      case ActionType::KeyDown: {  // keystroke entry: modifiers (HID 0xE0+bit) + one key
        const Keystroke &k = deck.keyMap().entry(a.value).ks;
        for (uint8_t b = 0; b < 4; b++)
          if (k.mods & (1u << b)) Keyboard.pressRaw(0xE0 + b);
        Keyboard.pressRaw(k.usage);
        break;
      }
      case ActionType::KeyUp: {
        const Keystroke &k = deck.keyMap().entry(a.value).ks;
        Keyboard.releaseRaw(k.usage);
        for (uint8_t b = 0; b < 4; b++)
          if (k.mods & (1u << b)) Keyboard.releaseRaw(0xE0 + b);
        break;
      }
      case ActionType::ReleaseAll:
        Keyboard.releaseAll();
        Consumer.release();
        break;
      case ActionType::Consumer:
        Consumer.press(kConsumerCodes[a.value]);
        Consumer.release();
        break;
      case ActionType::Sync: break;  // local only, no HID report
      case ActionType::HelperAction:
      case ActionType::HelperMissing:
      case ActionType::HelperMicMute:
      case ActionType::MicUp:
      case ActionType::MicDown:
      case ActionType::WifiToggle:
      case ActionType::KeyMapDiscarded: break;  // wired in later tasks
      case ActionType::KeyMapCommitted: keymapSave(deck.keyMap()); break;
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
    deck.onReadFailed(millis(), out);
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

  KeyMap stored;
  bool hadMap = keymapExists();
  bool loaded = keymapLoad(stored);
  if (loaded) deck.setKeyMap(stored);
  keymapResetNotice = hadMap && !loaded;
  brownoutNotice = esp_reset_reason() == ESP_RST_BROWNOUT;

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
  if (d1 || d2) deck.onEncoders(d1, d2, millis(), actions);
  run(actions);

  // Any input (still acted on above) or an on-screen prompt keeps the deck awake
  uint32_t now = millis();
  if (actions.count || d1 || d2 || deck.pending() || deck.chordHeldMs(now) > 0) dimmer.onActivity(now);
  uint8_t dim = dimmer.level(now);
  ledsSetDim(dim);
  displaySetDim(dim);

  const VolumeState &spk = deck.speaker();
  displayUpdate({spk.percent(), spk.muted(), deck.micMuted(), false, 0, BANNER_NONE, 0}, millis());
  ledsUpdate({spk.percent(), spk.muted(), deck.micMuted()}, millis());
}
