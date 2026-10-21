# Firmware: configurable keys, Wi-Fi portal, helper link — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deck firmware for spec `docs/superpowers/specs/2026-10-20-wifi-keymap-helper-design.md`: per-key actions stored on the deck, WiFiManager settings portal behind a 5 s two-knob hold, vendor-HID link to a Mac helper (actions out; volume / mic state in), knob 2 push+turn mic level.

**Architecture:** All behaviour lives in `lib/deck_core/` as pure C++17 classes tested on the host (Unity, `test/native/<suite>/`). `src/` only wires hardware: USB HID (keyboard, consumer, vendor), NVS (Preferences), WiFiManager, LCD, LEDs. Two throwaway spikes run first because WiFiManager on core 3.x and the vendor-HID path on macOS are unverified. The Mac helper is a separate plan (Plan 2), written after Task 2's result.

**Tech Stack:** pioarduino espressif32 55.03.35 (Arduino core 3.x / ESP-IDF 5.x), ESP32-S3, TinyUSB via Arduino `USB` lib, tzapu/WiFiManager 2.0.17, Adafruit ST7789/GFX/NeoPixel (pinned), Unity on host.

## Global Constraints

- TDD (CLAUDE.md): failing test first in `test/native/<suite>/`, run it red for the right reason, minimal code, green, refactor. No `Arduino.h` in `lib/deck_core/`.
- Done per task = `~/.platformio/penv/bin/pio test -e native` green **and** `~/.platformio/penv/bin/pio run -e saucedeck -e bringup` SUCCESS with **0 warnings** **and** `pio check -e native --fail-on-defect=medium` pass. Show output.
- Native build uses `-Wall -Wextra -Werror`: every `switch` on an enum must list all values.
- Exact library versions in `lib_deps` (supply chain). WiFiManager = `tzapu/WiFiManager@2.0.17`.
- Wi-Fi headers (`WiFi.h`, `WiFiManager.h`, `ESPmDNS.h`) only in `src/wifi_portal.cpp` (CI-checked, Task 15).
- Vendor HID report size **63 bytes** (Arduino `USBHIDVendor` default). Header 3 bytes → payload ≤ 60.
- Key value max **56** printable ASCII chars (spec delta, see "Spec deltas" at the end).
- Wi-Fi on: LED frame cap **30 mA**; Wi-Fi off: **250 mA** (existing).
- Timings: Wi-Fi hold **5000 ms**, Wi-Fi auto-off **600000 ms**, confirm window **30000 ms**, knob 2 turn qualifies after **150 ms** held, helper presence timeout **3000 ms**, bus-loss **20 ms** (existing).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Run commands from `/Users/antoine/saucedeck`. `P=~/.platformio/penv/bin/pio`.
- Hardware-only behaviour: mark commits "bench-verified" or "not bench-verified" (CLAUDE.md).

## File structure

| File | Responsibility |
| --- | --- |
| `lib/deck_core/src/keystroke.h` | Parse `cmd+shift+4` into HID modifiers + usage |
| `lib/deck_core/src/keymap.h` / `keymap.cpp` | 12 key entries, defaults F13–F24, validation, NVS blob (de)serialize, CRC32 |
| `lib/deck_core/src/knob_chord.h` (modify) | v2: SYNC on release < 5 s, WIFI at 5 s held, countdown |
| `lib/deck_core/src/helper_proto.h` | Vendor HID message encode/decode |
| `lib/deck_core/src/host_link.h` | Helper presence (timeout), last VOLUME / MIC |
| `lib/deck_core/src/wifi_session.h` | Wi-Fi on/off state machine + auto-off |
| `lib/deck_core/src/deck.h` (modify) | Routing: keymap, helper presence, knob 2 gesture, pending change, Wi-Fi chord |
| `lib/deck_core/src/led_effects.h` (modify) | Wi-Fi mode + new action types |
| `lib/deck_core/src/ui_layout.h` (modify) | Mic level + banner in ScreenState, dirty flags |
| `src/keymap_store.cpp/.h` | NVS load/save via Preferences |
| `src/helper_hid.cpp/.h` | USBHIDVendor glue → HelperProto / HostLink |
| `src/wifi_portal.cpp/.h` | WiFiManager portal, params → staged KeyMap; only Wi-Fi file |
| `src/display.cpp` (modify) | Banner line, mic % text |
| `src/main.cpp` (modify) | Executes the new actions |
| `src/spike_wifi/main.cpp`, `src/spike_hid/main.cpp`, `tools/spike/hidprobe.swift` | Throwaway spikes (Tasks 1–2) |
| `tools/check_radio_scope.sh` | CI: radio symbols only from `wifi_portal.cpp.o` |
| Tests | `test/native/test_keymap/`, `test_proto/`, `test_wifi/` (new); `test_volume/`, `test_deck/`, `test_led/`, `test_ui/` (modify) |

---

### Task 1: Spike A — WiFiManager on pioarduino 55.03.35 (bench)

**Files:**
- Create: `src/spike_wifi/main.cpp`
- Modify: `platformio.ini` (new env, exclude spikes from production)
- Modify: `docs/kb/toolchain.md` (result)

**Interfaces:** Produces only a pass/fail fact. Tasks 13–14 assume PASS; on FAIL stop and revise this plan.

- [ ] **Step 1: Add env and exclusions to `platformio.ini`**

Change `[env:saucedeck]` `build_src_filter` to:
```ini
build_src_filter = +<*> -<bringup/> -<spike_wifi/> -<spike_hid/>
```
Append:
```ini
; Throwaway spike: WiFiManager + custom params on core 3.x (plan Task 1)
[env:spike_wifi]
extends = esp32s3
build_src_filter = +<spike_wifi/>
lib_deps =
    tzapu/WiFiManager@2.0.17
```

- [ ] **Step 2: Write `src/spike_wifi/main.cpp`**

```cpp
// Spike: does WiFiManager 2.0.17 build and serve custom params on core 3.x / S3?
// Pass = AP portal shows the 2 custom fields, Save prints them; after joining home Wi-Fi,
// http://<ip>/param shows the same fields (startWebPortal in STA mode).
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

static WiFiManager wm;
static WiFiManagerParameter pType("t1", "Key 1 type (key/app/url/shortcut)", "key", 9);
static WiFiManagerParameter pValue("v1", "Key 1 value", "f13", 57);
static bool webPortal = false;

static void onSave() {
  Serial.printf("SAVE t1='%s' v1='%s'\n", pType.getValue(), pValue.getValue());
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  wm.addParameter(&pType);
  wm.addParameter(&pValue);
  wm.setSaveParamsCallback(onSave);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(20);
  bool ok = wm.autoConnect("SAUCEDECK-setup", "12345678");
  Serial.printf("autoConnect=%d (0 = AP portal running)\n", ok);
}

void loop() {
  wm.process();
  if (WiFi.status() == WL_CONNECTED && !webPortal) {
    wm.startWebPortal();
    webPortal = true;
    Serial.printf("JOINED ip=%s, open http://%s/param\n", WiFi.localIP().toString().c_str(),
                  WiFi.localIP().toString().c_str());
  }
}
```

- [ ] **Step 3: Build**

Run: `$P run -e spike_wifi 2>&1 | grep -E 'error|warning:|SUCCESS|FAILED'`
Expected: `SUCCESS`. If it fails to compile on core 3.x, record the first error and STOP (plan revision needed).

- [ ] **Step 4: Flash and bench**

Run: `$P run -e spike_wifi -t upload --upload-port $(ls /dev/cu.usbmodem* | head -1)` then `$P device monitor`.
Operator: phone joins `SAUCEDECK-setup` (password `12345678`), portal opens, "Setup" page shows the two fields, enter home Wi-Fi + values, Save.
Expected serial: `SAVE t1=... v1=...`, then `JOINED ip=...`. Browser at `http://<ip>/param` shows the fields; Save prints `SAVE` again.

- [ ] **Step 5: Record result and restore production firmware**

Append to `docs/kb/toolchain.md` one line: `- **WiFiManager 2.0.17 on pioarduino 55.03.35: <PASS|FAIL detail>.** [verified: bench <date>]`.
Run: `$P run -e saucedeck -t upload --upload-port $(ls /dev/cu.usbmodem* | head -1)`.

- [ ] **Step 6: Commit**

```bash
git add platformio.ini src/spike_wifi docs/kb/toolchain.md
git commit -m "Spike: WiFiManager 2.0.17 custom params on core 3.x (<PASS|FAIL>)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Spike B — vendor HID echo, macOS access without Input Monitoring (bench)

**Files:**
- Create: `src/spike_hid/main.cpp`, `tools/spike/hidprobe.swift`
- Modify: `platformio.ini`, `docs/kb/toolchain.md`

**Interfaces:** Produces facts for Task 13 and Plan 2: vendor report ID, whether macOS needs Input Monitoring.

- [ ] **Step 1: Add env to `platformio.ini`**

```ini
; Throwaway spike: vendor HID echo (plan Task 2)
[env:spike_hid]
extends = esp32s3
build_src_filter = +<spike_hid/>
```

- [ ] **Step 2: Write `src/spike_hid/main.cpp`**

```cpp
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
```

- [ ] **Step 3: Write `tools/spike/hidprobe.swift`**

```swift
// Spike probe: open SAUCEDECK's vendor HID collection, send a report, wait for the echo.
// Run: swift tools/spike/hidprobe.swift   (no Input Monitoring permission granted)
import Foundation
import IOKit.hid

let mgr = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
let match: [String: Any] = [kIOHIDVendorIDKey: 0x303A, kIOHIDPrimaryUsagePageKey: 0xFF00]
IOHIDManagerSetDeviceMatching(mgr, match as CFDictionary)
IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)
let openResult = IOHIDManagerOpen(mgr, IOOptionBits(kIOHIDOptionsTypeNone))
print("IOHIDManagerOpen:", openResult == kIOReturnSuccess ? "OK" : String(format: "0x%08x", openResult))

guard let devices = IOHIDManagerCopyDevices(mgr) as? Set<IOHIDDevice>, let dev = devices.first else {
  print("FAIL: no vendor HID device (usage page 0xFF00) found"); exit(1)
}
let product = IOHIDDeviceGetProperty(dev, kIOHIDProductKey as CFString) as? String ?? "?"
print("device:", product)

var inBuf = [UInt8](repeating: 0, count: 64)
var echoed = false
let cb: IOHIDReportCallback = { _, _, _, _, reportID, report, length in
  let bytes = (0..<min(length, 4)).map { String(format: "%02X", report[$0]) }.joined(separator: " ")
  print("ECHO reportID=\(reportID) len=\(length): \(bytes)")
  echoed = true
}
IOHIDDeviceRegisterInputReportCallback(dev, &inBuf, inBuf.count, cb, nil)

var report = [UInt8](repeating: 0, count: 63)
report[0] = 1; report[1] = 0x01; report[2] = 1; report[3] = 1  // HELLO v1
for id in 1...6 where !echoed {
  let r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, CFIndex(id), report, report.count)
  print("SetReport id=\(id):", r == kIOReturnSuccess ? "OK" : String(format: "0x%08x", r))
  CFRunLoopRunInMode(CFRunLoopMode.defaultMode, 0.5, false)
}
print(echoed ? "PASS: echo received" : "FAIL: no echo")
```

- [ ] **Step 4: Build, flash, run probe**

Run: `$P run -e spike_hid -t upload --upload-port $(ls /dev/cu.usbmodem* | head -1)` (expect `SUCCESS`), then `swift tools/spike/hidprobe.swift`.
Expected PASS: `IOHIDManagerOpen: OK`, `SetReport id=N: OK`, `ECHO reportID=N ...`, no macOS permission dialog. Record N (vendor report ID) and whether a dialog appeared.
Also run `ioreg -r -c IOHIDDevice -l -w0 | grep -E '"PrimaryUsagePage"|"Product"'` and note whether 0xFF00 (65280) is a separate IOHIDDevice.

- [ ] **Step 5: Record result, restore production**

Append to `docs/kb/toolchain.md`: `- **Vendor HID (USBHIDVendor 63 B) on macOS: <PASS/FAIL>, report ID <N>, Input Monitoring prompt <yes/no>.** [verified: bench <date>]`.
Run: `$P run -e saucedeck -t upload --upload-port $(ls /dev/cu.usbmodem* | head -1)`.

- [ ] **Step 6: Commit**

```bash
git add platformio.ini src/spike_hid tools/spike docs/kb/toolchain.md
git commit -m "Spike: vendor HID echo to macOS (<PASS|FAIL>, report ID <N>)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

If FAIL (no echo, or permission required): STOP. Plan 2 and Task 13 need a different transport (CDC serial framing) — revise before continuing.

---

### Task 3: Keystroke parser

**Files:**
- Create: `lib/deck_core/src/keystroke.h`
- Test: `test/native/test_keymap/test_main.cpp` (new suite)

**Interfaces:**
- Produces: `struct Keystroke { uint8_t mods; uint8_t usage; };` — `mods` bit0 Ctrl, bit1 Shift, bit2 Alt, bit3 Cmd (HID modifier usage = `0xE0 + bit`); `usage` = HID keyboard usage ID.
- Produces: `bool parseKeystroke(const char *text, Keystroke &out);`

- [ ] **Step 1: Write the failing tests** (`test/native/test_keymap/test_main.cpp`)

