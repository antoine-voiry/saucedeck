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
#include "tile_anim.h"
#include "helper_link.h"
#include "keymap_store.h"
#include "wifi_portal.h"
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
static uint32_t helperMissingUntil = 0;
static WifiSession wifi;
static uint8_t wifiInfoTick = 0;  // forces a banner redraw when the info text changes
static uint8_t invalidKey = 0;
static uint32_t invalidUntil = 0, wifiFailedUntil = 0;  // "helper not running" banner end
static IdleDimmer dimmer;  // LEDs + LCD fade after 120 s idle, off at 240 s
static TileAnim tileAnim;    // legend tile press feedback
static uint8_t keymapGen = 0;  // bumped when a key map goes live: legends repaint

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
      case ActionType::HelperAction: helperSendAction(a.value, deck.keyMap().entry(a.value)); break;
      case ActionType::HelperMicMute: helperSendMicMute(); break;
      case ActionType::MicUp: helperSendMicDelta(1); break;
      case ActionType::MicDown: helperSendMicDelta(-1); break;
      case ActionType::HelperMissing: helperMissingUntil = millis() + 2000; break;
      case ActionType::WifiToggle:
        wifi.toggle(millis());
        if (wifi.on()) {
          if (portalStart(deck.keyMap())) {
            displaySetApPassword(portalApPassword());
          } else {
            wifi.onFailed();
            wifiFailedUntil = millis() + 4000;
          }
        } else {
          portalStop();
        }
        ledsSetWifi(wifi.on());
        break;
      case ActionType::KeyMapDiscarded: break;  // banner simply disappears
      case ActionType::KeyMapCommitted:
        keymapSave(deck.keyMap());
        keymapGen++;
        break;
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
  helperBegin();

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
  uint32_t now = millis();
  helperPoll(now);
  const HostLink &hl = helperLink();
  deck.setHelperPresent(hl.present(now));

  ActionList actions;
  scanButtons(actions);
  // Knob 1 -> volume, knob 2 -> next/previous track
  int d1, d2;
  encodersRead(d1, d2);
  if (d1 || d2) deck.onEncoders(d1, d2, millis(), actions);
  run(actions);
  tileAnim.onKeys(deck.keysHeld(), helperMissingKeys(actions), now);

  now = millis();
  portalPoll();
  if (wifi.on()) {
    WifiState ps = portalState();
    if (ps == WifiState::Joined) wifi.onJoined();
    else if (ps == WifiState::Portal) wifi.onPortal();
    else if (ps == WifiState::Starting) wifi.onLost();
    static uint32_t lastInfo = 0;
    if (now - lastInfo >= 1000) {  // IP / RSSI / status for the banner, once a second
      lastInfo = now;
      char info[28];
      portalInfo(info, sizeof info);
      displaySetWifiInfo(info);
      wifiInfoTick++;
    }
  }
  if (wifi.tick(now)) {  // 10 min auto-off
    portalStop();
    ledsSetWifi(false);
  }
  KeyMap staged;
  uint8_t bad = 0;
  if (portalTakeStaged(staged, bad)) {
    if (bad) {
      invalidKey = bad;
      invalidUntil = now + 4000;
    } else {
      deck.stage(staged, now);  // goes live only after a knob 1 press
    }
  }

  // Any input (still acted on above) or an on-screen prompt keeps the deck awake
  if (actions.count || d1 || d2 || deck.pending() || wifi.on() || deck.chordHeldMs(now) > 0)
    dimmer.onActivity(now);
  uint8_t dim = dimmer.level(now);
  ledsSetDim(dim);
  displaySetDim(dim);

  StatusBoard &status = helperStatus();
  if (!hl.present(now)) status.clear();  // cards are only true while the helper runs
  status.tick(now);
  // Helper connected: show the Mac's real volume / mic; otherwise the deck's own count
  const VolumeState &spk = deck.speaker();
  bool live = hl.present(now) && hl.hasVolume();
  uint8_t pct = live ? hl.volumePercent() : spk.percent();
  bool spkMuted = live ? hl.volumeMuted() : spk.muted();
  bool micKnown = hl.present(now) && hl.hasMic();
  bool micMuted = micKnown ? hl.micMuted() : deck.micMuted();
  uint8_t micPct = micKnown ? hl.micPercent() : 0;
  uint8_t banner = BANNER_NONE, arg = 0;
  uint32_t held = deck.chordHeldMs(now);
  if (deck.pending()) {
    banner = BANNER_APPLY;
    arg = static_cast<uint8_t>((deck.pendingMsLeft(now) + 999) / 1000);
  } else if (held > 500 && held < KnobChord::kWifiHoldMs) {
    banner = BANNER_WIFI_COUNTDOWN;
    arg = static_cast<uint8_t>((KnobChord::kWifiHoldMs - held + 999) / 1000);
  } else if (static_cast<int32_t>(invalidUntil - now) > 0) {
    banner = BANNER_INVALID_KEY;
    arg = invalidKey;
  } else if (static_cast<int32_t>(helperMissingUntil - now) > 0) {
    banner = BANNER_HELPER_MISSING;
  } else if (static_cast<int32_t>(wifiFailedUntil - now) > 0) {
    banner = BANNER_WIFI_FAILED;
  } else if (wifi.state() == WifiState::Portal) {
    banner = BANNER_WIFI_PORTAL;
  } else if (wifi.state() == WifiState::Joined) {
    banner = BANNER_WIFI_JOINED;
    arg = wifiInfoTick;
  } else if (wifi.state() == WifiState::Starting) {
    banner = BANNER_WIFI_STARTING;
    arg = wifiInfoTick;
  } else if (keymapResetNotice && now < 5000) {
    banner = BANNER_KEYMAP_RESET;
  } else if (brownoutNotice && now < 8000) {
    banner = BANNER_BROWNOUT;
  }
  displayUpdate({pct, spkMuted, micMuted, micKnown, micPct, banner, arg, keymapGen, hl.present(now), wifi.on(),
                 helperClock().minuteOfDay(now), status.gen()},
                deck.keyMap(), tileAnim, status, now);
  ledsUpdate({pct, spkMuted, micMuted}, now);
}
