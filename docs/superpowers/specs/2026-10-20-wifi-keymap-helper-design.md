# SAUCEDECK: configurable keys, Wi-Fi settings portal, Mac helper, volume sync, mic level

Status: design approved in brainstorming 2026-10-20, pending spec review.
Spike to confirm: WiFiManager serves the same custom-parameter page in station mode
(`startWebPortal`) as in hotspot mode.
Scope: firmware (ESP32-S3) + a macOS helper app. Windows support is out of scope (own spec later).

## 1. Goals

1. Each of the 12 keys can be set to one of: send a keystroke, open an app, open a URL, run a macOS Shortcut.
2. The key map is edited from a browser, served by the deck itself over Wi-Fi.
3. The deck shows the Mac's real output volume and mute (no more drift).
4. Knob 2 adjusts the Mac's real microphone level and mute, and the deck shows them.
5. Everything that worked before keeps working without the helper (fallback).

Non-goals: Windows, OTA firmware updates, page login, encrypted storage, cloud anything.

## 2. Decisions (with owner, 2026-10-20)

| Topic | Decision |
| --- | --- |
| Settings UI | WiFiManager (tzapu) portal page: Wi-Fi join **and** key-map form (custom parameters) |
| Wi-Fi activation | Hold both knobs ≥ 5 s. Off by default. Auto-off after 10 min or another 5 s hold |
| Wi-Fi mode | Join home network (`http://saucedeck.local`); if no saved network or join fails in 20 s, open hotspot `SAUCEDECK-setup` (WPA2, password shown on LCD) |
| Wi-Fi password | Stored by WiFiManager / ESP-IDF in **plain flash**. Accepted risk: anyone with the deck and USB can read it |
| Page protection | No login. Every save is staged and applied only after a knob 1 press on the deck (30 s window) |
| Key map location | On the deck (NVS), so it travels with the deck |
| Mac helper | Swift menu-bar app, talks to the deck over a private (vendor-defined) USB HID interface |
| LEDs while Wi-Fi on | Cap 30 mA, action flashes off, one LED pulses blue as Wi-Fi indicator |
| Knob 2 | Turn = next/prev track. Push + turn = mic level (helper). Push + release without turn = mic mute |
| Volume | Knob 1 keeps sending consumer VOL+/VOL-; helper reports the real level back for display |

## 3. Architecture

```
 SAUCEDECK (ESP32-S3)                                   Mac
 ┌──────────────────────────────────────────┐   USB   ┌──────────────────────┐
 │ deck_core (pure C++17, host-tested)      │ HID kbd │ SauceDeck helper     │
 │  KeyMap ─ NVS                            │ HID cons│  (Swift, menu bar)   │
 │  KnobChord v2, KnobGesture               │◄───────►│  open app / URL /    │
 │  WifiSession, PendingChange              │ vendor  │  Shortcut            │
 │  HelperProto, HostState                  │ HID     │  CoreAudio: out/in   │
 │  Deck (actions), LedEffects (wifi mode)  │         │  volume + mute       │
 │ src/ glue                                │         └──────────────────────┘
 │  wifi_portal.cpp (only file with radio)  │
 │  helper_hid.cpp, display, leds, encoders │
 └──────────────────────────────────────────┘
        ▲ Wi-Fi only after 5 s two-knob hold
        └── phone / laptop browser → WiFiManager page
```

USB composite device: keyboard + consumer control (existing) + one vendor-defined HID
interface (usage page 0xFF00, 64-byte in/out reports) for the helper. Browsers and the
helper never use the keyboard interface for data.

## 4. Components (deck_core, all unit-tested)