```cpp
#include <unity.h>
#include "keystroke.h"

void setUp() {}
void tearDown() {}

static Keystroke ks(const char *t, bool expectOk = true) {
  Keystroke k{0xFF, 0xFF};
  TEST_ASSERT_EQUAL(expectOk, parseKeystroke(t, k));
  return k;
}

void test_ks_single_letter() {
  Keystroke k = ks("a");
  TEST_ASSERT_EQUAL_HEX8(0x00, k.mods);
  TEST_ASSERT_EQUAL_HEX8(0x04, k.usage);
}

void test_ks_cmd_shift_4() {
  Keystroke k = ks("cmd+shift+4");
  TEST_ASSERT_EQUAL_HEX8(0x0A, k.mods);  // shift bit1 | cmd bit3
  TEST_ASSERT_EQUAL_HEX8(0x21, k.usage);
}

void test_ks_all_modifiers_case_insensitive() {
  Keystroke k = ks("CTRL+Alt+Shift+Cmd+z");
  TEST_ASSERT_EQUAL_HEX8(0x0F, k.mods);
  TEST_ASSERT_EQUAL_HEX8(0x1D, k.usage);
}

void test_ks_digits_and_named_keys() {
  TEST_ASSERT_EQUAL_HEX8(0x1E, ks("1").usage);
  TEST_ASSERT_EQUAL_HEX8(0x27, ks("0").usage);
  TEST_ASSERT_EQUAL_HEX8(0x28, ks("enter").usage);
  TEST_ASSERT_EQUAL_HEX8(0x29, ks("esc").usage);
  TEST_ASSERT_EQUAL_HEX8(0x2B, ks("tab").usage);
  TEST_ASSERT_EQUAL_HEX8(0x2C, ks("space").usage);
}

void test_ks_function_keys() {
  TEST_ASSERT_EQUAL_HEX8(0x3A, ks("f1").usage);
  TEST_ASSERT_EQUAL_HEX8(0x45, ks("f12").usage);
  TEST_ASSERT_EQUAL_HEX8(0x68, ks("f13").usage);
  TEST_ASSERT_EQUAL_HEX8(0x73, ks("f24").usage);
}

void test_ks_rejects_bad_input() {
  ks("", false);
  ks("cmd", false);            // no key
  ks("a+b", false);            // two keys
  ks("cmd++a", false);         // empty token
  ks("hyper+a", false);        // unknown modifier
  ks("f25", false);
  ks("f0", false);
  ks("cmd+shift+4 ", false);   // trailing space
  ks("é", false);
}

void test_ks_rejects_overlong_input() {
  char longText[80];
  for (int i = 0; i < 79; i++) longText[i] = 'a';
  longText[79] = 0;
  ks(longText, false);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ks_single_letter);
  RUN_TEST(test_ks_cmd_shift_4);
  RUN_TEST(test_ks_all_modifiers_case_insensitive);
  RUN_TEST(test_ks_digits_and_named_keys);
  RUN_TEST(test_ks_function_keys);
  RUN_TEST(test_ks_rejects_bad_input);
  RUN_TEST(test_ks_rejects_overlong_input);
  return UNITY_END();
}
```

Stub `lib/deck_core/src/keystroke.h`:
```cpp
#pragma once
#include <stdint.h>
struct Keystroke { uint8_t mods; uint8_t usage; };
inline bool parseKeystroke(const char *text, Keystroke &out) { (void)text; (void)out; return false; }
```

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_keymap`
Expected: FAIL in `test_ks_single_letter` etc. (`Expected 1 Was 0`); `test_ks_rejects_*` pass on the stub.

- [ ] **Step 3: Implement `lib/deck_core/src/keystroke.h`**

```cpp
#pragma once
#include <stdint.h>
#include <string.h>

// Keystroke for a deck key: modifiers + exactly one key, e.g. "cmd+shift+4".
// mods: bit0 Ctrl, bit1 Shift, bit2 Alt, bit3 Cmd -> HID modifier usage 0xE0 + bit.
// usage: HID keyboard usage ID (USB HID Usage Tables, Keyboard page 0x07).
struct Keystroke {
  uint8_t mods;
  uint8_t usage;
};

namespace keystroke_detail {
inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline bool tokenIs(const char *t, size_t n, const char *word) {
  return strlen(word) == n && strncmp(t, word, n) == 0;
}

// Returns modifier bit (0..3) or -1.
inline int modifierBit(const char *t, size_t n) {
  if (tokenIs(t, n, "ctrl")) return 0;
  if (tokenIs(t, n, "shift")) return 1;
  if (tokenIs(t, n, "alt")) return 2;
  if (tokenIs(t, n, "cmd")) return 3;
  return -1;
}

// Returns HID usage or 0.
inline uint8_t keyUsage(const char *t, size_t n) {
  if (n == 1 && t[0] >= 'a' && t[0] <= 'z') return static_cast<uint8_t>(0x04 + (t[0] - 'a'));
  if (n == 1 && t[0] >= '1' && t[0] <= '9') return static_cast<uint8_t>(0x1E + (t[0] - '1'));
  if (n == 1 && t[0] == '0') return 0x27;
  if (tokenIs(t, n, "enter")) return 0x28;
  if (tokenIs(t, n, "esc")) return 0x29;
  if (tokenIs(t, n, "tab")) return 0x2B;
  if (tokenIs(t, n, "space")) return 0x2C;
  if (n >= 2 && n <= 3 && t[0] == 'f') {
    int num = 0;
    for (size_t i = 1; i < n; i++) {
      if (t[i] < '0' || t[i] > '9') return 0;
      num = num * 10 + (t[i] - '0');
    }
    if (num >= 1 && num <= 12) return static_cast<uint8_t>(0x3A + num - 1);
    if (num >= 13 && num <= 24) return static_cast<uint8_t>(0x68 + num - 13);
  }
  return 0;
}
}  // namespace keystroke_detail

inline bool parseKeystroke(const char *text, Keystroke &out) {
  using namespace keystroke_detail;
  char buf[48];
  size_t len = strnlen(text, sizeof buf);
  if (len == 0 || len >= sizeof buf) return false;
  for (size_t i = 0; i < len; i++) buf[i] = lower(text[i]);
  buf[len] = 0;

  uint8_t mods = 0, usage = 0;
  size_t start = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i < len && buf[i] != '+') continue;
    size_t n = i - start;
    if (n == 0) return false;
    const char *tok = buf + start;
    int bit = modifierBit(tok, n);
    if (bit >= 0) {
      mods |= static_cast<uint8_t>(1u << bit);
    } else {
      uint8_t u = keyUsage(tok, n);
      if (u == 0 || usage != 0) return false;  // unknown token, or a second key
      usage = u;
    }
    start = i + 1;
  }
  if (usage == 0) return false;
  out = {mods, usage};
  return true;
}
```

- [ ] **Step 4: Run green**

Run: `$P test -e native -f native/test_keymap`
Expected: `7 test cases: 7 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/keystroke.h test/native/test_keymap
git commit -m "Keystroke parser: cmd+shift+4 -> HID mods + usage

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: KeyMap entries, defaults, validation

**Files:**
- Create: `lib/deck_core/src/keymap.h`, `lib/deck_core/src/keymap.cpp`
- Test: `test/native/test_keymap/test_main.cpp` (append)

**Interfaces:**
- Consumes: `Keystroke`, `parseKeystroke` (Task 3).
- Produces:
```cpp
enum class KeyType : uint8_t { Keystroke = 0, App = 1, Url = 2, Shortcut = 3 };
constexpr size_t kKeyCount = 12;
constexpr size_t kKeyValueMax = 56;
struct KeyEntry { KeyType type; char value[kKeyValueMax + 1]; Keystroke ks; };
enum class KeyMapError : uint8_t { None, BadIndex, BadType, Empty, TooLong, NotPrintable, BadUrl, BadKeystroke };
bool parseKeyType(const char *s, KeyType &out);     // "key" | "app" | "url" | "shortcut"
const char *keyTypeName(KeyType t);
class KeyMap {
 public:
  KeyMap();                                          // defaults: Keystroke "f13".."f24"
  const KeyEntry &entry(uint8_t i) const;            // i < kKeyCount
  KeyMapError set(uint8_t i, KeyType t, const char *value);
  KeyMapError setFromStrings(uint8_t i, const char *type, const char *value);
  bool operator==(const KeyMap &o) const;
};
```

- [ ] **Step 1: Append failing tests**

Add `#include "keymap.h"` after `#include "keystroke.h"`, then before `int main`:
```cpp
// --- KeyMap ---

void test_keymap_defaults_are_f13_to_f24() {
  KeyMap m;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    TEST_ASSERT_TRUE(m.entry(i).type == KeyType::Keystroke);
    TEST_ASSERT_EQUAL_HEX8(0x68 + i, m.entry(i).ks.usage);
    TEST_ASSERT_EQUAL_HEX8(0, m.entry(i).ks.mods);
  }
  TEST_ASSERT_EQUAL_STRING("f13", m.entry(0).value);
}

void test_keymap_set_app_url_shortcut() {
  KeyMap m;
  TEST_ASSERT_TRUE(m.set(0, KeyType::App, "Calculator") == KeyMapError::None);
  TEST_ASSERT_TRUE(m.set(1, KeyType::Url, "https://www.linkedin.com") == KeyMapError::None);
  TEST_ASSERT_TRUE(m.set(2, KeyType::Shortcut, "Start focus") == KeyMapError::None);
  TEST_ASSERT_EQUAL_STRING("Calculator", m.entry(0).value);
  TEST_ASSERT_TRUE(m.entry(1).type == KeyType::Url);
}

void test_keymap_set_keystroke_parses() {
  KeyMap m;
  TEST_ASSERT_TRUE(m.set(7, KeyType::Keystroke, "cmd+shift+4") == KeyMapError::None);
  TEST_ASSERT_EQUAL_HEX8(0x0A, m.entry(7).ks.mods);
  TEST_ASSERT_EQUAL_HEX8(0x21, m.entry(7).ks.usage);
}

void test_keymap_rejections_leave_entry_unchanged() {
  KeyMap m;
  TEST_ASSERT_TRUE(m.set(12, KeyType::App, "x") == KeyMapError::BadIndex);
  TEST_ASSERT_TRUE(m.set(0, KeyType::App, "") == KeyMapError::Empty);
  TEST_ASSERT_TRUE(m.set(0, KeyType::App, "Calc\nulator") == KeyMapError::NotPrintable);
  TEST_ASSERT_TRUE(m.set(0, KeyType::Url, "ftp://x") == KeyMapError::BadUrl);
  TEST_ASSERT_TRUE(m.set(0, KeyType::Url, "javascript:alert(1)") == KeyMapError::BadUrl);
  TEST_ASSERT_TRUE(m.set(0, KeyType::Keystroke, "a+b") == KeyMapError::BadKeystroke);
  TEST_ASSERT_TRUE(m.set(0, static_cast<KeyType>(9), "x") == KeyMapError::BadType);
  TEST_ASSERT_TRUE(m.entry(0).type == KeyType::Keystroke);
  TEST_ASSERT_EQUAL_STRING("f13", m.entry(0).value);
}

void test_keymap_value_length_limit() {
  KeyMap m;
  char v[kKeyValueMax + 2];
  memset(v, 'a', kKeyValueMax);
  v[kKeyValueMax] = 0;
  TEST_ASSERT_TRUE(m.set(0, KeyType::App, v) == KeyMapError::None);
  v[kKeyValueMax] = 'a';
  v[kKeyValueMax + 1] = 0;
  TEST_ASSERT_TRUE(m.set(0, KeyType::App, v) == KeyMapError::TooLong);
}

void test_keymap_set_from_strings() {
  KeyMap m;
  TEST_ASSERT_TRUE(m.setFromStrings(3, "app", "Visual Studio Code") == KeyMapError::None);
  TEST_ASSERT_TRUE(m.entry(3).type == KeyType::App);
  TEST_ASSERT_TRUE(m.setFromStrings(3, "macro", "x") == KeyMapError::BadType);
  KeyType t;
  TEST_ASSERT_TRUE(parseKeyType("shortcut", t) && t == KeyType::Shortcut);
  TEST_ASSERT_EQUAL_STRING("url", keyTypeName(KeyType::Url));
}

void test_keymap_equality() {
  KeyMap a, b;
  TEST_ASSERT_TRUE(a == b);
  b.set(5, KeyType::App, "Notes");
  TEST_ASSERT_FALSE(a == b);
}
```
Add `#include <string.h>` at the top and RUN_TEST lines for all 7 new tests.

Stub `keymap.h` declaring the Interfaces above with `KeyMap()` leaving entries zeroed, `set`/`setFromStrings` returning `KeyMapError::None` without writing, `parseKeyType` returning false, `keyTypeName` returning `""`, `operator==` returning true; empty `keymap.cpp` with `#include "keymap.h"`.

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_keymap`
Expected: new tests FAIL (e.g. `test_keymap_defaults_are_f13_to_f24: Expected 0x68 Was 0x00`).

- [ ] **Step 3: Implement**

`lib/deck_core/src/keymap.h`:
```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "keystroke.h"

enum class KeyType : uint8_t { Keystroke = 0, App = 1, Url = 2, Shortcut = 3 };
constexpr size_t kKeyCount = 12;
constexpr size_t kKeyValueMax = 56;  // fits one 63-byte vendor HID report (helper_proto.h)

struct KeyEntry {
  KeyType type;
  char value[kKeyValueMax + 1];
  Keystroke ks;  // valid only when type == Keystroke
};

enum class KeyMapError : uint8_t { None, BadIndex, BadType, Empty, TooLong, NotPrintable, BadUrl, BadKeystroke };

bool parseKeyType(const char *s, KeyType &out);
const char *keyTypeName(KeyType t);

// What each deck key does. Values are validated here so nothing downstream (HID, helper)
// ever sees free-form input: printable ASCII only, URLs http(s) only, keystrokes parsed.
class KeyMap {
 public:
  KeyMap();
  const KeyEntry &entry(uint8_t i) const { return entries_[i < kKeyCount ? i : 0]; }
  KeyMapError set(uint8_t i, KeyType t, const char *value);
  KeyMapError setFromStrings(uint8_t i, const char *type, const char *value);
  bool operator==(const KeyMap &o) const;

 private:
  KeyEntry entries_[kKeyCount];
};
```

`lib/deck_core/src/keymap.cpp`:
```cpp
#include "keymap.h"
#include <stdio.h>
#include <string.h>

bool parseKeyType(const char *s, KeyType &out) {
  if (strcmp(s, "key") == 0) { out = KeyType::Keystroke; return true; }
  if (strcmp(s, "app") == 0) { out = KeyType::App; return true; }
  if (strcmp(s, "url") == 0) { out = KeyType::Url; return true; }
  if (strcmp(s, "shortcut") == 0) { out = KeyType::Shortcut; return true; }
  return false;
}

const char *keyTypeName(KeyType t) {
  switch (t) {
    case KeyType::Keystroke: return "key";
    case KeyType::App: return "app";
    case KeyType::Url: return "url";
    case KeyType::Shortcut: return "shortcut";
  }
  return "";
}

KeyMap::KeyMap() {
  for (uint8_t i = 0; i < kKeyCount; i++) {
    char v[8];
    snprintf(v, sizeof v, "f%u", 13u + i);
    entries_[i] = KeyEntry{};
    set(i, KeyType::Keystroke, v);
  }
}

KeyMapError KeyMap::set(uint8_t i, KeyType t, const char *value) {
  if (i >= kKeyCount) return KeyMapError::BadIndex;
  if (t != KeyType::Keystroke && t != KeyType::App && t != KeyType::Url && t != KeyType::Shortcut)
    return KeyMapError::BadType;
  size_t n = strnlen(value, kKeyValueMax + 1);
  if (n == 0) return KeyMapError::Empty;
  if (n > kKeyValueMax) return KeyMapError::TooLong;
  for (size_t k = 0; k < n; k++)
    if (value[k] < 0x20 || value[k] > 0x7E) return KeyMapError::NotPrintable;

  Keystroke ks{0, 0};
  if (t == KeyType::Url && strncmp(value, "http://", 7) != 0 && strncmp(value, "https://", 8) != 0)
    return KeyMapError::BadUrl;
  if (t == KeyType::Keystroke && !parseKeystroke(value, ks)) return KeyMapError::BadKeystroke;

  KeyEntry &e = entries_[i];
  e.type = t;
  memcpy(e.value, value, n);
  e.value[n] = 0;
  e.ks = ks;
  return KeyMapError::None;
}

KeyMapError KeyMap::setFromStrings(uint8_t i, const char *type, const char *value) {
  KeyType t;
  if (!parseKeyType(type, t)) return KeyMapError::BadType;
  return set(i, t, value);
}

bool KeyMap::operator==(const KeyMap &o) const {
  for (uint8_t i = 0; i < kKeyCount; i++)
    if (entries_[i].type != o.entries_[i].type || strcmp(entries_[i].value, o.entries_[i].value) != 0)
      return false;
  return true;
}
```

- [ ] **Step 4: Run green**

Run: `$P test -e native -f native/test_keymap`
Expected: `14 test cases: 14 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/keymap.h lib/deck_core/src/keymap.cpp test/native/test_keymap
git commit -m "KeyMap: per-key action entries, F13-F24 defaults, strict validation

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: KeyMap NVS blob + CRC32

**Files:**
- Modify: `lib/deck_core/src/keymap.h`, `lib/deck_core/src/keymap.cpp`
- Test: `test/native/test_keymap/test_main.cpp` (append)

**Interfaces:**
- Produces:
```cpp
uint32_t crc32(const uint8_t *data, size_t n);                            // IEEE 802.3
constexpr size_t kKeyMapBlobMax = 4 + 1 + kKeyCount * (2 + kKeyValueMax) + 4;  // 705
size_t serializeKeyMap(const KeyMap &m, uint8_t *buf, size_t cap);        // 0 if cap too small
bool deserializeKeyMap(const uint8_t *buf, size_t n, KeyMap &out);        // false -> out untouched
```
Blob: `'S','D','K','M'`, version `1`, 12 × `[type, len, value bytes]`, CRC32 LE over all preceding bytes.

- [ ] **Step 1: Append failing tests**

```cpp
// --- KeyMap blob ---

void test_crc32_check_value() {
  const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32(s, sizeof s));
}

void test_blob_roundtrip() {
  KeyMap m;
  m.set(0, KeyType::App, "Calculator");
  m.set(4, KeyType::Url, "https://www.linkedin.com");
  m.set(7, KeyType::Keystroke, "cmd+shift+4");
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  KeyMap back;
  TEST_ASSERT_TRUE(deserializeKeyMap(buf, n, back));
  TEST_ASSERT_TRUE(back == m);
  TEST_ASSERT_EQUAL_HEX8(0x21, back.entry(7).ks.usage);  // keystroke re-parsed
}

void test_blob_rejects_corruption_and_keeps_target() {
  KeyMap m;
  m.set(0, KeyType::App, "Calculator");
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  KeyMap target;
  target.set(1, KeyType::App, "Notes");
  KeyMap before = target;

  buf[10] ^= 0x01;  // payload bit flip -> CRC mismatch
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target));
  buf[10] ^= 0x01;
  buf[0] = 'X';  // magic
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target));
  buf[0] = 'S';
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n - 1, target));  // truncated
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, 0, target));
  TEST_ASSERT_TRUE(target == before);
}

void test_blob_rejects_valid_crc_but_invalid_entry() {
  // Attacker-crafted blob with a correct CRC must still pass KeyMap validation
  KeyMap m;
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  buf[5] = 2;  // key 0 type -> Url, value "f13" is not http(s)
  uint32_t c = crc32(buf, n - 4);
  buf[n - 4] = c & 0xFF; buf[n - 3] = (c >> 8) & 0xFF; buf[n - 2] = (c >> 16) & 0xFF; buf[n - 1] = c >> 24;
  KeyMap target;
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target));
}

void test_blob_small_buffer_returns_zero() {
  KeyMap m;
  uint8_t buf[16];
  TEST_ASSERT_EQUAL(0, serializeKeyMap(m, buf, sizeof buf));
}
```
Add RUN_TEST lines. Stubs: `crc32` returns 0, `serializeKeyMap` returns 0, `deserializeKeyMap` returns false.

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_keymap`
Expected: `test_crc32_check_value` and `test_blob_roundtrip` FAIL; the rejection tests pass on stubs.

- [ ] **Step 3: Implement (append to `keymap.cpp`, declarations to `keymap.h`)**

```cpp
uint32_t crc32(const uint8_t *data, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= data[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static const uint8_t kMagic[4] = {'S', 'D', 'K', 'M'};
static const uint8_t kBlobVersion = 1;

size_t serializeKeyMap(const KeyMap &m, uint8_t *buf, size_t cap) {
  size_t need = 4 + 1 + 4;
  for (uint8_t i = 0; i < kKeyCount; i++) need += 2 + strlen(m.entry(i).value);
  if (cap < need) return 0;
  size_t p = 0;
  memcpy(buf, kMagic, 4);
  p = 4;
  buf[p++] = kBlobVersion;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    const KeyEntry &e = m.entry(i);
    size_t len = strlen(e.value);
    buf[p++] = static_cast<uint8_t>(e.type);
    buf[p++] = static_cast<uint8_t>(len);
    memcpy(buf + p, e.value, len);
    p += len;
  }
  uint32_t c = crc32(buf, p);
  for (int k = 0; k < 4; k++) buf[p++] = static_cast<uint8_t>(c >> (8 * k));
  return p;
}

bool deserializeKeyMap(const uint8_t *buf, size_t n, KeyMap &out) {
  if (n < 4 + 1 + 4 || memcmp(buf, kMagic, 4) != 0 || buf[4] != kBlobVersion) return false;
  uint32_t stored = buf[n - 4] | (buf[n - 3] << 8) | (buf[n - 2] << 16) | (static_cast<uint32_t>(buf[n - 1]) << 24);
  if (crc32(buf, n - 4) != stored) return false;
  KeyMap m;
  size_t p = 5;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    if (p + 2 > n - 4) return false;
    uint8_t type = buf[p++];
    uint8_t len = buf[p++];
    if (len > kKeyValueMax || p + len > n - 4) return false;
    char v[kKeyValueMax + 1];
    memcpy(v, buf + p, len);
    v[len] = 0;
    p += len;
    if (m.set(i, static_cast<KeyType>(type), v) != KeyMapError::None) return false;
  }
  if (p != n - 4) return false;
  out = m;
  return true;
}
```

- [ ] **Step 4: Run green**

Run: `$P test -e native -f native/test_keymap`
Expected: `19 test cases: 19 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/keymap.h lib/deck_core/src/keymap.cpp test/native/test_keymap
git commit -m "KeyMap NVS blob: magic, version, CRC32, full re-validation on load

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: KnobChord v2 (SYNC on release < 5 s, WIFI at 5 s)

**Files:**
- Modify: `lib/deck_core/src/knob_chord.h`
- Modify: `lib/deck_core/src/deck.h` (pass time to chord)
- Test: `test/native/test_volume/test_main.cpp` (chord section), `test/native/test_deck/test_main.cpp` (sync test)

**Interfaces:**
- Produces: `CHORD_WIFI = 1u << 3`; `KnobChord::kWifiHoldMs = 5000`; `uint8_t KnobChord::update(uint16_t pressed, uint32_t nowMs)`; `uint32_t KnobChord::bothHeldMs(uint32_t nowMs) const` (0 when not both held).

- [ ] **Step 1: Rewrite the chord tests in `test/native/test_volume/test_main.cpp`**

Replace every `c.update(X)` call with `c.update(X, t)` using a local `uint32_t t` that increases by 10 per call, and replace `test_chord_both_pressed_fires_sync_once`, `test_chord_releases_after_sync_fire_nothing`, `test_chord_simultaneous_release_after_sync` with:
```cpp
void test_chord_sync_fires_on_release_before_5s() {
  KnobChord c(S1, S2);
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S1 | S2, 0));
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S1 | S2, 4999));
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S2, 5000));  // one up: chord still running
  TEST_ASSERT_EQUAL_HEX8(CHORD_SYNC, c.update(0, 5010));
}

void test_chord_wifi_fires_once_at_5s_while_held() {
  KnobChord c(S1, S2);
  c.update(S1 | S2, 100);
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S1 | S2, 5099));
  TEST_ASSERT_EQUAL_HEX8(CHORD_WIFI, c.update(S1 | S2, 5100));
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S1 | S2, 9000));
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(0, 9010));  // no SYNC after WIFI
}

void test_chord_releases_after_chord_fire_no_single_knob() {
  KnobChord c(S1, S2);
  c.update(S1 | S2, 0);
  TEST_ASSERT_EQUAL_HEX8(CHORD_NONE, c.update(S2, 100));
  TEST_ASSERT_EQUAL_HEX8(CHORD_SYNC, c.update(0, 200));
}

void test_chord_countdown() {
  KnobChord c(S1, S2);
  TEST_ASSERT_EQUAL_UINT32(0, c.bothHeldMs(0));
  c.update(S1 | S2, 1000);
  TEST_ASSERT_EQUAL_UINT32(1500, c.bothHeldMs(2500));
  c.update(S1, 2600);
  TEST_ASSERT_EQUAL_UINT32(0, c.bothHeldMs(2700));
}
```
Update the RUN_TEST list accordingly. In `test/native/test_deck/test_main.cpp`, change `test_deck_sync_resets_speaker_and_mic_without_hid` so the `hold(both)` call expects `a.count == 0` and the following `hold(d, 0, t)` expects one `ActionType::Sync`:
```cpp
  a = hold(d, DECK_ENC1_SW | DECK_ENC2_SW, t);
  TEST_ASSERT_EQUAL(0, a.count);  // nothing until release
  a = hold(d, 0, t);
  TEST_ASSERT_EQUAL(1, a.count);  // local Sync only, no HID action
  TEST_ASSERT_TRUE(has(a, ActionType::Sync));
  TEST_ASSERT_EQUAL_UINT8(0, d.speaker().steps());
  TEST_ASSERT_FALSE(d.micMuted());
```

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_volume -f native/test_deck`
Expected: compile error `too many arguments to function call` for `update(X, t)` — add `uint32_t nowMs` parameter as `(void)nowMs` to the stub signature, rerun; then expected FAIL in `test_chord_sync_fires_on_release_before_5s` (`Expected 0x00 Was 0x04` at press) and `test_chord_wifi_fires_once_at_5s_while_held`.

- [ ] **Step 3: Implement `lib/deck_core/src/knob_chord.h`**

```cpp
#pragma once
#include <stdint.h>

enum : uint8_t {
  CHORD_NONE = 0,
  CHORD_KNOB1 = 1u << 0,  // knob 1 switch pressed and released alone
  CHORD_KNOB2 = 1u << 1,  // knob 2 switch pressed and released alone
  CHORD_SYNC = 1u << 2,   // both held, chord ended before kWifiHoldMs
  CHORD_WIFI = 1u << 3,   // both held for kWifiHoldMs (fires once, while held)
};

// Knob push-switch chord detector. Feed the debounced deck mask every scan.
// Single knobs fire on release; a two-knob chord decides on duration: released early =
// SYNC, held to 5 s = WIFI. After the chord starts nothing else fires until both are up.
class KnobChord {
 public:
  static constexpr uint32_t kWifiHoldMs = 5000;

  KnobChord(uint16_t sw1, uint16_t sw2) : sw1_(sw1), sw2_(sw2) {}

  uint8_t update(uint16_t pressed, uint32_t nowMs) {
    bool a = pressed & sw1_;
    bool b = pressed & sw2_;
    uint8_t out = CHORD_NONE;
    if (a && b && !chord_) {
      chord_ = true;
      wifiFired_ = false;
      timing_ = true;
      since_ = nowMs;
    }
    if (chord_ && timing_ && !(a && b)) timing_ = false;  // first knob up: clock stops
    if (chord_ && timing_ && !wifiFired_ && nowMs - since_ >= kWifiHoldMs) {
      wifiFired_ = true;
      out |= CHORD_WIFI;
    }
    if (!chord_) {
      if (wasA_ && !a) out |= CHORD_KNOB1;
      if (wasB_ && !b) out |= CHORD_KNOB2;
    }
    if (chord_ && !a && !b) {
      if (!wifiFired_) out |= CHORD_SYNC;
      chord_ = false;
    }
    wasA_ = a;
    wasB_ = b;
    held_ = a && b;
    return out;
  }

  // For the LCD countdown: how long both have been held, 0 if not both held.
  uint32_t bothHeldMs(uint32_t nowMs) const { return (chord_ && timing_ && held_) ? nowMs - since_ : 0; }

 private:
  uint16_t sw1_, sw2_;
  bool wasA_ = false, wasB_ = false, chord_ = false, wifiFired_ = false, timing_ = false, held_ = false;
  uint32_t since_ = 0;
};
```

In `lib/deck_core/src/deck.h` change `uint8_t chord = chord_.update(now);` to `uint8_t chord = chord_.update(now, nowMs);`.

- [ ] **Step 4: Run green (all suites)**

Run: `$P test -e native`
Expected: all pass (`test_chord_still_held_partner_blocks_single_action` and `test_chord_rearms_after_both_released` still pass with times added).

- [ ] **Step 5: Firmware build**

Run: `$P run -e saucedeck -e bringup 2>&1 | grep -cE 'warning:'; $P run -e saucedeck -e bringup 2>&1 | tail -3`
Expected: `0`, both `SUCCESS`.

- [ ] **Step 6: Commit**

```bash
git add lib/deck_core/src/knob_chord.h lib/deck_core/src/deck.h test/native/test_volume test/native/test_deck
git commit -m "KnobChord v2: sync on release before 5 s, Wi-Fi at 5 s hold

Behaviour change: both-knob sync now fires on release. Not bench-verified.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: HelperProto (vendor HID messages)

**Files:**
- Create: `lib/deck_core/src/helper_proto.h`
- Test: `test/native/test_proto/test_main.cpp` (new suite)