### 4.1 KeyMap
- 12 entries `{type, value}`; `type` ∈ `Keystroke`, `App`, `Url`, `Shortcut`.
- Default: every key `Keystroke` F13–F24 (today's behaviour).
- `value`: 1–95 printable ASCII characters (0x20–0x7E).
- `Keystroke` value grammar: `mod+mod+key`, mods ∈ `cmd ctrl alt shift`, key = one of `a`–`z`,
  `0`–`9`, `f1`–`f24`, `space enter tab esc`. Exactly one non-modifier key. Anything else rejected.
- `Url` must start with `http://` or `https://`.
- Serialization: versioned blob `{magic, version, 12 × entry, crc32}` in NVS. Bad magic, version
  or CRC → defaults, LCD shows "Key map reset".

### 4.2 KnobChord v2 (both knob switches)
- Both held, released before 5000 ms → `SYNC` (fired on release).
- Both held 5000 ms → `WIFI` fired once while still held; the following release fires nothing.
- While both are held the LCD shows a 5 s countdown.
- Single-knob behaviour unchanged: fires on release unless the chord happened.

### 4.3 KnobGesture (knob 2 push + turn)
- Knob 2 held ≥ 150 ms and turned → each detent = `MIC_DELTA ±1`; the release then fires nothing.
- Knob 2 pressed and released with no qualifying turn → `MIC_MUTE` (as today).
- Detents within the first 150 ms of the hold are ignored (shaft nudge when pressing). Bench-tune.
- Knob 2 turned while not held → next/prev track (unchanged).
- Knob 1 turned while knob 2 is held → volume as usual; it does not count as a knob 2 turn.

### 4.4 WifiSession
- States: `Off → Starting → Portal | Joined → Off`.
- `Off` after 10 min in any on-state, after another `WIFI` gesture, or on start failure.
- Exposes a status for LCD and LEDs.

### 4.5 PendingChange
- Portal save → validated `KeyMap` staged; invalid fields are reported back on the page, nothing staged.
- LCD: "Apply changes? press knob 1" with a 30 s countdown.
- Knob 1 press in the window → commit to NVS and make it live. While pending, knob 1 = confirm (not mute).
- Timeout, or a new save → previous staged map discarded.

### 4.6 HelperProto (vendor HID messages, 64-byte reports)
Header: `ver (1) | type | len`, payload ≤ 61 bytes. Parser rejects bad version, unknown type,
`len` > 61, out-of-range values. Fuzz-style tests on random input.

| Dir | Type | Payload |
| --- | --- | --- |
| Mac → deck | `HELLO` | helper version |
| Mac → deck | `VOLUME` | out percent 0–100, out muted 0/1 |
| Mac → deck | `MIC` | in percent 0–100, in muted 0/1 |
| deck → Mac | `ACTION` | key 0–11, type (`App`/`Url`/`Shortcut`), value |
| deck → Mac | `MIC_DELTA` | steps −8…+8 |
| deck → Mac | `MIC_MUTE` | (toggle) |

Helper presence = a `HELLO` received since USB mount; lost on USB unmount or 3 s without any
host message (helper sends `HELLO` every second as keep-alive).

### 4.7 HostState
- Holds the last `VOLUME` and `MIC` from the helper.
- Display source: helper present → `HostState`; absent → existing open-loop `VolumeState`
  (sync chord still re-aligns it).

### 4.8 Deck / action routing
- Key press, entry `Keystroke` → HID keyboard report (deck sends it itself, no helper needed).
- Key press, entry `App`/`Url`/`Shortcut` → `ACTION` message if helper present; otherwise
  orange flash + LCD "helper not running" for 2 s, nothing sent.
- Knob 2 push (no turn): helper present → `MIC_MUTE`; absent → Cmd+Shift+M chord (today).
- Knob 2 push + turn: helper present → `MIC_DELTA`; absent → LCD "helper not running".
- The deck never types a stored value as text.

### 4.9 LedEffects (Wi-Fi mode)
- Wi-Fi on: frame cap 30 mA, action flashes and gauge off, LED 0 pulses blue (~1 Hz).
- Wi-Fi off: unchanged (250 mA cap).

## 5. src/ glue
- `wifi_portal.cpp`: WiFiManager setup, custom parameters (12 × type + value), save callback →
  `PendingChange`. Only file allowed to include Wi-Fi headers.
- `helper_hid.cpp`: TinyUSB vendor HID interface, feeds/drains `HelperProto`.
- `display.cpp`: adds Wi-Fi status, countdown, "Apply?", "helper not running", "Key map reset",
  mic level bar when helper present.
- CI radio check changes from "no radio linked" to "radio symbols only from `wifi_portal.cpp`
  objects" (check `.map` file per object).

## 6. Mac helper (Swift)
- Menu-bar icon: connected / not connected; menu: last actions, quit.
- Opens the vendor HID device (VID/PID of SAUCEDECK, usage page 0xFF00) via IOHIDManager.
- `ACTION` → `App`: `open -a <value>`; `Url`: `open <value>`; `Shortcut`: `shortcuts run <value>`.
  Spawned with an argument array (`Process`), never through a shell. Logs every action.
- CoreAudio: listens to default output and input device volume + mute; sends `VOLUME` / `MIC`
  on change and on connect. Applies `MIC_DELTA` (±5 % per step) and `MIC_MUTE` to the default
  input device.
- Rejects any message it doesn't know. Never executes anything except the three action types.

## 7. Data flows

**Edit keys:** 5 s two-knob hold → Wi-Fi on → join or hotspot → page → Save → validate →
staged → LCD "Apply?" → knob 1 within 30 s → NVS + live → Wi-Fi off after 10 min / 5 s hold.

**Key press:** see 4.8. Deck flashes the key colour when it sends, orange when it can't.

**Volume:** knob 1 → consumer VOL± → macOS changes volume → helper sees change → `VOLUME` →
LCD/LEDs show real level.

**Mic:** knob 2 push+turn → `MIC_DELTA` → helper sets input volume → `MIC` → LCD shows it.

## 8. Error handling
- NVS map corrupt → defaults + LCD notice.
- Wi-Fi join fails → hotspot fallback; WiFiManager start fails → Wi-Fi off, LCD "Wi-Fi failed".
- Brownout reset → reset reason stored, shown once on LCD after reboot.
- Expander bus loss → unchanged (release all), Wi-Fi unaffected.
- Malformed helper message → dropped (both sides), counted, logged.

## 9. Power and hardware
- ESP32-S3 Wi-Fi TX peaks ~300 mA+ `[unverified: check S3 datasheet current table]`.
- Budget with Wi-Fi on: LEDs ≤ 30 mA, Wi-Fi TX power limited (`WiFi.setTxPower`, start at
  ~11 dBm, bench-tune), backlight unchanged.
- Bench: watch for brownout with Wi-Fi on; measure USB current if possible.
- SuperMini antenna performance unverified; check join range on the bench.

## 10. Security
- Accepted risks (owner): Wi-Fi password in plain flash; settings page has no login on the LAN.
- Vendor HID channel: any local process could open it. Mac → deck messages (`HELLO`, `VOLUME`,
  `MIC`) only change what the deck displays; they can't trigger keys or actions. Actions only
  flow deck → Mac, and only from a physical key press.
- Mitigations: Wi-Fi off by default and time-limited; every map change needs a physical press
  on the deck; strict value validation; deck never types stored text; helper runs only 3 action
  types without a shell; parsers fuzz-tested; radio code confined to one file (CI-checked).
- SECURITY.md baseline and `docs/plans/2026-10-18-upstream-port.md` security table updated to
  reflect that Wi-Fi now exists.

## 11. Testing
- Host unit tests (TDD): KeyMap (grammar, validation, serialize, CRC), KnobChord v2 timings,
  KnobGesture (150 ms rule, release suppression), WifiSession, PendingChange (confirm, timeout,
  replace), HelperProto (encode/decode + garbage), HostState fallback, Deck routing with and
  without helper, LedEffects Wi-Fi cap.
- Swift unit tests: message parsing, action dispatch with a fake process runner, CoreAudio
  wrapper behind a protocol.
- Bench (bring-up steps + log): Wi-Fi join and hotspot fallback, LCD confirm, each action type,
  volume follow, mic level/mute, brownout watch, knob 2 push+turn tuning.

## 12. Order of work
1. Spikes (pass/fail note each): WiFiManager builds on pioarduino 55.03.35 with a custom form;
   Swift reads/writes the vendor HID interface without Input Monitoring permission.
2. Deck: KeyMap + NVS, KnobChord v2, KnobGesture, PendingChange, Keystroke actions.
3. Deck: WifiSession + portal + LED Wi-Fi mode + CI radio check update.
4. Deck: vendor HID + HelperProto + routing.
5. Mac helper: actions, then volume, then mic.
6. Bench session; update kb and wiring docs.