**Interfaces:**
- Consumes: `KeyType`, `kKeyValueMax` (Task 4).
- Produces:
```cpp
constexpr size_t kReportSize = 63;
constexpr uint8_t kProtoVersion = 1;
enum : uint8_t { MSG_HELLO = 0x01, MSG_VOLUME = 0x02, MSG_MIC = 0x03,
                 MSG_ACTION = 0x81, MSG_MIC_DELTA = 0x82, MSG_MIC_MUTE = 0x83 };
struct HostMsg { uint8_t type; uint8_t a; uint8_t b; };   // HELLO a=ver; VOLUME/MIC a=pct b=muted
bool decodeHost(const uint8_t *buf, size_t n, HostMsg &out);
size_t encodeAction(uint8_t key, KeyType t, const char *value, uint8_t *out, size_t cap);
size_t encodeMicDelta(int8_t steps, uint8_t *out, size_t cap);
size_t encodeMicMute(uint8_t *out, size_t cap);
```
Wire format: `[ver=1, type, len, payload...]`, zero-padded to 63. Encoders return 63 (full report) or 0.

- [ ] **Step 1: Write failing tests** (`test/native/test_proto/test_main.cpp`)

```cpp
#include <unity.h>
#include <string.h>
#include "helper_proto.h"

void setUp() {}
void tearDown() {}

static uint8_t rep[kReportSize];
static void msg(uint8_t type, uint8_t len, uint8_t p0 = 0, uint8_t p1 = 0) {
  memset(rep, 0, sizeof rep);
  rep[0] = kProtoVersion; rep[1] = type; rep[2] = len; rep[3] = p0; rep[4] = p1;
}

void test_decode_hello_volume_mic() {
  HostMsg m;
  msg(MSG_HELLO, 1, 3);
  TEST_ASSERT_TRUE(decodeHost(rep, sizeof rep, m));
  TEST_ASSERT_EQUAL_HEX8(MSG_HELLO, m.type);
  TEST_ASSERT_EQUAL_UINT8(3, m.a);
  msg(MSG_VOLUME, 2, 42, 1);
  TEST_ASSERT_TRUE(decodeHost(rep, sizeof rep, m));
  TEST_ASSERT_EQUAL_UINT8(42, m.a);
  TEST_ASSERT_EQUAL_UINT8(1, m.b);
  msg(MSG_MIC, 2, 100, 0);
  TEST_ASSERT_TRUE(decodeHost(rep, sizeof rep, m));
}

void test_decode_rejects_bad_fields() {
  HostMsg m;
  msg(MSG_VOLUME, 2, 101, 0); TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // pct > 100
  msg(MSG_VOLUME, 2, 50, 2);  TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // muted not 0/1
  msg(MSG_VOLUME, 3, 50, 0);  TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // wrong len
  msg(MSG_ACTION, 1, 0);      TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // deck->host type
  msg(0x7F, 0);               TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // unknown
  msg(MSG_HELLO, 61, 1);      TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // len > 60
  msg(MSG_HELLO, 1, 1); rep[0] = 2;
  TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));                               // version
  msg(MSG_HELLO, 1, 1);
  TEST_ASSERT_FALSE(decodeHost(rep, 2, m));                                        // short
}

void test_decode_garbage_never_crashes_or_accepts_out_of_range() {
  uint32_t x = 12345;
  HostMsg m;
  for (int i = 0; i < 20000; i++) {
    for (size_t k = 0; k < kReportSize; k++) {
      x = x * 1103515245u + 12345u;
      rep[k] = static_cast<uint8_t>(x >> 16);
    }
    size_t n = (x >> 8) % (kReportSize + 1);
    if (decodeHost(rep, n, m)) {
      TEST_ASSERT_TRUE(m.type == MSG_HELLO || m.type == MSG_VOLUME || m.type == MSG_MIC);
      if (m.type != MSG_HELLO) TEST_ASSERT_TRUE(m.a <= 100 && m.b <= 1);
    }
  }
}

void test_encode_action() {
  uint8_t out[kReportSize];
  TEST_ASSERT_EQUAL(kReportSize, encodeAction(4, KeyType::Url, "https://x.io", out, sizeof out));
  TEST_ASSERT_EQUAL_HEX8(kProtoVersion, out[0]);
  TEST_ASSERT_EQUAL_HEX8(MSG_ACTION, out[1]);
  TEST_ASSERT_EQUAL_UINT8(3 + 12, out[2]);
  TEST_ASSERT_EQUAL_UINT8(4, out[3]);
  TEST_ASSERT_EQUAL_UINT8(2, out[4]);   // KeyType::Url
  TEST_ASSERT_EQUAL_UINT8(12, out[5]);
  TEST_ASSERT_EQUAL_MEMORY("https://x.io", out + 6, 12);
  TEST_ASSERT_EQUAL_HEX8(0, out[18]);  // zero padded
}

void test_encode_action_rejects() {
  uint8_t out[kReportSize];
  char v[kKeyValueMax + 2];
  memset(v, 'a', kKeyValueMax + 1);
  v[kKeyValueMax + 1] = 0;
  TEST_ASSERT_EQUAL(0, encodeAction(0, KeyType::App, v, out, sizeof out));            // too long
  TEST_ASSERT_EQUAL(0, encodeAction(12, KeyType::App, "x", out, sizeof out));          // key idx
  TEST_ASSERT_EQUAL(0, encodeAction(0, KeyType::Keystroke, "a", out, sizeof out));     // not a helper type
  TEST_ASSERT_EQUAL(0, encodeAction(0, KeyType::App, "x", out, 10));                    // cap
  memset(v, 'a', kKeyValueMax);
  v[kKeyValueMax] = 0;
  TEST_ASSERT_EQUAL(kReportSize, encodeAction(0, KeyType::App, v, out, sizeof out));   // 56 fits
}

void test_encode_mic() {
  uint8_t out[kReportSize];
  TEST_ASSERT_EQUAL(kReportSize, encodeMicDelta(-2, out, sizeof out));
  TEST_ASSERT_EQUAL_HEX8(MSG_MIC_DELTA, out[1]);
  TEST_ASSERT_EQUAL_UINT8(1, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFE, out[3]);
  TEST_ASSERT_EQUAL(0, encodeMicDelta(9, out, sizeof out));  // |steps| <= 8
  TEST_ASSERT_EQUAL(kReportSize, encodeMicMute(out, sizeof out));
  TEST_ASSERT_EQUAL_HEX8(MSG_MIC_MUTE, out[1]);
  TEST_ASSERT_EQUAL_UINT8(0, out[2]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_decode_hello_volume_mic);
  RUN_TEST(test_decode_rejects_bad_fields);
  RUN_TEST(test_decode_garbage_never_crashes_or_accepts_out_of_range);
  RUN_TEST(test_encode_action);
  RUN_TEST(test_encode_action_rejects);
  RUN_TEST(test_encode_mic);
  return UNITY_END();
}
```
Stub `helper_proto.h` with the Interfaces, `decodeHost` returning false, encoders returning 0.

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_proto`
Expected: FAIL in `test_decode_hello_volume_mic`, `test_encode_action`, `test_encode_mic`.

- [ ] **Step 3: Implement `lib/deck_core/src/helper_proto.h`**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "keymap.h"

// Deck <-> Mac helper messages on the vendor HID interface (63-byte reports).
// [ver, type, len, payload...], zero padded. Host->deck messages can only update
// displayed state; actions only flow deck->host, from a physical key press.
constexpr size_t kReportSize = 63;
constexpr size_t kPayloadMax = kReportSize - 3;
constexpr uint8_t kProtoVersion = 1;
enum : uint8_t {
  MSG_HELLO = 0x01,
  MSG_VOLUME = 0x02,
  MSG_MIC = 0x03,
  MSG_ACTION = 0x81,
  MSG_MIC_DELTA = 0x82,
  MSG_MIC_MUTE = 0x83,
};

struct HostMsg {
  uint8_t type;
  uint8_t a;  // HELLO: helper version; VOLUME/MIC: percent 0..100
  uint8_t b;  // VOLUME/MIC: muted 0/1
};

inline bool decodeHost(const uint8_t *buf, size_t n, HostMsg &out) {
  if (n < 3 || buf[0] != kProtoVersion) return false;
  uint8_t type = buf[1], len = buf[2];
  if (len > kPayloadMax || 3u + len > n) return false;
  const uint8_t *p = buf + 3;
  switch (type) {
    case MSG_HELLO:
      if (len != 1) return false;
      out = {type, p[0], 0};
      return true;
    case MSG_VOLUME:
    case MSG_MIC:
      if (len != 2 || p[0] > 100 || p[1] > 1) return false;
      out = {type, p[0], p[1]};
      return true;
    default:
      return false;
  }
}

namespace proto_detail {
inline size_t frame(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t cap) {
  if (cap < kReportSize || len > kPayloadMax) return 0;
  memset(out, 0, kReportSize);
  out[0] = kProtoVersion;
  out[1] = type;
  out[2] = static_cast<uint8_t>(len);
  if (len) memcpy(out + 3, payload, len);
  return kReportSize;
}
}  // namespace proto_detail

inline size_t encodeAction(uint8_t key, KeyType t, const char *value, uint8_t *out, size_t cap) {
  if (key >= kKeyCount) return 0;
  if (t != KeyType::App && t != KeyType::Url && t != KeyType::Shortcut) return 0;
  size_t vlen = strnlen(value, kKeyValueMax + 1);
  if (vlen == 0 || vlen > kKeyValueMax) return 0;
  uint8_t p[kPayloadMax];
  p[0] = key;
  p[1] = static_cast<uint8_t>(t);
  p[2] = static_cast<uint8_t>(vlen);
  memcpy(p + 3, value, vlen);
  return proto_detail::frame(MSG_ACTION, p, 3 + vlen, out, cap);
}

inline size_t encodeMicDelta(int8_t steps, uint8_t *out, size_t cap) {
  if (steps < -8 || steps > 8 || steps == 0) return 0;
  uint8_t p[1] = {static_cast<uint8_t>(steps)};
  return proto_detail::frame(MSG_MIC_DELTA, p, 1, out, cap);
}

inline size_t encodeMicMute(uint8_t *out, size_t cap) {
  return proto_detail::frame(MSG_MIC_MUTE, nullptr, 0, out, cap);
}
```

- [ ] **Step 4: Run green**

Run: `$P test -e native -f native/test_proto`
Expected: `6 test cases: 6 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/helper_proto.h test/native/test_proto
git commit -m "HelperProto: vendor HID message format, strict decoder, fuzz test

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: HostLink (helper presence + host state)

**Files:**
- Create: `lib/deck_core/src/host_link.h`
- Test: `test/native/test_proto/test_main.cpp` (append)

**Interfaces:**
- Consumes: `HostMsg`, `MSG_*` (Task 7).
- Produces:
```cpp
class HostLink {
 public:
  static constexpr uint32_t kTimeoutMs = 3000;
  void onMessage(const HostMsg &m, uint32_t nowMs);
  void onUsbReset();
  bool present(uint32_t nowMs) const;
  bool hasVolume() const; uint8_t volumePercent() const; bool volumeMuted() const;
  bool hasMic() const;    uint8_t micPercent() const;    bool micMuted() const;
};
```

- [ ] **Step 1: Append failing tests** (add `#include "host_link.h"`)

```cpp
// --- HostLink ---

void test_link_absent_until_hello() {
  HostLink l;
  TEST_ASSERT_FALSE(l.present(0));
  l.onMessage({MSG_VOLUME, 50, 0}, 10);  // state without HELLO does not mean present
  TEST_ASSERT_FALSE(l.present(10));
  l.onMessage({MSG_HELLO, 1, 0}, 20);
  TEST_ASSERT_TRUE(l.present(20));
}

void test_link_times_out_after_3s_silence() {
  HostLink l;
  l.onMessage({MSG_HELLO, 1, 0}, 1000);
  TEST_ASSERT_TRUE(l.present(1000 + HostLink::kTimeoutMs - 1));
  TEST_ASSERT_FALSE(l.present(1000 + HostLink::kTimeoutMs));
  l.onMessage({MSG_HELLO, 1, 0}, 5000);
  TEST_ASSERT_TRUE(l.present(5000));
}

void test_link_any_message_keeps_alive() {
  HostLink l;
  l.onMessage({MSG_HELLO, 1, 0}, 0);
  l.onMessage({MSG_MIC, 30, 1}, 2500);
  TEST_ASSERT_TRUE(l.present(5000));
}

void test_link_stores_volume_and_mic() {
  HostLink l;
  TEST_ASSERT_FALSE(l.hasVolume());
  l.onMessage({MSG_HELLO, 1, 0}, 0);
  l.onMessage({MSG_VOLUME, 42, 1}, 1);
  l.onMessage({MSG_MIC, 75, 0}, 2);
  TEST_ASSERT_TRUE(l.hasVolume() && l.hasMic());
  TEST_ASSERT_EQUAL_UINT8(42, l.volumePercent());
  TEST_ASSERT_TRUE(l.volumeMuted());
  TEST_ASSERT_EQUAL_UINT8(75, l.micPercent());
  TEST_ASSERT_FALSE(l.micMuted());
}

void test_link_usb_reset_clears_everything() {
  HostLink l;
  l.onMessage({MSG_HELLO, 1, 0}, 0);
  l.onMessage({MSG_VOLUME, 42, 0}, 1);
  l.onUsbReset();
  TEST_ASSERT_FALSE(l.present(2));
  TEST_ASSERT_FALSE(l.hasVolume());
}

void test_link_millis_wrap() {
  HostLink l;
  l.onMessage({MSG_HELLO, 1, 0}, 0xFFFFFF00u);
  TEST_ASSERT_TRUE(l.present(0x00000100u));  // 512 ms later across wrap
}
```
Stub: methods return false/0, `onMessage` empty.

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_proto`
Expected: FAIL `test_link_absent_until_hello` (`Expected TRUE Was FALSE`), etc.

- [ ] **Step 3: Implement `lib/deck_core/src/host_link.h`**

```cpp
#pragma once
#include <stdint.h>
#include "helper_proto.h"

// What the deck knows about the Mac helper: is it running (HELLO keep-alive every second,
// gone after kTimeoutMs of silence or a USB reset) and the last real volume / mic state.
class HostLink {
 public:
  static constexpr uint32_t kTimeoutMs = 3000;

  void onMessage(const HostMsg &m, uint32_t nowMs) {
    if (m.type == MSG_HELLO) hello_ = true;
    lastMs_ = nowMs;
    if (m.type == MSG_VOLUME) { vol_ = m.a; volMuted_ = m.b; hasVol_ = true; }
    if (m.type == MSG_MIC) { mic_ = m.a; micMuted_ = m.b; hasMic_ = true; }
  }

  void onUsbReset() { *this = HostLink(); }

  bool present(uint32_t nowMs) const { return hello_ && nowMs - lastMs_ < kTimeoutMs; }
  bool hasVolume() const { return hasVol_; }
  uint8_t volumePercent() const { return vol_; }
  bool volumeMuted() const { return volMuted_; }
  bool hasMic() const { return hasMic_; }
  uint8_t micPercent() const { return mic_; }
  bool micMuted() const { return micMuted_; }

 private:
  bool hello_ = false, hasVol_ = false, hasMic_ = false, volMuted_ = false, micMuted_ = false;
  uint8_t vol_ = 0, mic_ = 0;
  uint32_t lastMs_ = 0;
};
```

- [ ] **Step 4: Run green**

Run: `$P test -e native -f native/test_proto`
Expected: `12 test cases: 12 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/host_link.h test/native/test_proto
git commit -m "HostLink: helper presence with 3 s timeout, last volume/mic state

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: WifiSession state machine

**Files:**
- Create: `lib/deck_core/src/wifi_session.h`
- Test: `test/native/test_wifi/test_main.cpp` (new suite)

**Interfaces:**
- Produces:
```cpp
enum class WifiState : uint8_t { Off, Starting, Portal, Joined };
class WifiSession {
 public:
  static constexpr uint32_t kOnMs = 600000;
  void toggle(uint32_t nowMs);   // Off -> Starting; any on-state -> Off
  void onPortal();               // Starting -> Portal
  void onJoined();               // Starting|Portal -> Joined
  void onFailed();               // any -> Off
  bool tick(uint32_t nowMs);     // true once when auto-off fires
  WifiState state() const;
  bool on() const;               // state != Off
};
```

- [ ] **Step 1: Write failing tests** (`test/native/test_wifi/test_main.cpp`)

```cpp
#include <unity.h>
#include "wifi_session.h"

void setUp() {}
void tearDown() {}

void test_wifi_starts_off() {
  WifiSession w;
  TEST_ASSERT_TRUE(w.state() == WifiState::Off);
  TEST_ASSERT_FALSE(w.on());
}

void test_wifi_toggle_on_then_states() {
  WifiSession w;
  w.toggle(0);
  TEST_ASSERT_TRUE(w.state() == WifiState::Starting);
  w.onPortal();
  TEST_ASSERT_TRUE(w.state() == WifiState::Portal);
  w.onJoined();
  TEST_ASSERT_TRUE(w.state() == WifiState::Joined);
  TEST_ASSERT_TRUE(w.on());
}

void test_wifi_toggle_again_turns_off() {
  WifiSession w;
  w.toggle(0);
  w.onJoined();
  w.toggle(100);
  TEST_ASSERT_TRUE(w.state() == WifiState::Off);
}

void test_wifi_auto_off_after_10_min_once() {
  WifiSession w;
  w.toggle(1000);
  w.onJoined();
  TEST_ASSERT_FALSE(w.tick(1000 + WifiSession::kOnMs - 1));
  TEST_ASSERT_TRUE(w.tick(1000 + WifiSession::kOnMs));
  TEST_ASSERT_TRUE(w.state() == WifiState::Off);
  TEST_ASSERT_FALSE(w.tick(1000 + WifiSession::kOnMs + 1));
}

void test_wifi_failed_goes_off() {
  WifiSession w;
  w.toggle(0);
  w.onFailed();
  TEST_ASSERT_FALSE(w.on());
}

void test_wifi_events_ignored_when_off() {
  WifiSession w;
  w.onJoined();
  w.onPortal();
  TEST_ASSERT_TRUE(w.state() == WifiState::Off);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_wifi_starts_off);
  RUN_TEST(test_wifi_toggle_on_then_states);
  RUN_TEST(test_wifi_toggle_again_turns_off);
  RUN_TEST(test_wifi_auto_off_after_10_min_once);
  RUN_TEST(test_wifi_failed_goes_off);
  RUN_TEST(test_wifi_events_ignored_when_off);
  return UNITY_END();
}
```
Stub: all methods no-ops, `state()` returns `Off`, `tick` returns false.

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_wifi`
Expected: FAIL `test_wifi_toggle_on_then_states`, `test_wifi_auto_off_after_10_min_once`.

- [ ] **Step 3: Implement `lib/deck_core/src/wifi_session.h`**

```cpp
#pragma once
#include <stdint.h>

enum class WifiState : uint8_t { Off, Starting, Portal, Joined };

// Wi-Fi is off by default; a 5 s two-knob hold toggles it, and it always turns itself off
// after kOnMs so a forgotten session doesn't leave the portal (and the radio) up.
class WifiSession {
 public:
  static constexpr uint32_t kOnMs = 600000;

  void toggle(uint32_t nowMs) {
    if (state_ == WifiState::Off) {
      state_ = WifiState::Starting;
      since_ = nowMs;
    } else {
      state_ = WifiState::Off;
    }
  }
  void onPortal() { if (state_ == WifiState::Starting) state_ = WifiState::Portal; }
  void onJoined() { if (state_ == WifiState::Starting || state_ == WifiState::Portal) state_ = WifiState::Joined; }
  void onFailed() { state_ = WifiState::Off; }

  bool tick(uint32_t nowMs) {
    if (state_ != WifiState::Off && nowMs - since_ >= kOnMs) {
      state_ = WifiState::Off;
      return true;
    }
    return false;
  }

  WifiState state() const { return state_; }
  bool on() const { return state_ != WifiState::Off; }

 private:
  WifiState state_ = WifiState::Off;
  uint32_t since_ = 0;
};
```

- [ ] **Step 4: Run green**

Run: `$P test -e native -f native/test_wifi`
Expected: `6 test cases: 6 succeeded`.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/wifi_session.h test/native/test_wifi
git commit -m "WifiSession: off by default, toggle, 10 min auto-off

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Deck routing — keymap, helper presence, Wi-Fi chord

**Files:**
- Modify: `lib/deck_core/src/deck.h`
- Modify: `lib/deck_core/src/led_effects.h` (new enum values in switch)
- Modify: `src/main.cpp` (new enum values in switch, temporary no-op cases)
- Test: `test/native/test_deck/test_main.cpp` (append)

**Interfaces:**
- Consumes: `KeyMap`, `KeyType` (Task 4), `CHORD_WIFI` (Task 6).
- Produces (added to `ActionType`, in this order after `Sync`): `HelperAction, HelperMissing, HelperMicMute, MicUp, MicDown, WifiToggle, KeyMapCommitted, KeyMapDiscarded`. `Action.value` = key index for `KeyDown/KeyUp/HelperAction/HelperMissing`.
- Produces on `Deck`: `void setKeyMap(const KeyMap &m)`, `const KeyMap &keyMap() const`, `void setHelperPresent(bool p)`, `uint32_t chordHeldMs(uint32_t nowMs) const`.
- Routing: key with `Keystroke` entry → `KeyDown i` / `KeyUp i`; other types → on press only: helper present → `HelperAction i`, else `HelperMissing i`; release → nothing. `CHORD_WIFI` → `WifiToggle`.

- [ ] **Step 1: Append failing tests**

```cpp
// --- keymap routing / helper / wifi ---

static Deck deckWithApp(uint8_t key) {
  Deck d;
  KeyMap m;
  m.set(key, KeyType::App, "Calculator");
  d.setKeyMap(m);
  return d;
}

void test_deck_keystroke_entry_keeps_down_up() {
  Deck d;
  uint32_t t = 0;
  ActionList a = hold(d, 1u << 0, t);
  TEST_ASSERT_TRUE(has(a, ActionType::KeyDown, 0));
  a = hold(d, 0, t);
  TEST_ASSERT_TRUE(has(a, ActionType::KeyUp, 0));
}

void test_deck_app_key_with_helper_sends_helper_action_on_press_only() {
  Deck d = deckWithApp(3);
  d.setHelperPresent(true);
  uint32_t t = 0;
  ActionList a = hold(d, 1u << 3, t);
  TEST_ASSERT_EQUAL(1, a.count);
  TEST_ASSERT_TRUE(has(a, ActionType::HelperAction, 3));
  a = hold(d, 0, t);
  TEST_ASSERT_EQUAL(0, a.count);
}

void test_deck_app_key_without_helper_reports_missing() {
  Deck d = deckWithApp(3);
  uint32_t t = 0;
  ActionList a = hold(d, 1u << 3, t);
  TEST_ASSERT_EQUAL(1, a.count);
  TEST_ASSERT_TRUE(has(a, ActionType::HelperMissing, 3));
}

void test_deck_wifi_chord_emits_toggle_once() {
  Deck d;
  uint32_t t = 0;
  ActionList a;
  d.onKeys(DECK_ENC1_SW | DECK_ENC2_SW, t, a);
  t += 30;
  d.onKeys(DECK_ENC1_SW | DECK_ENC2_SW, t, a);  // debounced: chord starts at t=30
  t += KnobChord::kWifiHoldMs;
  d.onKeys(DECK_ENC1_SW | DECK_ENC2_SW, t, a);
  TEST_ASSERT_TRUE(has(a, ActionType::WifiToggle));
  TEST_ASSERT_TRUE(d.chordHeldMs(t) >= KnobChord::kWifiHoldMs);
  ActionList b = hold(d, 0, t);
  TEST_ASSERT_FALSE(has(b, ActionType::Sync));
}
```
Add RUN_TEST lines. Stub step: add the new `ActionType` values; add `setKeyMap`, `keyMap`, `setHelperPresent`, `chordHeldMs` as no-ops returning defaults; add the new values to the `switch` in `LedEffects::onAction` (`break;`) and in `src/main.cpp` `run()` (`break;`).

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_deck`
Expected: FAIL `test_deck_app_key_with_helper_sends_helper_action_on_press_only` (`Expected 1 ... KeyDown`), `test_deck_wifi_chord_emits_toggle_once`.

- [ ] **Step 3: Implement in `lib/deck_core/src/deck.h`**

Add `#include "keymap.h"`. Replace the key loop in `onKeys` with:
```cpp
    for (uint8_t i = 0; i < 12; i++) {
      uint16_t bit = 1u << i;
      if (!(changed & bit)) continue;
      bool down = now & bit;
      if (keymap_.entry(i).type == KeyType::Keystroke) {
        out.push(down ? ActionType::KeyDown : ActionType::KeyUp, i);
      } else if (down) {
        out.push(helper_ ? ActionType::HelperAction : ActionType::HelperMissing, i);
      }
    }
```
After the `CHORD_SYNC` block add:
```cpp
    if (chord & CHORD_WIFI) out.push(ActionType::WifiToggle);
```
Public members:
```cpp
  void setKeyMap(const KeyMap &m) { keymap_ = m; }
  const KeyMap &keyMap() const { return keymap_; }
  void setHelperPresent(bool p) { helper_ = p; }
  uint32_t chordHeldMs(uint32_t nowMs) const { return chord_.bothHeldMs(nowMs); }
```
Private members: `KeyMap keymap_; bool helper_ = false;`.

Note: a `KeyUp` must still be sent for a `Keystroke` key whose entry changes while held — handled in Task 12 (`KeyMapCommitted` sends `ReleaseAll` first).

- [ ] **Step 4: Run green + build**

Run: `$P test -e native && $P run -e saucedeck -e bringup 2>&1 | grep -cE 'warning:'`
Expected: all suites pass; `0` warnings.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/deck.h lib/deck_core/src/led_effects.h src/main.cpp test/native/test_deck
git commit -m "Deck: route keys by KeyMap, helper presence, Wi-Fi chord action

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Deck — knob 2 gesture (push+turn mic) and mic routing

**Files:**
- Modify: `lib/deck_core/src/deck.h`, `src/main.cpp` (call site `onEncoders` gains `nowMs`)
- Test: `test/native/test_deck/test_main.cpp`

**Interfaces:**
- Produces: `void Deck::onEncoders(int knob1, int knob2, uint32_t nowMs, ActionList &out)` (signature change); `Deck::kMicTurnAfterMs = 150`.
- Rules: knob 2 held ≥ 150 ms + turned → one `MicUp`/`MicDown` per detent if helper present, else one `HelperMissing` (value 0xFF) per gesture; the release then fires nothing. Turns in the first 150 ms are ignored. Knob 2 not held → tracks (unchanged). Knob 2 push+release without qualifying turn → helper present: `HelperMicMute`; absent: `MicMute` (existing, toggles local `micMuted_`).

- [ ] **Step 1: Update call sites and append failing tests**

Replace every `d.onEncoders(x, y, a)` in `test/native/test_deck/test_main.cpp` with `d.onEncoders(x, y, 0, a)` (and `e.onEncoders(...)` likewise). Append:
```cpp
// --- knob 2 gesture ---

static void pressKnob2(Deck &d, uint32_t &t) {
  ActionList a;
  d.onKeys(DECK_ENC2_SW, t, a);
  t += 30;
  d.onKeys(DECK_ENC2_SW, t, a);  // debounced press at t
}

void test_deck_knob2_push_turn_with_helper_sends_mic_steps() {
  Deck d;
  d.setHelperPresent(true);
  uint32_t t = 0;
  pressKnob2(d, t);
  t += Deck::kMicTurnAfterMs;
  ActionList a;
  d.onEncoders(0, 2, t, a);
  d.onEncoders(0, -1, t, a);
  TEST_ASSERT_EQUAL(3, a.count);
  TEST_ASSERT_TRUE(a.items[0].type == ActionType::MicUp);
  TEST_ASSERT_TRUE(a.items[2].type == ActionType::MicDown);
  ActionList r = hold(d, 0, t);
  TEST_ASSERT_EQUAL(0, r.count);  // release after a turn: no mute
}

void test_deck_knob2_turn_in_first_150ms_ignored_and_still_mutes() {
  Deck d;
  d.setHelperPresent(true);
  uint32_t t = 0;
  pressKnob2(d, t);
  ActionList a;
  d.onEncoders(0, 1, t + Deck::kMicTurnAfterMs - 1, a);  // shaft nudge while pressing
  TEST_ASSERT_EQUAL(0, a.count);
  ActionList r = hold(d, 0, t);
  TEST_ASSERT_TRUE(has(r, ActionType::HelperMicMute));
}

void test_deck_knob2_push_turn_without_helper_reports_missing_once() {
  Deck d;
  uint32_t t = 0;
  pressKnob2(d, t);
  t += Deck::kMicTurnAfterMs;
  ActionList a;
  d.onEncoders(0, 1, t, a);
  d.onEncoders(0, 1, t, a);
  TEST_ASSERT_EQUAL(1, a.count);
  TEST_ASSERT_TRUE(has(a, ActionType::HelperMissing, 0xFF));
  ActionList r = hold(d, 0, t);
  TEST_ASSERT_EQUAL(0, r.count);
}

void test_deck_knob2_mute_without_helper_uses_local_chord() {
  Deck d;
  uint32_t t = 0;
  hold(d, DECK_ENC2_SW, t);
  ActionList r = hold(d, 0, t);
  TEST_ASSERT_TRUE(has(r, ActionType::MicMute));
  TEST_ASSERT_TRUE(d.micMuted());
}

void test_deck_knob2_turn_not_held_still_skips_tracks() {
  Deck d;
  d.setHelperPresent(true);
  ActionList a;
  d.onEncoders(0, 1, 0, a);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)ConsumerCode::NextTrack, a.items[0].value);
}

void test_deck_knob1_turn_while_knob2_held_is_volume() {
  Deck d;
  d.setHelperPresent(true);
  uint32_t t = 0;
  pressKnob2(d, t);
  t += Deck::kMicTurnAfterMs;
  ActionList a;
  d.onEncoders(1, 0, t, a);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)ConsumerCode::VolumeUp, a.items[0].value);
  ActionList r = hold(d, 0, t);
  TEST_ASSERT_TRUE(has(r, ActionType::HelperMicMute));  // knob 2 itself was not turned
}
```
Change `void onEncoders(int knob1, int knob2, ActionList &out)` to take `uint32_t nowMs` (unused in stub). In `src/main.cpp` call `deck.onEncoders(d1, d2, millis(), actions);`.

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_deck`
Expected: FAIL `test_deck_knob2_push_turn_with_helper_sends_mic_steps` (tracks emitted instead).

- [ ] **Step 3: Implement in `lib/deck_core/src/deck.h`**

In `onKeys`, before `held_ = now;`:
```cpp
    bool k2 = now & DECK_ENC2_SW;
    if (k2 && !(held_ & DECK_ENC2_SW)) {
      k2Since_ = nowMs;
      k2Turned_ = false;
      k2MissingSent_ = false;
    }
```
Replace the `CHORD_KNOB2` block:
```cpp
    if ((chord & CHORD_KNOB2) && !k2Turned_) {
      if (helper_) {
        out.push(ActionType::HelperMicMute);
      } else {
        out.push(ActionType::MicMute);
        micMuted_ = !micMuted_;
      }
    }
```
Replace `onEncoders`:
```cpp
  static constexpr uint32_t kMicTurnAfterMs = 150;

  void onEncoders(int knob1, int knob2, uint32_t nowMs, ActionList &out) {
    int room = static_cast<int>(ActionList::kMax - out.count);
    if (knob1 > room) knob1 = room;
    if (knob1 < -room) knob1 = -room;
    speaker_.rotate(knob1);
    pushSteps(knob1, ConsumerCode::VolumeUp, ConsumerCode::VolumeDown, out);

    if (!(held_ & DECK_ENC2_SW)) {
      pushSteps(knob2, ConsumerCode::NextTrack, ConsumerCode::PrevTrack, out);
      return;
    }
    if (knob2 == 0 || nowMs - k2Since_ < kMicTurnAfterMs) return;  // shaft nudge while pressing
    k2Turned_ = true;
    if (!helper_) {
      if (!k2MissingSent_) out.push(ActionType::HelperMissing, 0xFF);
      k2MissingSent_ = true;
      return;
    }
    for (; knob2 > 0; knob2--) out.push(ActionType::MicUp);
    for (; knob2 < 0; knob2++) out.push(ActionType::MicDown);
  }
```
In `onBusLost` add `k2Turned_ = false;`. Private: `uint32_t k2Since_ = 0; bool k2Turned_ = false, k2MissingSent_ = false;`.

- [ ] **Step 4: Run green + build**

Run: `$P test -e native && $P run -e saucedeck -e bringup 2>&1 | grep -cE 'warning:'`
Expected: all pass; `0`.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/deck.h src/main.cpp test/native/test_deck
git commit -m "Deck: knob 2 push+turn = mic level (150 ms guard), helper-aware mic mute

Not bench-verified (150 ms guard needs bench tuning).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Deck — staged key map with on-deck confirm

**Files:**
- Modify: `lib/deck_core/src/deck.h`
- Test: `test/native/test_deck/test_main.cpp`

**Interfaces:**
- Produces: `Deck::kConfirmMs = 30000`; `void Deck::stage(const KeyMap &m, uint32_t nowMs)`; `bool Deck::pending() const`; `uint32_t Deck::pendingMsLeft(uint32_t nowMs) const`.
- Rules: while pending, `CHORD_KNOB1` commits (`ReleaseAll` then `KeyMapCommitted`, new map live) instead of mute. Timeout in `onKeys` → `KeyMapDiscarded`. A new `stage` replaces the previous one and restarts the window.

- [ ] **Step 1: Append failing tests**

```cpp
// --- staged key map ---

void test_deck_stage_then_knob1_commits_without_mute() {
  Deck d;
  KeyMap m;
  m.set(0, KeyType::App, "Notes");
  uint32_t t = 0;
  d.stage(m, t);
  TEST_ASSERT_TRUE(d.pending());
  hold(d, DECK_ENC1_SW, t);
  ActionList a = hold(d, 0, t);
  TEST_ASSERT_TRUE(has(a, ActionType::ReleaseAll));
  TEST_ASSERT_TRUE(has(a, ActionType::KeyMapCommitted));
  TEST_ASSERT_FALSE(has(a, ActionType::Consumer, (uint8_t)ConsumerCode::Mute));
  TEST_ASSERT_FALSE(d.speaker().muted());
  TEST_ASSERT_FALSE(d.pending());
  TEST_ASSERT_TRUE(d.keyMap().entry(0).type == KeyType::App);
}

void test_deck_stage_times_out_and_discards() {
  Deck d;
  KeyMap m;
  m.set(0, KeyType::App, "Notes");
  d.stage(m, 1000);
  ActionList a;
  d.onKeys(0, 1000 + Deck::kConfirmMs - 1, a);
  TEST_ASSERT_TRUE(d.pending());
  d.onKeys(0, 1000 + Deck::kConfirmMs, a);
  TEST_ASSERT_TRUE(has(a, ActionType::KeyMapDiscarded));
  TEST_ASSERT_FALSE(d.pending());
  TEST_ASSERT_TRUE(d.keyMap().entry(0).type == KeyType::Keystroke);
}

void test_deck_new_stage_replaces_and_restarts_window() {
  Deck d;
  KeyMap a1, a2;
  a1.set(0, KeyType::App, "Notes");
  a2.set(0, KeyType::App, "Mail");
  d.stage(a1, 0);
  d.stage(a2, 20000);
  TEST_ASSERT_EQUAL_UINT32(Deck::kConfirmMs - 5000, d.pendingMsLeft(25000));
  uint32_t t = 25000;
  hold(d, DECK_ENC1_SW, t);
  hold(d, 0, t);
  TEST_ASSERT_EQUAL_STRING("Mail", d.keyMap().entry(0).value);
}

void test_deck_knob1_mutes_again_when_nothing_pending() {
  Deck d;
  uint32_t t = 0;
  hold(d, DECK_ENC1_SW, t);
  ActionList a = hold(d, 0, t);
  TEST_ASSERT_TRUE(has(a, ActionType::Consumer, (uint8_t)ConsumerCode::Mute));
}
```
Stub `stage`, `pending` (false), `pendingMsLeft` (0).

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_deck`
Expected: FAIL `test_deck_stage_then_knob1_commits_without_mute`.

- [ ] **Step 3: Implement in `lib/deck_core/src/deck.h`**

At the top of `onKeys` (after `failing_ = false;`):
```cpp
    if (pending_ && nowMs - stagedAt_ >= kConfirmMs) {
      pending_ = false;
      out.push(ActionType::KeyMapDiscarded);
    }
```
Replace the `CHORD_KNOB1` block:
```cpp
    if (chord & CHORD_KNOB1) {
      if (pending_) {
        out.push(ActionType::ReleaseAll);  // keys held under the old map must not stick
        keymap_ = staged_;
        held_ &= ~DECK_KEYS;
        pending_ = false;
        out.push(ActionType::KeyMapCommitted);
      } else {
        out.push(ActionType::Consumer, static_cast<uint8_t>(ConsumerCode::Mute));
        speaker_.toggleMute();
      }
    }
```
Public:
```cpp
  static constexpr uint32_t kConfirmMs = 30000;
  void stage(const KeyMap &m, uint32_t nowMs) { staged_ = m; stagedAt_ = nowMs; pending_ = true; }
  bool pending() const { return pending_; }
  uint32_t pendingMsLeft(uint32_t nowMs) const {
    if (!pending_) return 0;
    uint32_t e = nowMs - stagedAt_;
    return e >= kConfirmMs ? 0 : kConfirmMs - e;
  }
```
Private: `KeyMap staged_; uint32_t stagedAt_ = 0; bool pending_ = false;`.

Note `held_ &= ~DECK_KEYS` must run after `held_ = now;` in the same call — place the `CHORD_KNOB1` handling after that assignment (it already is: chord handling follows `held_ = now;`). Keys still physically held are pressed again on the next scan under the new map (their old HID keys were released by `ReleaseAll`).

- [ ] **Step 4: Run green + build**

Run: `$P test -e native && $P run -e saucedeck -e bringup 2>&1 | grep -cE 'warning:'`
Expected: all pass; `0`.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/deck.h test/native/test_deck
git commit -m "Deck: staged key map, knob 1 confirms within 30 s, else discarded

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: LED Wi-Fi mode + screen banner/mic level (deck_core)

**Files:**
- Modify: `lib/deck_core/src/led_effects.h`, `lib/deck_core/src/ui_layout.h`
- Test: `test/native/test_led/test_main.cpp`, `test/native/test_ui/test_main.cpp`

**Interfaces:**
- Produces on `LedEffects`: `static constexpr uint32_t kWifiBudgetMa = 30; void setWifi(bool on);`. New action handling: `HelperAction` → key colour flash; `HelperMissing` → orange `{255,40,0}`; `HelperMicMute` → red; `KeyMapCommitted` → green; others no flash.
- Produces on `ScreenState` (append fields): `bool micLevelKnown; uint8_t micPercent; uint8_t banner; uint8_t bannerArg;` and `enum : uint8_t { BANNER_NONE, BANNER_WIFI_COUNTDOWN, BANNER_WIFI_STARTING, BANNER_WIFI_PORTAL, BANNER_WIFI_JOINED, BANNER_APPLY, BANNER_HELPER_MISSING, BANNER_KEYMAP_RESET, BANNER_WIFI_FAILED, BANNER_INVALID_KEY, BANNER_BROWNOUT };` plus `DIRTY_BANNER = 1u << 5`, `DIRTY_ALL = 0x3F`.

- [ ] **Step 1: Failing tests**

`test/native/test_led/test_main.cpp` append:
```cpp
void test_wifi_mode_caps_and_shows_only_indicator() {
  LedEffects e;
  e.setWifi(true);
  e.onAction({ActionType::KeyDown, 0}, 0);  // flash suppressed in Wi-Fi mode
  Rgb px[N];
  for (uint32_t t = 0; t < 2000; t += 50) {
    e.render(kIdle, t, px, N);
    TEST_ASSERT_TRUE(estimateMilliamps(px, N) <= LedEffects::kWifiBudgetMa);
    for (size_t i = 1; i < N; i++) TEST_ASSERT_TRUE(isOff(px[i]));
    TEST_ASSERT_EQUAL_UINT8(0, px[0].r);
  }
}

void test_wifi_indicator_pulses() {
  LedEffects e;
  e.setWifi(true);
  Rgb a[N], b[N];
  e.render(kIdle, 0, a, N);
  e.render(kIdle, 500, b, N);
  TEST_ASSERT_TRUE(a[0].b != b[0].b);
}

void test_helper_missing_flashes_orange() {
  LedEffects e;
  Rgb px[N];
  e.onAction({ActionType::HelperMissing, 3}, 0);
  e.render(kIdle, 0, px, N);
  TEST_ASSERT_TRUE(px[0].r > 150 && px[0].g < 80);
}
```
`test/native/test_ui/test_main.cpp`: update every `m.update({a, b, c})` to `m.update({a, b, c, false, 0, BANNER_NONE, 0})`, and append:
```cpp
void test_model_mic_level_change_marks_mic() {
  ScreenModel m;
  m.update({20, false, false, true, 40, BANNER_NONE, 0});
  TEST_ASSERT_EQUAL_HEX8(DIRTY_MIC, m.update({20, false, false, true, 45, BANNER_NONE, 0}));
}

void test_model_banner_change_marks_banner() {
  ScreenModel m;
  m.update({20, false, false, false, 0, BANNER_NONE, 0});
  TEST_ASSERT_EQUAL_HEX8(DIRTY_BANNER, m.update({20, false, false, false, 0, BANNER_APPLY, 30}));
  TEST_ASSERT_EQUAL_HEX8(DIRTY_BANNER, m.update({20, false, false, false, 0, BANNER_APPLY, 29}));
}
```
Update `test_model_first_frame_all_dirty` / `invalidate` expectations to the new `DIRTY_ALL` (`0x3F`) automatically via the constant. Stubs: `setWifi` no-op; new ScreenState fields added; `DIRTY_BANNER` defined; no logic.

- [ ] **Step 2: Run red**

Run: `$P test -e native -f native/test_led -f native/test_ui`
Expected: FAIL `test_wifi_mode_caps_and_shows_only_indicator`, `test_helper_missing_flashes_orange`, `test_model_mic_level_change_marks_mic`, `test_model_banner_change_marks_banner`.

- [ ] **Step 3: Implement**

`led_effects.h` — in `onAction` switch:
```cpp
      case ActionType::HelperAction: flash(kKeyColors[a.value % 12], nowMs); break;
      case ActionType::HelperMissing: flash({255, 40, 0}, nowMs); break;
      case ActionType::HelperMicMute: flash({255, 0, 0}, nowMs); break;
      case ActionType::KeyMapCommitted: flash({0, 255, 0}, nowMs); break;
      case ActionType::MicUp:
      case ActionType::MicDown:
      case ActionType::WifiToggle:
      case ActionType::KeyMapDiscarded: break;
```
Add:
```cpp
  static constexpr uint32_t kWifiBudgetMa = 30;
  void setWifi(bool on) { wifi_ = on; }
```
At the start of `render`, before the existing body:
```cpp
    if (wifi_) {  // Wi-Fi TX peaks need the USB budget: one slow blue pulse, nothing else
      uint32_t ph = nowMs % 1000;
      uint8_t lvl = static_cast<uint8_t>((ph < 500 ? ph : 1000 - ph) * 40 / 500);
      for (size_t i = 0; i < n; i++) px[i] = {0, 0, 0};
      if (n) px[0] = {0, 0, lvl};
      limitPower(px, n, kWifiBudgetMa);
      return;
    }
```
Private: `bool wifi_ = false;`.

`ui_layout.h` — `ScreenState` becomes:
```cpp
enum : uint8_t {
  BANNER_NONE, BANNER_WIFI_COUNTDOWN, BANNER_WIFI_STARTING, BANNER_WIFI_PORTAL, BANNER_WIFI_JOINED,
  BANNER_APPLY, BANNER_HELPER_MISSING, BANNER_KEYMAP_RESET, BANNER_WIFI_FAILED, BANNER_INVALID_KEY,
  BANNER_BROWNOUT,
};

struct ScreenState {
  uint8_t volumePercent;
  bool speakerMuted;
  bool micMuted;
  bool micLevelKnown;  // helper connected: show real mic %
  uint8_t micPercent;
  uint8_t banner;      // BANNER_*
  uint8_t bannerArg;   // countdown seconds, key number, ...
};
```
Add `DIRTY_BANNER = 1u << 5`, set `DIRTY_ALL = 0x3F`. In `ScreenModel::update` add:
```cpp
      if (next.micLevelKnown != cur_.micLevelKnown || next.micPercent != cur_.micPercent) dirty |= DIRTY_MIC;
      if (next.banner != cur_.banner || next.bannerArg != cur_.bannerArg) dirty |= DIRTY_BANNER;
```
Update `src/main.cpp` and `src/display.cpp` call sites to pass the 7-field `ScreenState` (`{pct, muted, micMuted, false, 0, BANNER_NONE, 0}` until Task 16).

- [ ] **Step 4: Run green + build**

Run: `$P test -e native && $P run -e saucedeck -e bringup 2>&1 | grep -cE 'warning:'`
Expected: all pass; `0`.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/led_effects.h lib/deck_core/src/ui_layout.h test/native/test_led test/native/test_ui src/main.cpp src/display.cpp
git commit -m "LED Wi-Fi mode (30 mA, blue pulse) + screen banner and mic level model

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: Glue — keymap execution + NVS store

**Files:**
- Create: `src/keymap_store.h`, `src/keymap_store.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `KeyMap`, `serializeKeyMap`, `deserializeKeyMap`, `kKeyMapBlobMax` (Task 5); `Deck::setKeyMap` (Task 10).
- Produces: `bool keymapLoad(KeyMap &out);` (false → defaults kept, caller shows `BANNER_KEYMAP_RESET` only if a blob existed), `bool keymapSave(const KeyMap &m);`, `bool keymapExists();`.

- [ ] **Step 1: Write `src/keymap_store.h` / `.cpp`**

```cpp
// keymap_store.h
#pragma once
#include "keymap.h"
bool keymapExists();
bool keymapLoad(KeyMap &out);
bool keymapSave(const KeyMap &m);
```
```cpp
// keymap_store.cpp — NVS via Preferences. Format/validation live in deck_core/keymap.
#include "keymap_store.h"
#include <Preferences.h>

static const char *kNs = "saucedeck";
static const char *kKey = "keymap";

bool keymapExists() {
  Preferences p;
  if (!p.begin(kNs, true)) return false;
  bool e = p.isKey(kKey);
  p.end();
  return e;
}

bool keymapLoad(KeyMap &out) {
  Preferences p;
  if (!p.begin(kNs, true)) return false;
  uint8_t buf[kKeyMapBlobMax];
  size_t n = p.getBytes(kKey, buf, sizeof buf);
  p.end();
  return n > 0 && deserializeKeyMap(buf, n, out);
}

bool keymapSave(const KeyMap &m) {
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  if (n == 0) return false;
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  bool ok = p.putBytes(kKey, buf, n) == n;
  p.end();
  return ok;
}
```

- [ ] **Step 2: Execute keystroke entries in `src/main.cpp`**

Remove `kButtonKeys`. Add `#include "keymap_store.h"`. Replace the `KeyDown`/`KeyUp` cases in `run()`:
```cpp
      case ActionType::KeyDown: {
        const Keystroke &k = deck.keyMap().entry(a.value).ks;
        for (uint8_t b = 0; b < 4; b++)
          if (k.mods & (1u << b)) Keyboard.pressRaw(0xE0 + b);  // HID modifier usages
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
      case ActionType::KeyMapCommitted:
        keymapSave(deck.keyMap());
        break;
```
In `setup()` before `displayBegin();`:
```cpp
  KeyMap stored;
  bool hadMap = keymapExists();
  bool loaded = keymapLoad(stored);
  if (loaded) deck.setKeyMap(stored);
  keymapResetNotice = hadMap && !loaded;  // blob present but corrupt -> defaults + notice
  brownoutNotice = esp_reset_reason() == ESP_RST_BROWNOUT;  // spec 8: show once after reboot
```
with `static bool keymapResetNotice = false; static bool brownoutNotice = false;` at file scope (used in Task 16) and `#include <esp_system.h>`.

- [ ] **Step 3: Build**

Run: `$P run -e saucedeck -e bringup 2>&1 | grep -cE 'warning:'; $P run -e saucedeck 2>&1 | tail -1`
Expected: `0`; `SUCCESS`.

- [ ] **Step 4: Bench (defaults unchanged)**

Flash: `$P run -e saucedeck -t upload --upload-port $(ls /dev/cu.usbmodem* | head -1)`.
Operator: press keys 1–11 → Karabiner-EventViewer shows F13–F23 (key 12 pending issue #1). Expected: same as before (defaults).

- [ ] **Step 5: Commit**

```bash
git add src/keymap_store.h src/keymap_store.cpp src/main.cpp
git commit -m "Execute keystroke entries via pressRaw; key map persisted in NVS

Bench: <bench-verified F13-F23 | not bench-verified>.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15: Glue — vendor HID helper link

**Files:**
- Create: `src/helper_hid.h`, `src/helper_hid.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `HostLink`, `decodeHost`, `encodeAction`, `encodeMicDelta`, `encodeMicMute` (Tasks 7–8); Task 2 result (report size 63, no permission).
- Produces: `void helperBegin(); void helperPoll(uint32_t nowMs); const HostLink &helperLink(); void helperSendAction(uint8_t key, const KeyEntry &e); void helperSendMicDelta(int8_t steps); void helperSendMicMute();`

- [ ] **Step 1: Write `src/helper_hid.h` / `.cpp`**

```cpp
// helper_hid.h
#pragma once
#include <stdint.h>
#include "host_link.h"
#include "keymap.h"
void helperBegin();
void helperPoll(uint32_t nowMs);
const HostLink &helperLink();
void helperSendAction(uint8_t key, const KeyEntry &e);
void helperSendMicDelta(int8_t steps);
void helperSendMicMute();
```
```cpp
// helper_hid.cpp — vendor-defined HID (usage page 0xFF00) to the Mac helper.
// Message rules live in deck_core/helper_proto.h; this file only moves bytes.
#include "helper_hid.h"
#include <Arduino.h>
#include "USB.h"
#include "USBHIDVendor.h"
#include "helper_proto.h"

// Global so it registers with the HID descriptor before the core starts USB.
USBHIDVendor Vendor(kReportSize, false);
static HostLink link;
static uint32_t rejected = 0;

void helperBegin() {
  Vendor.begin();
  USB.onEvent(ARDUINO_USB_STOPPED_EVENT, [](void *, esp_event_base_t, int32_t, void *) { link.onUsbReset(); });
}

void helperPoll(uint32_t nowMs) {
  while (Vendor.available() >= static_cast<int>(kReportSize)) {
    uint8_t buf[kReportSize];
    size_t n = Vendor.read(buf, sizeof buf);
    HostMsg m;
    if (decodeHost(buf, n, m)) link.onMessage(m, nowMs);
    else rejected++;
  }
}

const HostLink &helperLink() { return link; }

static void send(const uint8_t *buf, size_t n) {
  if (n) Vendor.write(buf, n);
}

void helperSendAction(uint8_t key, const KeyEntry &e) {
  uint8_t buf[kReportSize];
  send(buf, encodeAction(key, e.type, e.value, buf, sizeof buf));
}

void helperSendMicDelta(int8_t steps) {
  uint8_t buf[kReportSize];
  send(buf, encodeMicDelta(steps, buf, sizeof buf));
}

void helperSendMicMute() {
  uint8_t buf[kReportSize];
  send(buf, encodeMicMute(buf, sizeof buf));
}
```

- [ ] **Step 2: Wire into `src/main.cpp`**

Add `#include "helper_hid.h"`. In `setup()` after `Consumer.begin();`: `helperBegin();`. In `run()`:
```cpp
      case ActionType::HelperAction: helperSendAction(a.value, deck.keyMap().entry(a.value)); break;
      case ActionType::HelperMicMute: helperSendMicMute(); break;
      case ActionType::MicUp: helperSendMicDelta(1); break;
      case ActionType::MicDown: helperSendMicDelta(-1); break;
      case ActionType::HelperMissing: helperMissingUntil = millis() + 2000; break;
```
(`static uint32_t helperMissingUntil = 0;` at file scope, used in Task 16.) In `loop()` first lines:
```cpp
  uint32_t now = millis();
  helperPoll(now);
  deck.setHelperPresent(helperLink().present(now));
```
Display source in `loop()`:
```cpp
  const HostLink &hl = helperLink();
  bool live = hl.present(now) && hl.hasVolume();
  uint8_t pct = live ? hl.volumePercent() : spk.percent();
  bool spkMuted = live ? hl.volumeMuted() : spk.muted();
  bool micKnown = hl.present(now) && hl.hasMic();
  bool micMuted = micKnown ? hl.micMuted() : deck.micMuted();
  uint8_t micPct = micKnown ? hl.micPercent() : 0;
```
and pass `{pct, spkMuted, micMuted, micKnown, micPct, banner, bannerArg}` (banner from Task 16; `BANNER_NONE, 0` for now) to `displayUpdate`, `{pct, spkMuted, micMuted}` to `ledsUpdate`.

- [ ] **Step 3: Build**

Run: `$P run -e saucedeck -e bringup 2>&1 | grep -cE 'warning:'; $P run -e saucedeck 2>&1 | tail -1`
Expected: `0`; `SUCCESS`.

- [ ] **Step 4: Bench with the spike probe**

Flash production. Run `swift tools/spike/hidprobe.swift` (sends HELLO, no echo expected now: production doesn't echo). Expected: `IOHIDManagerOpen: OK`, `SetReport ... OK`; keyboard keys still work; LCD unchanged (no VOLUME sent). Full check happens with Plan 2's helper.

- [ ] **Step 5: Commit**

```bash
git add src/helper_hid.h src/helper_hid.cpp src/main.cpp
git commit -m "Vendor HID helper link: actions/mic out, volume/mic state in

Not bench-verified end to end (needs the Mac helper, Plan 2).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 16: Glue — Wi-Fi portal, banners, LCD prompts

**Files:**
- Create: `src/wifi_portal.h`, `src/wifi_portal.cpp`
- Modify: `src/main.cpp`, `src/display.cpp`, `platformio.ini` (lib_deps)

**Interfaces:**
- Consumes: `WifiSession` (Task 9), `Deck::stage/pending/pendingMsLeft/chordHeldMs` (Tasks 10, 12), `LedEffects::setWifi` via `ledsSetWifi` (new), `BANNER_*` (Task 13), Task 1 result.
- Produces: `bool portalStart(const KeyMap &current); void portalStop(); void portalPoll(); WifiState portalState(); bool portalTakeStaged(KeyMap &out, uint8_t &badKey); const char *portalApPassword();` and `void ledsSetWifi(bool on);` in `src/leds.h/.cpp`.

- [ ] **Step 1: Add the library**

`platformio.ini` `[env:saucedeck]` `lib_deps` add `tzapu/WiFiManager@2.0.17`.

- [ ] **Step 2: Write `src/wifi_portal.h` / `.cpp`**

```cpp
// wifi_portal.h
#pragma once
#include <stdint.h>
#include "keymap.h"
#include "wifi_session.h"
bool portalStart(const KeyMap &current);  // false = could not start (Wi-Fi failed)
void portalStop();
void portalPoll();
WifiState portalState();
bool portalTakeStaged(KeyMap &out, uint8_t &badKey);  // badKey = 1..12 if a field was invalid, 0 = ok
const char *portalApPassword();
```
```cpp
// wifi_portal.cpp — the ONLY file with Wi-Fi code (CI: tools/check_radio_scope.sh).
// WiFiManager joins home Wi-Fi (credentials saved by ESP-IDF in plain NVS: accepted risk)
// or runs the SAUCEDECK-setup hotspot; its param form edits the key map. Saved forms are
// only staged: Deck applies them after a knob 1 press on the device.
#include "wifi_portal.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_random.h>

static WiFiManager wm;
static WifiState state = WifiState::Off;
static char apPass[9];
static char typeBuf[kKeyCount][10];
static char valueBuf[kKeyCount][kKeyValueMax + 1];
static char ids[kKeyCount * 2][4];
static char labels[kKeyCount * 2][24];
static WiFiManagerParameter *params[kKeyCount * 2];
static bool paramsAdded = false;
static bool savedPending = false;
static bool webPortal = false;

static void onSave() { savedPending = true; }

static void buildParams(const KeyMap &m) {
  for (uint8_t i = 0; i < kKeyCount; i++) {
    snprintf(typeBuf[i], sizeof typeBuf[i], "%s", keyTypeName(m.entry(i).type));
    snprintf(valueBuf[i], sizeof valueBuf[i], "%s", m.entry(i).value);
    if (paramsAdded) {
      params[2 * i]->setValue(typeBuf[i], 9);
      params[2 * i + 1]->setValue(valueBuf[i], kKeyValueMax);
      continue;
    }
    snprintf(ids[2 * i], 4, "t%u", i + 1);
    snprintf(ids[2 * i + 1], 4, "v%u", i + 1);
    snprintf(labels[2 * i], 24, "Key %u type", i + 1);
    snprintf(labels[2 * i + 1], 24, "Key %u value", i + 1);
    params[2 * i] = new WiFiManagerParameter(ids[2 * i], labels[2 * i], typeBuf[i], 9);
    params[2 * i + 1] = new WiFiManagerParameter(ids[2 * i + 1], labels[2 * i + 1], valueBuf[i], kKeyValueMax);
    wm.addParameter(params[2 * i]);
    wm.addParameter(params[2 * i + 1]);
  }
  paramsAdded = true;
}

bool portalStart(const KeyMap &current) {
  snprintf(apPass, sizeof apPass, "%08lu", static_cast<unsigned long>(esp_random() % 100000000UL));
  buildParams(current);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_11dBm);  // limits TX peaks on the 500 mA USB budget (bench-tune)
  wm.setSaveParamsCallback(onSave);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(20);
  wm.setShowInfoUpdate(false);  // no OTA page
  savedPending = false;
  webPortal = false;
  state = wm.autoConnect("SAUCEDECK-setup", apPass) ? WifiState::Joined : WifiState::Portal;
  if (state == WifiState::Portal && !wm.getConfigPortalActive()) {  // neither joined nor AP up
    portalStop();
    return false;
  }
  return true;
}

void portalStop() {
  if (webPortal) wm.stopWebPortal();
  wm.stopConfigPortal();
  MDNS.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state = WifiState::Off;
  webPortal = false;
}

void portalPoll() {
  if (state == WifiState::Off) return;
  wm.process();
  if (WiFi.status() == WL_CONNECTED && !webPortal) {
    wm.startWebPortal();  // same param page on the home network (/param)
    MDNS.begin("saucedeck");
    webPortal = true;
    state = WifiState::Joined;
  }
}

WifiState portalState() { return state; }

bool portalTakeStaged(KeyMap &out, uint8_t &badKey) {
  if (!savedPending) return false;
  savedPending = false;
  KeyMap m;
  badKey = 0;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    if (m.setFromStrings(i, params[2 * i]->getValue(), params[2 * i + 1]->getValue()) != KeyMapError::None) {
      badKey = i + 1;
      return true;
    }
  }
  out = m;
  return true;
}

const char *portalApPassword() { return apPass; }
```

- [ ] **Step 3: `ledsSetWifi` in `src/leds.h/.cpp`**

`leds.h`: `void ledsSetWifi(bool on);` — `leds.cpp`: `void ledsSetWifi(bool on) { effects.setWifi(on); }`.

- [ ] **Step 4: Wire into `src/main.cpp`**

Add `#include "wifi_portal.h"`, `static WifiSession wifi;`. In `run()`:
```cpp
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
      case ActionType::KeyMapDiscarded: break;
```
In `loop()` after `run(actions);`:
```cpp
  portalPoll();
  if (wifi.on()) {
    if (portalState() == WifiState::Joined) wifi.onJoined();
    else if (portalState() == WifiState::Portal) wifi.onPortal();
  }
  if (wifi.tick(now)) { portalStop(); ledsSetWifi(false); }
  KeyMap staged;
  uint8_t bad = 0;
  if (portalTakeStaged(staged, bad)) {
    if (bad) { invalidKey = bad; invalidUntil = now + 4000; }
    else deck.stage(staged, now);
  }

  uint8_t banner = BANNER_NONE, arg = 0;
  uint32_t held = deck.chordHeldMs(now);
  if (deck.pending()) { banner = BANNER_APPLY; arg = (deck.pendingMsLeft(now) + 999) / 1000; }
  else if (held > 500 && held < KnobChord::kWifiHoldMs) { banner = BANNER_WIFI_COUNTDOWN; arg = (KnobChord::kWifiHoldMs - held + 999) / 1000; }
  else if (static_cast<int32_t>(invalidUntil - now) > 0) { banner = BANNER_INVALID_KEY; arg = invalidKey; }
  else if (static_cast<int32_t>(helperMissingUntil - now) > 0) banner = BANNER_HELPER_MISSING;
  else if (static_cast<int32_t>(wifiFailedUntil - now) > 0) banner = BANNER_WIFI_FAILED;
  else if (wifi.state() == WifiState::Portal) banner = BANNER_WIFI_PORTAL;
  else if (wifi.state() == WifiState::Joined) banner = BANNER_WIFI_JOINED;
  else if (wifi.state() == WifiState::Starting) banner = BANNER_WIFI_STARTING;
  else if (keymapResetNotice && now < 5000) banner = BANNER_KEYMAP_RESET;
  else if (brownoutNotice && now < 8000) banner = BANNER_BROWNOUT;
```
(`static uint8_t invalidKey = 0; static uint32_t invalidUntil = 0, wifiFailedUntil = 0;` at file scope.) Pass `banner, arg` into the `ScreenState` from Task 15.

- [ ] **Step 5: Draw banners in `src/display.cpp`**

`display.cpp` must not include `wifi_portal.h` (radio scope, Task 17). Add `void displaySetApPassword(const char *p);` to `display.h`; `main.cpp` calls it after a successful `portalStart` (Step 4). In `display.cpp`:
```cpp
static char apPassword[9] = "";
void displaySetApPassword(const char *p) { snprintf(apPassword, sizeof apPassword, "%s", p); }

static void drawBanner(uint8_t b, uint8_t arg) {
  tft.fillRect(80, 222, 240, 18, kBg);
  if (b == BANNER_NONE) return;
  char text[40];
  switch (b) {
    case BANNER_WIFI_COUNTDOWN: snprintf(text, sizeof text, "Wi-Fi in %us", arg); break;
    case BANNER_WIFI_STARTING: snprintf(text, sizeof text, "Wi-Fi..."); break;
    case BANNER_WIFI_PORTAL: snprintf(text, sizeof text, "SAUCEDECK-setup %s", apPassword); break;
    case BANNER_WIFI_JOINED: snprintf(text, sizeof text, "saucedeck.local"); break;
    case BANNER_APPLY: snprintf(text, sizeof text, "Apply? knob 1 (%us)", arg); break;
    case BANNER_HELPER_MISSING: snprintf(text, sizeof text, "helper not running"); break;
    case BANNER_KEYMAP_RESET: snprintf(text, sizeof text, "Key map reset"); break;
    case BANNER_WIFI_FAILED: snprintf(text, sizeof text, "Wi-Fi failed"); break;
    case BANNER_INVALID_KEY: snprintf(text, sizeof text, "Invalid key %u", arg); break;
    case BANNER_BROWNOUT: snprintf(text, sizeof text, "Reset: brownout"); break;
    default: text[0] = 0; break;
  }
  tft.setTextSize(2);
  tft.setTextColor(b == BANNER_APPLY ? ST77XX_YELLOW : kFg, kBg);
  tft.setCursor(80, 223);
  tft.print(text);
}
```
In `displayUpdate`: `if (dirty & DIRTY_BANNER) drawBanner(cur.banner, cur.bannerArg);`. In `drawMic`, when `micLevelKnown`, print `"%3u%%"` of `micPercent` instead of `" LIVE"` (pass `const ScreenState &` to `drawMic`).

- [ ] **Step 6: Build**

Run: `$P run -e saucedeck -e bringup 2>&1 | grep -cE 'warning:'; $P run -e saucedeck 2>&1 | grep -E 'Flash:|SUCCESS'`
Expected: `0` (if WiFiManager headers emit warnings under `-Wall`, they come from `.pio/libdeps` — only `build_src_flags` apply `-Wall`, so library warnings must not appear; if they do, record and ask before suppressing). Flash usage < 90 % of 1.31 MB app partition.

- [ ] **Step 7: Bench**

Flash. Operator, with notes in `docs/bringup/<date>.log`:
1. Hold both knobs: LCD counts down 5 → 1, at 5 s LEDs go dark except LED 0 blue pulse; banner `SAUCEDECK-setup <8 digits>` (no saved Wi-Fi) or `saucedeck.local`.
2. Phone joins hotspot with the shown password, portal → "Setup" → 12 key fields; set key 1 = `app` / `Calculator`, Save. LCD: `Apply? knob 1 (30s)` counting down.
3. Press knob 1 → green flash; banner clears. Reboot (unplug/replug): key 1 still `app` (NVS).
4. Repeat with key 1 value `ftp://x` and type `url` → LCD `Invalid key 1`, nothing staged.
5. Leave Wi-Fi on: after 10 min Wi-Fi off, LEDs back to normal.
6. During Wi-Fi on, flash test: watch for USB disconnect / reset (brownout). Note result.

- [ ] **Step 8: Commit**

```bash
git add platformio.ini src/wifi_portal.h src/wifi_portal.cpp src/main.cpp src/display.cpp src/display.h src/leds.h src/leds.cpp docs/bringup
git commit -m "Wi-Fi settings portal (WiFiManager) behind 5 s two-knob hold, LCD prompts

<bench-verified steps ... | not bench-verified>

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 17: CI radio scope check + security docs

**Files:**
- Create: `tools/check_radio_scope.sh`
- Delete: `tools/check_no_radio.sh`
- Modify: `.github/workflows/ci.yml`, `SECURITY.md`, `docs/plans/2026-10-18-upstream-port.md`

**Interfaces:** CI step `tools/check_radio_scope.sh .pio/build/saucedeck` exits 0 only if (a) no Bluetooth init symbols in the ELF and (b) the only object under `src/` referencing Wi-Fi symbols is `wifi_portal.cpp.o`.

- [ ] **Step 1: Write `tools/check_radio_scope.sh`**

```sh
#!/bin/sh
# Radio scope (security baseline): Bluetooth never linked; Wi-Fi referenced only by
# src/wifi_portal.cpp. Usage: tools/check_radio_scope.sh .pio/build/saucedeck
set -eu
build="$1"
nm="${NM:-$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-nm}"
elf="$build/firmware.elf"
syms=$("$nm" "$elf")
echo "$syms" | grep -q -E ' T (_Z5setupv|app_main)$' || { echo "radio_scope: bad ELF" >&2; exit 2; }
if echo "$syms" | grep -E ' [TtW] (esp_bt_controller_init|esp_bluedroid_init|nimble_port_init|btStart)$'; then
  echo "radio_scope: Bluetooth linked" >&2; exit 1
fi
bad=0
for o in "$build"/src/*.o; do
  case "$o" in */wifi_portal.cpp.o) continue ;; esac
  if "$nm" -u "$o" | grep -q -E 'esp_wifi_|WiFiClass|WiFiManager|_ZN8WiFi'; then
    echo "radio_scope: Wi-Fi referenced by $o" >&2; bad=1
  fi
done
[ "$bad" -eq 0 ] || exit 1
echo "radio_scope: OK (no BT; Wi-Fi only from wifi_portal.cpp)"
```
`chmod +x tools/check_radio_scope.sh && git rm tools/check_no_radio.sh`.

- [ ] **Step 2: Verify it passes and catches a violation**

Run: `tools/check_radio_scope.sh .pio/build/saucedeck` → expect `OK`.
Temporarily add `#include <WiFi.h>` and `WiFi.mode(WIFI_OFF);` to `src/leds.cpp` `ledsBegin()`, rebuild, rerun → expect `Wi-Fi referenced by .../leds.cpp.o`, exit 1. Revert the change and rebuild.

- [ ] **Step 3: CI + docs**

`.github/workflows/ci.yml`: replace the "No Wi-Fi/BT linked" step with:
```yaml
      - name: Radio scope (no BT, Wi-Fi only in wifi_portal.cpp)
        run: tools/check_radio_scope.sh .pio/build/saucedeck
```
`SECURITY.md` Baseline: replace "Wi-Fi and Bluetooth are never initialised." with "Bluetooth is never linked. Wi-Fi exists only in `src/wifi_portal.cpp`, is off by default, starts only after a 5 s two-knob hold and turns off after 10 minutes. The Wi-Fi password is stored in plain flash (accepted risk). Key-map changes from the portal apply only after a knob press on the device." Replace "No typed strings." line with "Firmware sends only fixed key codes or validated keystrokes (modifiers + one key); it never types stored text."
`docs/plans/2026-10-18-upstream-port.md` security table row "Attack surface: radios" → "Changed 2026-10-21: Wi-Fi portal by design (spec 2026-10-20); CI `tools/check_radio_scope.sh`".

- [ ] **Step 4: Commit and push, watch CI**

```bash
git add tools .github/workflows/ci.yml SECURITY.md docs/plans/2026-10-18-upstream-port.md
git commit -m "CI radio scope: no BT, Wi-Fi only from wifi_portal.cpp; security docs

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
Push only after owner OK; then `gh run watch <id> --exit-status` → expect success.

---

### Task 18: Bench session + docs

**Files:**
- Create: `docs/bringup/<date>.log`
- Modify: `docs/kb/peripherals.md`, `docs/kb/esp32s3.md`, `wiring.md` (if anything changed), `CLAUDE.md` Status section

- [ ] **Step 1: Run the full bench list and log each result**

1. Defaults: keys 1–12 → F13–F24 (key 12 depends on issue #1).
2. Keystroke entry `cmd+shift+4` on key 8 → macOS screenshot crosshair.
3. App / URL / Shortcut keys without helper → orange flash + "helper not running".
4. Knob 1 volume, mute; knob 2 tracks; knob 2 push+release mute (Cmd+Shift+M fallback); push+turn → "helper not running".
5. Both knobs < 5 s → sync (green flash on release); ≥ 5 s → Wi-Fi.
6. Wi-Fi steps from Task 16 Step 7.
7. Brownout watch: Wi-Fi on + key presses + knob turns for 2 min; USB must stay up.
8. Encoder count per click on PCNT firmware (open item in docs/bringup/2026-10-19.log).

- [ ] **Step 2: Feed verified facts into kb**, e.g. Wi-Fi TX power that is stable, knob 2 guard value, WiFiManager behaviour. Update `CLAUDE.md` Status with date and what's bench-verified.

- [ ] **Step 3: Commit**

```bash
git add docs CLAUDE.md wiring.md
git commit -m "Bench <date>: key map, Wi-Fi portal, fallbacks (<results>)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Spec deltas (need owner OK; update the spec when approved)

1. **Key value max 56 chars** (spec: 95). Reason: Arduino `USBHIDVendor` report = 63 bytes; ACTION header + key + type + len leaves 56. Alternative is multi-report fragmentation (more parser surface).
2. **Invalid portal fields are reported on the LCD ("Invalid key N")**, not on the web page. WiFiManager's save callback has no simple way to render errors back into its form.
3. **Wi-Fi hotspot password is random per session (8 digits, shown on LCD)**, not fixed. The spec says "password shown on LCD"; this makes it not stored.

## Out of scope (Plan 2 and later)
- Mac helper (Swift): separate plan after Task 2's result.
- Windows support: own spec.
