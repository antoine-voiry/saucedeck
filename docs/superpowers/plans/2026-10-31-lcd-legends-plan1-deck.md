# LCD Legends + Press Feedback (Plan 1/3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The LCD shows a 4×3 legend grid (one tile per key, labelled from the key map) above a status strip (volume bar, mic state, banners), and a pressed key's tile fills and fades back.

**Architecture:** All logic lives in header-only/pure C++17 units in `lib/deck_core/src/` (labels in `KeyMap`, `legend.h` for label text and wrapping, `tile_anim.h` for press feedback, `ui_layout.h` ScreenModel v2 for dirty tracking), each TDD'd in `test/native/`. `src/display.cpp` is rewritten to draw only from those models; `src/main.cpp` feeds them. The device sim (`test/sim/lib/simdeck`) gains just enough Adafruit GFX surface to run the new display code, plus rect recording for scenario tests.

**Tech Stack:** PlatformIO, pioarduino 55.03.35 (Arduino core 3.x), Adafruit GFX 1.12.6 + ST7789 1.11.0, Unity (native + sim envs), C++17.

**Spec:** `docs/superpowers/specs/2026-10-31-lcd-legends-status-design.md` (§3, §4.1–4.3, §4.7, §5, §11). Plan 2 = status protocol (`TIME`/`CARD`, clock, cards). Plan 3 = Hammerspoon helper.

## Global Constraints

- `lib/deck_core/` = no `Arduino.h`, pure C++17; builds under `-std=gnu++17 -Wall -Wextra -Werror` (env:native).
- TDD: write the failing test, run it and see it fail for the right reason, then the minimal code.
- Panel: 320×240 landscape, rotation 3, SPI 20 MHz, MISO −1 (GPIO13 = LCD DC). Do not change.
- Backlight GPIO9 PWM path untouched (19 mA measured, 1 mA margin).
- Labels: `kKeyLabelMax = 16`, printable ASCII 0x20–0x7E or empty. Labels are only stored and drawn; never typed, never sent to the Mac.
- NVS blob: `'SDKM'`, version 2 = `12 × [type, vlen, value, llen, label]` + CRC32 LE. Version 1 blobs must still load (labels empty).
- Tiles: 80 × 56 px, grid y 20–187, key position N → tile N−1, row-major (positions 1–4 top row).
- Press feedback: held = full fill; release fades back in 4 steps × 60 ms (levels 3, 2, 1, 0 out of 4). Failed (helper missing) = orange `0xFD20`.
- Frame pacing stays `kMinFrameMs = 30`.
- Done = `pio test -e native` green, `pio test -e sim` green, `pio run -e saucedeck -e bringup` SUCCESS, zero new warnings. Show the output.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## File Structure

| File | Action | Responsibility |
| --- | --- | --- |
| `lib/deck_core/src/keymap.h`, `keymap.cpp` | Modify | `label` field, `setLabel`, blob v2 (+ v1 load) |
| `lib/deck_core/src/legend.h` | Create | `displayLabel` (text a tile shows), `wrapLabel` (2-line wrap with injected glyph width) |
| `lib/deck_core/src/tile_anim.h` | Create | `TileAnim` (press/fade levels per tile), `blend565` |
| `lib/deck_core/src/deck.h` | Modify | `Deck::keysHeld()`, `helperMissingKeys()` |
| `lib/deck_core/src/ui_layout.h` | Modify | ScreenModel v2, horizontal `barDeltaH` (replaces vertical `barDelta`) |
| `src/display.h`, `src/display.cpp` | Rewrite | Draw top bar, tiles, strip from the models |
| `src/main.cpp` | Modify | Feed `TileAnim`, key-map generation, new `displayUpdate` args |
| `src/wifi_portal.cpp` | Modify | 12 label fields, preset loader fills labels |
| `test/native/test_keymap/test_main.cpp` | Modify | Label + blob v2 tests |
| `test/native/test_legend/test_main.cpp` | Create | `displayLabel`, `wrapLabel`, `TileAnim`, `blend565` tests |
| `test/native/test_deck/test_main.cpp` | Modify | `keysHeld`, `helperMissingKeys` tests |
| `test/native/test_ui/test_main.cpp` | Modify | ScreenModel v2 + `barDeltaH` tests |
| `test/sim/lib/simdeck/src/Adafruit_GFX.h` | Modify | `GFXglyph` / `GFXfont` types |
| `test/sim/lib/simdeck/src/Fonts/FreeSans9pt7b.h` | Create | Stand-in font (fixed 10 px advance) |
| `test/sim/lib/simdeck/src/Adafruit_ST7789.h`, `fakes.cpp`, `sim/lcd.h` | Modify | `setFont`, `fillCircle`, rect recording |
| `test/sim/lib/simdeck/src/sim_deck.cpp` | Modify | Preset loader reads `label` |
| `test/native/test_sim_display/test_main.cpp` | Create | Display scenarios on the real firmware |
| `docs/kb/toolchain.md` | Modify | Firmware size with FreeSans9pt7b |

---

### Task 1: Key labels in KeyMap + NVS blob v2

**Files:**
- Modify: `lib/deck_core/src/keymap.h`
- Modify: `lib/deck_core/src/keymap.cpp`
- Test: `test/native/test_keymap/test_main.cpp`

**Interfaces:**
- Consumes: existing `KeyMap`, `KeyEntry`, `KeyMapError`, `crc32`, `serializeKeyMap`, `deserializeKeyMap`.
- Produces: `constexpr size_t kKeyLabelMax = 16;`, `KeyEntry::label` (`char[kKeyLabelMax + 1]`, empty by default), `KeyMapError KeyMap::setLabel(uint8_t i, const char *label);`, `operator==` compares labels, blob v2, `kKeyMapBlobMax = 4 + 1 + kKeyCount * (3 + kKeyValueMax + kKeyLabelMax) + 4`.

- [ ] **Step 1: Write the failing tests**

Add to `test/native/test_keymap/test_main.cpp` above `int main` (before `test_crc32_check_value` is fine):

```cpp
// --- labels (legend text on the LCD) ---

void test_label_default_empty() {
  KeyMap m;
  for (uint8_t i = 0; i < kKeyCount; i++) TEST_ASSERT_EQUAL_STRING("", m.entry(i).label);
}

void test_label_set_and_clear() {
  KeyMap m;
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setLabel(2, "Chrome"));
  TEST_ASSERT_EQUAL_STRING("Chrome", m.entry(2).label);
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setLabel(2, ""));
  TEST_ASSERT_EQUAL_STRING("", m.entry(2).label);
}

void test_label_rejections_leave_label_unchanged() {
  KeyMap m;
  m.setLabel(1, "Mail");
  TEST_ASSERT_EQUAL(KeyMapError::TooLong, m.setLabel(1, "abcdefghijklmnopq"));  // 17
  TEST_ASSERT_EQUAL(KeyMapError::NotPrintable, m.setLabel(1, "Ma\x01il"));
  TEST_ASSERT_EQUAL(KeyMapError::NotPrintable, m.setLabel(1, "Caf\xC3\xA9"));  // UTF-8
  TEST_ASSERT_EQUAL(KeyMapError::BadIndex, m.setLabel(12, "x"));
  TEST_ASSERT_EQUAL_STRING("Mail", m.entry(1).label);
  TEST_ASSERT_EQUAL(KeyMapError::None, m.setLabel(1, "abcdefghijklmnop"));  // 16 = max
}

void test_label_survives_value_change() {
  KeyMap m;
  m.setLabel(0, "Shot");
  TEST_ASSERT_EQUAL(KeyMapError::None, m.set(0, KeyType::App, "Screenshot"));
  TEST_ASSERT_EQUAL_STRING("Shot", m.entry(0).label);
}

void test_keymap_equality_includes_label() {
  KeyMap a, b;
  b.setLabel(5, "Settings");
  TEST_ASSERT_FALSE(a == b);
  a.setLabel(5, "Settings");
  TEST_ASSERT_TRUE(a == b);
}

void test_blob_roundtrip_keeps_labels() {
  KeyMap m;
  m.set(1, KeyType::App, "Google Chrome");
  m.setLabel(1, "Chrome");
  m.setLabel(11, "Bambu Studio");
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_UINT8(2, buf[4]);  // blob version
  KeyMap back;
  TEST_ASSERT_TRUE(deserializeKeyMap(buf, n, back));
  TEST_ASSERT_TRUE(back == m);
  TEST_ASSERT_EQUAL_STRING("Bambu Studio", back.entry(11).label);
}

void test_blob_full_map_fits_max() {
  // Every value and label at its maximum length must still serialize.
  KeyMap m;
  char v[kKeyValueMax + 1], l[kKeyLabelMax + 1];
  memset(v, 'a', kKeyValueMax);
  v[kKeyValueMax] = 0;
  memset(l, 'b', kKeyLabelMax);
  l[kKeyLabelMax] = 0;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    TEST_ASSERT_EQUAL(KeyMapError::None, m.set(i, KeyType::App, v));
    TEST_ASSERT_EQUAL(KeyMapError::None, m.setLabel(i, l));
  }
  uint8_t buf[kKeyMapBlobMax];
  TEST_ASSERT_EQUAL(kKeyMapBlobMax, serializeKeyMap(m, buf, sizeof buf));
}

// Factory map as the firmware stored it before 2026-10-31 (blob version 1, no labels).
static size_t v1Blob(uint8_t *buf) {
  const uint8_t head[] = {'S', 'D', 'K', 'M', 1};
  memcpy(buf, head, sizeof head);
  size_t p = sizeof head;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    char v[8];
    size_t n = static_cast<size_t>(snprintf(v, sizeof v, "f%u", 13u + i));
    buf[p++] = 0;  // KeyType::Keystroke
    buf[p++] = static_cast<uint8_t>(n);
    memcpy(buf + p, v, n);
    p += n;
  }
  uint32_t c = crc32(buf, p);
  for (int k = 0; k < 4; k++) buf[p++] = static_cast<uint8_t>(c >> (8 * k));
  return p;
}

void test_blob_v1_still_loads_with_empty_labels() {
  uint8_t buf[kKeyMapBlobMax];
  size_t n = v1Blob(buf);
  KeyMap back;
  back.setLabel(0, "junk");
  TEST_ASSERT_TRUE(deserializeKeyMap(buf, n, back));
  TEST_ASSERT_EQUAL_STRING("f13", back.entry(0).value);
  TEST_ASSERT_EQUAL_STRING("f24", back.entry(11).value);
  TEST_ASSERT_EQUAL_STRING("", back.entry(0).label);
}

void test_blob_rejects_valid_crc_but_unprintable_label() {
  KeyMap m;
  m.setLabel(0, "ab");
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  // key 0: [5]=type [6]=vlen [7..9]="f13" [10]=llen [11..12]="ab"
  TEST_ASSERT_EQUAL_UINT8(2, buf[10]);
  buf[11] = 0x01;
  uint32_t c = crc32(buf, n - 4);
  buf[n - 4] = c & 0xFF; buf[n - 3] = (c >> 8) & 0xFF; buf[n - 2] = (c >> 16) & 0xFF; buf[n - 1] = c >> 24;
  KeyMap target;
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target));
}

void test_blob_rejects_unknown_version() {
  KeyMap m;
  uint8_t buf[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, buf, sizeof buf);
  buf[4] = 3;
  uint32_t c = crc32(buf, n - 4);
  buf[n - 4] = c & 0xFF; buf[n - 3] = (c >> 8) & 0xFF; buf[n - 2] = (c >> 16) & 0xFF; buf[n - 1] = c >> 24;
  KeyMap target;
  TEST_ASSERT_FALSE(deserializeKeyMap(buf, n, target));
}
```

Register them in `main` after `RUN_TEST(test_keymap_mac_daily_preset_is_valid);`:

```cpp
  RUN_TEST(test_label_default_empty);
  RUN_TEST(test_label_set_and_clear);
  RUN_TEST(test_label_rejections_leave_label_unchanged);
  RUN_TEST(test_label_survives_value_change);
  RUN_TEST(test_keymap_equality_includes_label);
```

and after `RUN_TEST(test_blob_small_buffer_returns_zero);`:

```cpp
  RUN_TEST(test_blob_roundtrip_keeps_labels);
  RUN_TEST(test_blob_full_map_fits_max);
  RUN_TEST(test_blob_v1_still_loads_with_empty_labels);
  RUN_TEST(test_blob_rejects_valid_crc_but_unprintable_label);
  RUN_TEST(test_blob_rejects_unknown_version);
```

- [ ] **Step 2: Run tests, confirm they fail**

Run: `pio test -e native -f test_keymap`
Expected: compile errors `'struct KeyEntry' has no member named 'label'` and `'class KeyMap' has no member named 'setLabel'`.

- [ ] **Step 3: Implement**

`lib/deck_core/src/keymap.h`: after `constexpr size_t kKeyValueMax = 56;` add:

```cpp
constexpr size_t kKeyLabelMax = 16;  // legend text on the LCD tile (2 lines of ~8 chars)
```

Replace `struct KeyEntry`:

```cpp
struct KeyEntry {
  KeyType type;
  char value[kKeyValueMax + 1];
  Keystroke ks;  // valid only when type == Keystroke
  char label[kKeyLabelMax + 1];  // empty = derive from value (legend.h)
};
```

In `class KeyMap` public section, after `setFromStrings`:

```cpp
  KeyMapError setLabel(uint8_t i, const char *label);  // printable ASCII, may be empty
```

Replace the blob constant and comment:

```cpp
constexpr size_t kKeyMapBlobMax = 4 + 1 + kKeyCount * (3 + kKeyValueMax + kKeyLabelMax) + 4;
// NVS blob: 'SDKM', version 2, 12 x [type, vlen, value, llen, label], CRC32 LE.
// Version 1 (no label fields) still loads. Load re-validates every entry.
```

`lib/deck_core/src/keymap.cpp`: add after `setFromStrings`:

```cpp
KeyMapError KeyMap::setLabel(uint8_t i, const char *label) {
  if (i >= kKeyCount) return KeyMapError::BadIndex;
  size_t n = strnlen(label, kKeyLabelMax + 1);
  if (n > kKeyLabelMax) return KeyMapError::TooLong;
  for (size_t k = 0; k < n; k++)
    if (label[k] < 0x20 || label[k] > 0x7E) return KeyMapError::NotPrintable;
  memcpy(entries_[i].label, label, n);
  entries_[i].label[n] = 0;
  return KeyMapError::None;
}
```

Replace `operator==` body loop condition:

```cpp
    if (entries_[i].type != o.entries_[i].type || strcmp(entries_[i].value, o.entries_[i].value) != 0 ||
        strcmp(entries_[i].label, o.entries_[i].label) != 0)
      return false;
```

Replace `kBlobVersion`, `serializeKeyMap`, `deserializeKeyMap`:

```cpp
static const uint8_t kBlobVersion = 2;  // v2 (2026-10-31) adds labels; v1 still loads

size_t serializeKeyMap(const KeyMap &m, uint8_t *buf, size_t cap) {
  size_t need = 4 + 1 + 4;
  for (uint8_t i = 0; i < kKeyCount; i++) need += 3 + strlen(m.entry(i).value) + strlen(m.entry(i).label);
  if (cap < need) return 0;
  memcpy(buf, kMagic, 4);
  size_t p = 4;
  buf[p++] = kBlobVersion;
  for (uint8_t i = 0; i < kKeyCount; i++) {
    const KeyEntry &e = m.entry(i);
    size_t len = strlen(e.value);
    buf[p++] = static_cast<uint8_t>(e.type);
    buf[p++] = static_cast<uint8_t>(len);
    memcpy(buf + p, e.value, len);
    p += len;
    size_t ll = strlen(e.label);
    buf[p++] = static_cast<uint8_t>(ll);
    memcpy(buf + p, e.label, ll);
    p += ll;
  }
  uint32_t c = crc32(buf, p);
  for (int k = 0; k < 4; k++) buf[p++] = static_cast<uint8_t>(c >> (8 * k));
  return p;
}

bool deserializeKeyMap(const uint8_t *buf, size_t n, KeyMap &out) {
  if (n < 4 + 1 + 4 || memcmp(buf, kMagic, 4) != 0 || (buf[4] != 1 && buf[4] != 2)) return false;
  const bool hasLabels = buf[4] == 2;
  uint32_t stored = buf[n - 4] | (buf[n - 3] << 8) | (buf[n - 2] << 16) |
                    (static_cast<uint32_t>(buf[n - 1]) << 24);
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
    if (!hasLabels) continue;
    if (p + 1 > n - 4) return false;
    uint8_t ll = buf[p++];
    if (ll > kKeyLabelMax || p + ll > n - 4) return false;
    char l[kKeyLabelMax + 1];
    memcpy(l, buf + p, ll);
    l[ll] = 0;
    p += ll;
    if (m.setLabel(i, l) != KeyMapError::None) return false;
  }
  if (p != n - 4) return false;
  out = m;
  return true;
}
```

- [ ] **Step 4: Run tests, confirm pass**

Run: `pio test -e native -f test_keymap`
Expected: all test_keymap tests PASS (21 old + 10 new = 31).

- [ ] **Step 5: Full native run + firmware build**

Run: `pio test -e native && pio run -e saucedeck`
Expected: all suites PASS; `[SUCCESS]`. (`keymap_store.cpp` uses `kKeyMapBlobMax`, so it picks up the larger buffer, 4 + 1 + 12 × 75 + 4 = 909 B on the stack, with no edit.)

- [ ] **Step 6: Commit**

```bash
git add lib/deck_core/src/keymap.h lib/deck_core/src/keymap.cpp test/native/test_keymap/test_main.cpp
git commit -m "KeyMap: per-key legend labels, NVS blob v2 (v1 still loads)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Legend text — displayLabel + wrapLabel

**Files:**
- Create: `lib/deck_core/src/legend.h`
- Create: `test/native/test_legend/test_main.cpp`

**Interfaces:**
- Consumes: `KeyEntry`, `KeyType`, `kKeyValueMax` (Task 1 `label` field).
- Produces:
  - `void displayLabel(const KeyEntry &e, char *out, size_t cap);`
  - `using GlyphWidthFn = uint8_t (*)(char);`
  - `constexpr size_t kLegendLineMax = kKeyValueMax;`
  - `struct LabelLines { char line1[kLegendLineMax + 1]; char line2[kLegendLineMax + 4]; };`
  - `void wrapLabel(const char *text, uint16_t maxPx, GlyphWidthFn w, LabelLines &out);`

- [ ] **Step 1: Write the failing tests**

Create `test/native/test_legend/test_main.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "keymap.h"
#include "legend.h"

void setUp() {}
void tearDown() {}

// --- displayLabel: label if set, else readable text derived from the value ---

static void shown(const KeyMap &m, uint8_t i, const char *expected, size_t cap = kLegendLineMax + 1) {
  char out[kLegendLineMax + 1];
  displayLabel(m.entry(i), out, cap);
  TEST_ASSERT_EQUAL_STRING(expected, out);
}

void test_label_default_fkey_uppercased() {
  KeyMap m;
  shown(m, 0, "F13");
  shown(m, 11, "F24");
}

void test_label_keystroke_chord_as_written() {
  KeyMap m;
  m.set(8, KeyType::Keystroke, "cmd+h");
  shown(m, 8, "cmd+h");
}

void test_label_user_label_wins() {
  KeyMap m;
  m.setLabel(1, "Chrome");
  shown(m, 1, "Chrome");
}

void test_label_url_host_without_www_and_path() {
  KeyMap m;
  m.set(7, KeyType::Url, "https://www.youtube.com/watch?v=x");
  shown(m, 7, "youtube.com");
  m.set(2, KeyType::Url, "http://mail.google.com");
  shown(m, 2, "mail.google.com");
}

void test_label_app_and_shortcut_show_value() {
  KeyMap m;
  m.set(0, KeyType::App, "Calculator");
  m.set(3, KeyType::Shortcut, "Focus on");
  shown(m, 0, "Calculator");
  shown(m, 3, "Focus on");
}

void test_label_truncates_to_cap() {
  KeyMap m;
  m.set(0, KeyType::App, "Calculator");
  shown(m, 0, "Calc", 5);
}

// --- wrapLabel: up to 2 lines that fit maxPx, split at a space when possible ---

static uint8_t w10(char) { return 10; }
static uint8_t narrowI(char c) { return c == 'i' ? 4 : 10; }

static LabelLines wrap(const char *t, uint16_t maxPx = 80, GlyphWidthFn w = w10) {
  LabelLines l;
  wrapLabel(t, maxPx, w, l);
  return l;
}

void test_wrap_fits_one_line() {
  LabelLines l = wrap("Chrome");
  TEST_ASSERT_EQUAL_STRING("Chrome", l.line1);
  TEST_ASSERT_EQUAL_STRING("", l.line2);
}

void test_wrap_exact_width_is_one_line() {
  LabelLines l = wrap("Telegram");  // 8 x 10 = 80
  TEST_ASSERT_EQUAL_STRING("Telegram", l.line1);
  TEST_ASSERT_EQUAL_STRING("", l.line2);
}

void test_wrap_splits_at_last_fitting_space() {
  LabelLines l = wrap("System Settings");
  TEST_ASSERT_EQUAL_STRING("System", l.line1);
  TEST_ASSERT_EQUAL_STRING("Settings", l.line2);
}

void test_wrap_space_right_after_fit_is_used() {
  LabelLines l = wrap("Gmail in Chrome");
  TEST_ASSERT_EQUAL_STRING("Gmail in", l.line1);
  TEST_ASSERT_EQUAL_STRING("Chrome", l.line2);
}

void test_wrap_hard_cut_without_space() {
  LabelLines l = wrap("Screenshot");
  TEST_ASSERT_EQUAL_STRING("Screensh", l.line1);
  TEST_ASSERT_EQUAL_STRING("ot", l.line2);
}

void test_wrap_second_line_truncated_with_dots() {
  LabelLines l = wrap("Autodesk Fusion 360 Pro");
  TEST_ASSERT_EQUAL_STRING("Autodesk", l.line1);
  TEST_ASSERT_EQUAL_STRING("Fusio...", l.line2);  // 5 x 10 + "..." 30 = 80
}

void test_wrap_uses_glyph_widths() {
  LabelLines l = wrap("iiiiiiiiiiii", 80, narrowI);  // 12 x 4 = 48
  TEST_ASSERT_EQUAL_STRING("iiiiiiiiiiii", l.line1);
  TEST_ASSERT_EQUAL_STRING("", l.line2);
}

void test_wrap_empty() {
  LabelLines l = wrap("");
  TEST_ASSERT_EQUAL_STRING("", l.line1);
  TEST_ASSERT_EQUAL_STRING("", l.line2);
}

void test_wrap_longest_value_stays_in_buffers() {
  char t[kLegendLineMax + 1];
  memset(t, 'x', kLegendLineMax);
  t[kLegendLineMax] = 0;
  LabelLines l = wrap(t, 1000);  // 56 x 10 = 560: one line
  TEST_ASSERT_EQUAL_size_t(kLegendLineMax, strlen(l.line1));
  l = wrap(t, 30);
  TEST_ASSERT_EQUAL_STRING("xxx", l.line1);
  TEST_ASSERT_EQUAL_STRING("...", l.line2);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_label_default_fkey_uppercased);
  RUN_TEST(test_label_keystroke_chord_as_written);
  RUN_TEST(test_label_user_label_wins);
  RUN_TEST(test_label_url_host_without_www_and_path);
  RUN_TEST(test_label_app_and_shortcut_show_value);
  RUN_TEST(test_label_truncates_to_cap);
  RUN_TEST(test_wrap_fits_one_line);
  RUN_TEST(test_wrap_exact_width_is_one_line);
  RUN_TEST(test_wrap_splits_at_last_fitting_space);
  RUN_TEST(test_wrap_space_right_after_fit_is_used);
  RUN_TEST(test_wrap_hard_cut_without_space);
  RUN_TEST(test_wrap_second_line_truncated_with_dots);
  RUN_TEST(test_wrap_uses_glyph_widths);
  RUN_TEST(test_wrap_empty);
  RUN_TEST(test_wrap_longest_value_stays_in_buffers);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, confirm failure**

Run: `pio test -e native -f test_legend`
Expected: compile error `legend.h: No such file or directory`.

- [ ] **Step 3: Implement**

Create `lib/deck_core/src/legend.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "keymap.h"

// Legend tile text, hardware-free. The glue passes the font's glyph advances, so the
// wrapping rule is tested here without the font.

// Text a tile shows: the user's label, else something readable derived from the value
// (keystroke as written with F-keys uppercased, URL host without www., app/shortcut name).
inline void displayLabel(const KeyEntry &e, char *out, size_t cap) {
  if (cap == 0) return;
  const bool derived = e.label[0] == 0;
  const char *src = derived ? e.value : e.label;
  size_t n;
  if (derived && e.type == KeyType::Url) {
    const char *host = strstr(e.value, "://");
    src = host ? host + 3 : e.value;
    if (strncmp(src, "www.", 4) == 0) src += 4;
    n = strcspn(src, "/?#");
  } else {
    n = strlen(src);
  }
  if (n > cap - 1) n = cap - 1;
  memcpy(out, src, n);
  out[n] = 0;
  if (derived && e.type == KeyType::Keystroke && out[0] == 'f' && n > 1 && !strchr(out, '+')) out[0] = 'F';
}

using GlyphWidthFn = uint8_t (*)(char);
constexpr size_t kLegendLineMax = kKeyValueMax;  // longest displayLabel() result

struct LabelLines {
  char line1[kLegendLineMax + 1];
  char line2[kLegendLineMax + 4];  // room for "..."
};

namespace legend_detail {
inline uint16_t width(const char *s, size_t n, GlyphWidthFn w) {
  uint16_t t = 0;
  for (size_t i = 0; i < n; i++) t += w(s[i]);
  return t;
}

// Number of leading chars of s[0..n) that fit in maxPx.
inline size_t fit(const char *s, size_t n, uint16_t maxPx, GlyphWidthFn w) {
  uint16_t t = 0;
  size_t i = 0;
  for (; i < n; i++) {
    if (t + w(s[i]) > maxPx) break;
    t += w(s[i]);
  }
  return i;
}
}  // namespace legend_detail

// Up to two lines, each at most maxPx wide. Line 1 breaks at the last space that fits
// (hard cut if none); line 2 that still overflows is cut and ends with "...".
inline void wrapLabel(const char *text, uint16_t maxPx, GlyphWidthFn w, LabelLines &out) {
  using namespace legend_detail;
  out.line1[0] = out.line2[0] = 0;
  size_t n = strnlen(text, kLegendLineMax);
  size_t a = fit(text, n, maxPx, w);
  if (a == n) {
    memcpy(out.line1, text, n);
    out.line1[n] = 0;
    return;
  }
  size_t cut = a;
  for (size_t k = a; k > 0; k--)
    if (text[k] == ' ') {
      cut = k;
      break;
    }
  if (cut == 0) cut = 1;  // first glyph alone is wider than the tile: still make progress
  memcpy(out.line1, text, cut);
  out.line1[cut] = 0;

  const char *rest = text + cut;
  size_t rn = n - cut;
  while (rn && *rest == ' ') {
    rest++;
    rn--;
  }
  if (fit(rest, rn, maxPx, w) == rn) {
    memcpy(out.line2, rest, rn);
    out.line2[rn] = 0;
    return;
  }
  uint16_t dots = width("...", 3, w);
  size_t c = dots >= maxPx ? 0 : fit(rest, rn, static_cast<uint16_t>(maxPx - dots), w);
  while (c && rest[c - 1] == ' ') c--;
  memcpy(out.line2, rest, c);
  memcpy(out.line2 + c, "...", 4);
}
```

- [ ] **Step 4: Run, confirm pass**

Run: `pio test -e native -f test_legend`
Expected: 15 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/legend.h test/native/test_legend/test_main.cpp
git commit -m "deck_core: legend text (derived labels, 2-line wrap by glyph width)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Press feedback model — TileAnim, blend565, Deck key state

**Files:**
- Create: `lib/deck_core/src/tile_anim.h`
- Modify: `lib/deck_core/src/deck.h`
- Test: `test/native/test_legend/test_main.cpp` (TileAnim, blend565)
- Test: `test/native/test_deck/test_main.cpp` (keysHeld, helperMissingKeys)

**Interfaces:**
- Consumes: `ActionList`, `ActionType::HelperMissing` (value = key index 0–11, or 0xFF for knob 2), `DECK_KEYS`, `kKeyCount`.
- Produces:
  - `class TileAnim` with `kTiles = 12`, `kFull = 4`, `kStepMs = 60`; `void onKeys(uint16_t held, uint16_t failed, uint32_t nowMs);` `uint16_t tick(uint32_t nowMs);` (mask of tiles whose look changed since the previous `tick`) `uint8_t level(uint8_t i) const;` (0 idle … 4 pressed, as of last `tick`) `bool failed(uint8_t i) const;`
  - `uint16_t blend565(uint16_t a, uint16_t b, uint8_t num, uint8_t den);` (RGB565 mix, `num/den` of `b`)
  - `uint16_t Deck::keysHeld() const;` (debounced key bits 0–11)
  - `uint16_t helperMissingKeys(const ActionList &a);`

- [ ] **Step 1: Write failing tests (TileAnim, blend565)**

In `test/native/test_legend/test_main.cpp` add `#include "tile_anim.h"` under the other includes, and these tests above `int main`:

```cpp
// --- TileAnim: held = full fill, release fades 3/4, 2/4, 1/4, 0 at 60 ms steps ---

void test_anim_idle_nothing_dirty() {
  TileAnim a;
  TEST_ASSERT_EQUAL_HEX16(0, a.tick(0));
  TEST_ASSERT_EQUAL_UINT8(0, a.level(0));
}

void test_anim_press_fills_tile_once() {
  TileAnim a;
  a.onKeys(1u << 2, 0, 100);
  TEST_ASSERT_EQUAL_HEX16(1u << 2, a.tick(100));
  TEST_ASSERT_EQUAL_UINT8(TileAnim::kFull, a.level(2));
  TEST_ASSERT_FALSE(a.failed(2));
  a.onKeys(1u << 2, 0, 110);
  TEST_ASSERT_EQUAL_HEX16(0, a.tick(110));  // still held: no repaint
}

void test_anim_release_fades_in_steps() {
  TileAnim a;
  a.onKeys(1u << 0, 0, 0);
  a.tick(0);
  a.onKeys(0, 0, 200);
  TEST_ASSERT_EQUAL_HEX16(1u << 0, a.tick(200));
  TEST_ASSERT_EQUAL_UINT8(3, a.level(0));
  TEST_ASSERT_EQUAL_HEX16(0, a.tick(259));
  TEST_ASSERT_EQUAL_HEX16(1u << 0, a.tick(260));
  TEST_ASSERT_EQUAL_UINT8(2, a.level(0));
  a.tick(320);
  TEST_ASSERT_EQUAL_UINT8(1, a.level(0));
  TEST_ASSERT_EQUAL_HEX16(1u << 0, a.tick(380));
  TEST_ASSERT_EQUAL_UINT8(0, a.level(0));
  TEST_ASSERT_EQUAL_HEX16(0, a.tick(1000));
}

void test_anim_steps_between_frames_merge() {
  TileAnim a;
  a.onKeys(1u << 4, 0, 0);
  a.tick(0);
  a.onKeys(0, 0, 10);
  TEST_ASSERT_EQUAL_HEX16(1u << 4, a.tick(500));  // 4 -> 0 in one repaint
  TEST_ASSERT_EQUAL_UINT8(0, a.level(4));
}

void test_anim_failed_press_until_next_good_press() {
  TileAnim a;
  a.onKeys(1u << 5, 1u << 5, 0);
  a.tick(0);
  TEST_ASSERT_TRUE(a.failed(5));
  a.onKeys(0, 0, 100);
  a.tick(400);
  a.onKeys(1u << 5, 0, 500);
  a.tick(500);
  TEST_ASSERT_FALSE(a.failed(5));
}

void test_anim_press_during_fade_refills() {
  TileAnim a;
  a.onKeys(1u << 1, 0, 0);
  a.tick(0);
  a.onKeys(0, 0, 100);
  a.tick(160);
  TEST_ASSERT_EQUAL_UINT8(2, a.level(1));
  a.onKeys(1u << 1, 0, 170);
  TEST_ASSERT_EQUAL_HEX16(1u << 1, a.tick(170));
  TEST_ASSERT_EQUAL_UINT8(TileAnim::kFull, a.level(1));
}

void test_anim_ignores_bits_above_12() {
  TileAnim a;
  a.onKeys(0xF000, 0xF000, 0);
  TEST_ASSERT_EQUAL_HEX16(0, a.tick(0));
}

// --- blend565: RGB565 mix, num/den of b ---

void test_blend_endpoints_and_half() {
  TEST_ASSERT_EQUAL_HEX16(0x0000, blend565(0x0000, 0xFFFF, 0, 4));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, blend565(0x0000, 0xFFFF, 4, 4));
  TEST_ASSERT_EQUAL_HEX16(0x7800, blend565(0x0000, 0xF800, 2, 4));  // red 31 * 2/4 = 15
  TEST_ASSERT_EQUAL_HEX16(0x03E0, blend565(0x0000, 0x07E0, 2, 4));  // green 63 * 2/4 = 31
}
```

Register in `main` after the wrap tests:

```cpp
  RUN_TEST(test_anim_idle_nothing_dirty);
  RUN_TEST(test_anim_press_fills_tile_once);
  RUN_TEST(test_anim_release_fades_in_steps);
  RUN_TEST(test_anim_steps_between_frames_merge);
  RUN_TEST(test_anim_failed_press_until_next_good_press);
  RUN_TEST(test_anim_press_during_fade_refills);
  RUN_TEST(test_anim_ignores_bits_above_12);
  RUN_TEST(test_blend_endpoints_and_half);
```

- [ ] **Step 2: Write failing tests (Deck)**

In `test/native/test_deck/test_main.cpp` add above `int main`:

```cpp
void test_deck_keys_held_tracks_debounced_keys() {
  Deck d;
  uint32_t t = 0;
  hold(d, (1u << 3) | (1u << 7), t);
  TEST_ASSERT_EQUAL_HEX16((1u << 3) | (1u << 7), d.keysHeld());
  hold(d, (1u << 3) | DECK_ENC1_SW, t);
  TEST_ASSERT_EQUAL_HEX16(1u << 3, d.keysHeld());  // knob switches are not keys
}

void test_helper_missing_keys_mask() {
  ActionList a;
  a.push(ActionType::HelperMissing, 2);
  a.push(ActionType::HelperMissing, 0xFF);  // knob 2 gesture, not a tile
  a.push(ActionType::KeyDown, 5);
  TEST_ASSERT_EQUAL_HEX16(1u << 2, helperMissingKeys(a));
}
```

and register both with `RUN_TEST(...)` at the end of `main`'s list.

- [ ] **Step 3: Run, confirm failure**

Run: `pio test -e native -f test_legend -f test_deck`
Expected: compile errors `tile_anim.h: No such file or directory`, `'class Deck' has no member named 'keysHeld'`, `'helperMissingKeys' was not declared`.

- [ ] **Step 4: Implement TileAnim**

Create `lib/deck_core/src/tile_anim.h`:

```cpp
#pragma once
#include <stdint.h>

// Legend tile press feedback, hardware-free. A held key's tile is filled (level kFull);
// on release it fades back over kFull - 1 steps of kStepMs. "Failed" = the press could not
// run (helper not running): same animation, warning colour. The glue repaints the tiles
// tick() names and reads level()/failed() for each.
class TileAnim {
 public:
  static constexpr uint8_t kTiles = 12;
  static constexpr uint8_t kFull = 4;
  static constexpr uint32_t kStepMs = 60;

  void onKeys(uint16_t held, uint16_t failed, uint32_t nowMs) {
    held &= kMask;
    uint16_t down = held & static_cast<uint16_t>(~held_);
    uint16_t up = held_ & static_cast<uint16_t>(~held);
    failed_ = static_cast<uint16_t>((failed_ & ~down) | (failed & held));
    for (uint8_t i = 0; i < kTiles; i++)
      if (up & (1u << i)) releasedAt_[i] = nowMs;
    fading_ = static_cast<uint16_t>((fading_ | up) & ~held);
    held_ = held;
  }

  uint16_t tick(uint32_t nowMs) {
    uint16_t changed = 0;
    for (uint8_t i = 0; i < kTiles; i++) {
      uint16_t bit = 1u << i;
      uint8_t lv = 0;
      if (held_ & bit) {
        lv = kFull;
      } else if (fading_ & bit) {
        uint32_t step = (nowMs - releasedAt_[i]) / kStepMs;
        lv = step >= kFull - 1u ? 0 : static_cast<uint8_t>(kFull - 1 - step);
        if (lv == 0) fading_ = static_cast<uint16_t>(fading_ & ~bit);
      }
      uint8_t look = lv ? static_cast<uint8_t>(lv | ((failed_ & bit) ? 0x80 : 0)) : 0;
      if (look != look_[i]) changed |= bit;
      look_[i] = look;
    }
    return changed;
  }

  uint8_t level(uint8_t i) const { return i < kTiles ? look_[i] & 0x7F : 0; }
  bool failed(uint8_t i) const { return i < kTiles && (failed_ & (1u << i)); }

 private:
  static constexpr uint16_t kMask = (1u << kTiles) - 1;
  uint16_t held_ = 0, fading_ = 0, failed_ = 0;
  uint32_t releasedAt_[kTiles] = {};
  uint8_t look_[kTiles] = {};  // level | 0x80 when failed, as of the last tick()
};

// RGB565 mix: num/den of b, the rest of a (per channel, rounded down).
inline uint16_t blend565(uint16_t a, uint16_t b, uint8_t num, uint8_t den) {
  if (den == 0 || num >= den) return num ? b : a;
  uint32_t keep = den - num;
  uint32_t r = ((a >> 11) * keep + (b >> 11) * num) / den;
  uint32_t g = (((a >> 5) & 0x3F) * keep + ((b >> 5) & 0x3F) * num) / den;
  uint32_t bl = ((a & 0x1F) * keep + (b & 0x1F) * num) / den;
  return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}
```

- [ ] **Step 5: Implement Deck additions**

In `lib/deck_core/src/deck.h`, directly after `struct ActionList { ... };` add:

```cpp
// Keys whose press could not run (App/Url/Shortcut while the helper is absent); bit i = key i.
inline uint16_t helperMissingKeys(const ActionList &a) {
  uint16_t m = 0;
  for (size_t i = 0; i < a.count; i++)
    if (a.items[i].type == ActionType::HelperMissing && a.items[i].value < kKeyCount)
      m = static_cast<uint16_t>(m | (1u << a.items[i].value));
  return m;
}
```

In `class Deck` public section (next to the other getters, e.g. after `onKeys`):

```cpp
  uint16_t keysHeld() const { return held_ & DECK_KEYS; }  // debounced, bit i = key i
```

If `deck.h` does not already see `kKeyCount` before `helperMissingKeys`, move the new function below the existing `#include "keymap.h"` line (the class already uses `KeyMap`, so the include exists).

- [ ] **Step 6: Run, confirm pass**

Run: `pio test -e native`
Expected: all suites PASS, test_legend now 23 tests, test_deck +2.

- [ ] **Step 7: Commit**

```bash
git add lib/deck_core/src/tile_anim.h lib/deck_core/src/deck.h test/native/test_legend/test_main.cpp test/native/test_deck/test_main.cpp
git commit -m "deck_core: tile press/fade animation model, RGB565 blend, Deck key state

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: ScreenModel v2 + new screen layout in display.cpp + main wiring

One task because changing `ScreenState` breaks `display.cpp` and the sim build until all three move together.

**Files:**
- Modify: `lib/deck_core/src/ui_layout.h`
- Modify: `test/native/test_ui/test_main.cpp`
- Rewrite: `src/display.h`, `src/display.cpp`
- Modify: `src/main.cpp`
- Modify: `test/sim/lib/simdeck/src/Adafruit_GFX.h`, `Adafruit_ST7789.h`, `fakes.cpp`, `sim/lcd.h`
- Create: `test/sim/lib/simdeck/src/Fonts/FreeSans9pt7b.h`

**Interfaces:**
- Consumes: Task 1 `KeyMap`/`label`, Task 2 `displayLabel`/`wrapLabel`/`LabelLines`/`kLegendLineMax`, Task 3 `TileAnim`/`blend565`/`Deck::keysHeld`/`helperMissingKeys`.
- Produces:
  - `struct ScreenState { uint8_t volumePercent; bool speakerMuted; bool micMuted; bool micLevelKnown; uint8_t micPercent; uint8_t banner; uint8_t bannerArg; uint8_t keymapGen; bool helperPresent; bool wifiOn; };`
  - Dirty flags (uint16_t): `DIRTY_BAR`, `DIRTY_BAR_COLOR`, `DIRTY_VOLUME_TEXT`, `DIRTY_MIC`, `DIRTY_BANNER`, `DIRTY_TOPBAR`, `DIRTY_TILES`, `DIRTY_ALL`.
  - `uint16_t ScreenModel::update(const ScreenState &next, uint16_t tileChanges);` `uint16_t ScreenModel::tileMask() const;`
  - `struct Span { uint16_t x; uint16_t w; bool filled; };` `Span barDeltaH(uint16_t oldFill, uint16_t newFill, uint16_t innerLeft);`
  - `void displayUpdate(const ScreenState &state, const KeyMap &keys, TileAnim &anim, uint32_t nowMs);`
  - Sim: `GFXglyph`, `GFXfont`, `FreeSans9pt7b` (10 px advance), `Adafruit_ST7789::setFont`, `fillCircle`, `SimLcdText::customFont`.

- [ ] **Step 1: Rewrite the UI tests (failing)**

In `test/native/test_ui/test_main.cpp`, replace everything from the `// --- barDelta` comment through `test_model_banner_change_marks_banner` (inclusive) with:

```cpp
// --- barDeltaH: only the span between old and new fill is redrawn (bar grows rightward) ---

void test_delta_grow_paints_span_after_old_fill() {
  Span s = barDeltaH(10, 30, 5);
  TEST_ASSERT_TRUE(s.filled);
  TEST_ASSERT_EQUAL_UINT16(15, s.x);
  TEST_ASSERT_EQUAL_UINT16(20, s.w);
}

void test_delta_shrink_clears_span() {
  Span s = barDeltaH(30, 10, 5);
  TEST_ASSERT_FALSE(s.filled);
  TEST_ASSERT_EQUAL_UINT16(15, s.x);
  TEST_ASSERT_EQUAL_UINT16(20, s.w);
}

void test_delta_no_change_is_empty() {
  TEST_ASSERT_EQUAL_UINT16(0, barDeltaH(42, 42, 5).w);
}

// --- ScreenModel v2: dirty flags + tiles to repaint ---

static ScreenState base() {
  ScreenState s{};
  s.volumePercent = 20;
  s.banner = BANNER_NONE;
  return s;
}

void test_model_first_frame_all_dirty_all_tiles() {
  ScreenModel m;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_ALL, m.update(base(), 0));
  TEST_ASSERT_EQUAL_HEX16(0x0FFF, m.tileMask());
}

void test_model_same_state_clean() {
  ScreenModel m;
  m.update(base(), 0);
  TEST_ASSERT_EQUAL_HEX16(DIRTY_NONE, m.update(base(), 0));
  TEST_ASSERT_EQUAL_HEX16(0, m.tileMask());
}

void test_model_volume_change_marks_bar_and_text() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.volumePercent = 26;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_BAR | DIRTY_VOLUME_TEXT, m.update(s, 0));
}

void test_model_speaker_mute_marks_color_and_text() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.speakerMuted = true;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_BAR_COLOR | DIRTY_VOLUME_TEXT, m.update(s, 0));
}

void test_model_mic_changes_mark_mic_only() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.micMuted = true;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_MIC, m.update(s, 0));
  s.micLevelKnown = true;
  s.micPercent = 45;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_MIC, m.update(s, 0));
}

void test_model_banner_change_marks_banner() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.banner = BANNER_APPLY;
  s.bannerArg = 30;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_BANNER, m.update(s, 0));
  s.bannerArg = 29;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_BANNER, m.update(s, 0));
}

void test_model_helper_and_wifi_mark_top_bar() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.helperPresent = true;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TOPBAR, m.update(s, 0));
  s.wifiOn = true;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TOPBAR, m.update(s, 0));
}

void test_model_tile_changes_pass_through() {
  ScreenModel m;
  m.update(base(), 0);
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TILES, m.update(base(), (1u << 3) | (1u << 9)));
  TEST_ASSERT_EQUAL_HEX16((1u << 3) | (1u << 9), m.tileMask());
}

void test_model_keymap_change_repaints_every_tile() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.keymapGen = 1;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TILES, m.update(s, 0));
  TEST_ASSERT_EQUAL_HEX16(0x0FFF, m.tileMask());
}

void test_model_previous_tracks_last_drawn() {
  ScreenModel m;
  ScreenState s = base();
  s.volumePercent = 50;
  m.update(s, 0);
  s.volumePercent = 75;
  m.update(s, 0);
  TEST_ASSERT_EQUAL_UINT8(75, m.current().volumePercent);
  TEST_ASSERT_EQUAL_UINT8(50, m.previous().volumePercent);
}

void test_model_invalidate_forces_full_redraw() {
  ScreenModel m;
  m.update(base(), 0);
  m.invalidate();
  TEST_ASSERT_EQUAL_HEX16(DIRTY_ALL, m.update(base(), 0));
  TEST_ASSERT_EQUAL_HEX16(0x0FFF, m.tileMask());
}
```

Replace the matching `RUN_TEST` lines in `main` (the three `test_delta_*` and all `test_model_*`) with:

```cpp
  RUN_TEST(test_delta_grow_paints_span_after_old_fill);
  RUN_TEST(test_delta_shrink_clears_span);
  RUN_TEST(test_delta_no_change_is_empty);
  RUN_TEST(test_model_first_frame_all_dirty_all_tiles);
  RUN_TEST(test_model_same_state_clean);
  RUN_TEST(test_model_volume_change_marks_bar_and_text);
  RUN_TEST(test_model_speaker_mute_marks_color_and_text);
  RUN_TEST(test_model_mic_changes_mark_mic_only);
  RUN_TEST(test_model_banner_change_marks_banner);
  RUN_TEST(test_model_helper_and_wifi_mark_top_bar);
  RUN_TEST(test_model_tile_changes_pass_through);
  RUN_TEST(test_model_keymap_change_repaints_every_tile);
  RUN_TEST(test_model_previous_tracks_last_drawn);
  RUN_TEST(test_model_invalidate_forces_full_redraw);
```

Keep the `barFill` and `IdleDimmer` tests as they are.

- [ ] **Step 2: Run, confirm failure**

Run: `pio test -e native -f test_ui`
Expected: compile errors `'Span' was not declared`, `'barDeltaH' was not declared`, `'DIRTY_TOPBAR' was not declared`.

- [ ] **Step 3: Implement ScreenModel v2**

In `lib/deck_core/src/ui_layout.h` replace the header comment, `struct ScreenState`, the dirty enum, `struct Strip`, `barDelta` and `class ScreenModel` (keep the `BANNER_*` enum and `barFill`) with:

```cpp
// Screen model, hardware-free: top bar, 4x3 legend tiles, status strip (volume bar, mic,
// banner line). The glue redraws only what the dirty flags name, only the tiles in
// tileMask(), and only the span of the bar that changed: a full clear flickers and stalls
// the key scan while SPI is busy (~61 ms per full frame at 20 MHz).
```

```cpp
struct ScreenState {
  uint8_t volumePercent;
  bool speakerMuted;
  bool micMuted;
  bool micLevelKnown;  // helper connected: show real mic %
  uint8_t micPercent;
  uint8_t banner;      // BANNER_*
  uint8_t bannerArg;   // countdown seconds, key number, ...
  uint8_t keymapGen;   // bumped when a key map goes live: every tile repaints
  bool helperPresent;  // top bar dot
  bool wifiOn;         // top bar dot
};

enum : uint16_t {
  DIRTY_NONE = 0,
  DIRTY_BAR = 1u << 0,          // fill level changed: redraw barDeltaH() span
  DIRTY_BAR_COLOR = 1u << 1,    // mute changed: repaint whole bar interior
  DIRTY_VOLUME_TEXT = 1u << 2,  // "62%" / "MUTED"
  DIRTY_MIC = 1u << 3,
  DIRTY_BANNER = 1u << 4,       // strip line 2 (Wi-Fi, Apply?, helper missing, ...)
  DIRTY_TOPBAR = 1u << 5,       // helper / Wi-Fi dots
  DIRTY_TILES = 1u << 6,        // repaint the tiles in tileMask()
  DIRTY_ALL = 0x7F,
};

struct Span {
  uint16_t x;
  uint16_t w;
  bool filled;  // true = paint fill color, false = paint background
};

// Span to repaint when the fill goes from oldFill to newFill. Bar grows rightward from
// innerLeft.
inline Span barDeltaH(uint16_t oldFill, uint16_t newFill, uint16_t innerLeft) {
  uint16_t hi = oldFill > newFill ? oldFill : newFill;
  uint16_t lo = oldFill > newFill ? newFill : oldFill;
  return {static_cast<uint16_t>(innerLeft + lo), static_cast<uint16_t>(hi - lo), newFill > oldFill};
}

class ScreenModel {
 public:
  static constexpr uint16_t kAllTiles = 0x0FFF;

  uint16_t update(const ScreenState &next, uint16_t tileChanges) {
    uint16_t dirty = DIRTY_NONE;
    tiles_ = tileChanges & kAllTiles;
    if (first_) {
      dirty = DIRTY_ALL;
      tiles_ = kAllTiles;
      first_ = false;
    } else {
      if (next.volumePercent != cur_.volumePercent) dirty |= DIRTY_BAR | DIRTY_VOLUME_TEXT;
      if (next.speakerMuted != cur_.speakerMuted) dirty |= DIRTY_BAR_COLOR | DIRTY_VOLUME_TEXT;
      if (next.micMuted != cur_.micMuted) dirty |= DIRTY_MIC;
      if (next.micLevelKnown != cur_.micLevelKnown || next.micPercent != cur_.micPercent) dirty |= DIRTY_MIC;
      if (next.banner != cur_.banner || next.bannerArg != cur_.bannerArg) dirty |= DIRTY_BANNER;
      if (next.helperPresent != cur_.helperPresent || next.wifiOn != cur_.wifiOn) dirty |= DIRTY_TOPBAR;
      if (next.keymapGen != cur_.keymapGen) tiles_ = kAllTiles;
      if (tiles_) dirty |= DIRTY_TILES;
    }
    prev_ = cur_;
    cur_ = next;
    return dirty;
  }

  uint16_t tileMask() const { return tiles_; }  // bit i = key position i + 1
  void invalidate() { first_ = true; }
  const ScreenState &current() const { return cur_; }
  const ScreenState &previous() const { return prev_; }

 private:
  ScreenState cur_{}, prev_{};
  uint16_t tiles_ = 0;
  bool first_ = true;
};
```

- [ ] **Step 4: Run UI tests, confirm pass**

Run: `pio test -e native -f test_ui`
Expected: PASS (3 barFill + 3 delta + 11 model + 4 dimmer = 21).

- [ ] **Step 5: Sim fakes — font types, setFont, fillCircle**

`test/sim/lib/simdeck/src/Adafruit_GFX.h` becomes:

```cpp
#pragma once
#include "Arduino.h"
// Drawing primitives are declared on Adafruit_ST7789 (Adafruit_ST7789.h) in the sim.

// gfxfont.h layout (Adafruit GFX 1.12.6)
typedef struct {
  uint16_t bitmapOffset;
  uint8_t width;
  uint8_t height;
  uint8_t xAdvance;
  int8_t xOffset;
  int8_t yOffset;
} GFXglyph;

typedef struct {
  uint8_t *bitmap;
  GFXglyph *glyph;
  uint16_t first;
  uint16_t last;
  uint8_t yAdvance;
} GFXfont;
```

Create `test/sim/lib/simdeck/src/Fonts/FreeSans9pt7b.h`:

```cpp
#pragma once
#include "Adafruit_GFX.h"

// Stand-in for Adafruit GFX Fonts/FreeSans9pt7b.h: same name and range (0x20-0x7E), but
// every glyph advances 10 px (real advances vary ~5-17 px). Sim checks layout logic, not
// pixel widths; legibility is a bench check.
struct SimFreeSansGlyphs {
  GFXglyph g[0x7E - 0x20 + 1];
  constexpr SimFreeSansGlyphs() : g() {
    for (GFXglyph &x : g) x = GFXglyph{0, 0, 0, 10, 0, 0};
  }
};
inline constexpr SimFreeSansGlyphs kSimFreeSansGlyphs{};
inline const GFXfont FreeSans9pt7b = {nullptr, const_cast<GFXglyph *>(kSimFreeSansGlyphs.g), 0x20, 0x7E, 22};
```

In `test/sim/lib/simdeck/src/sim/lcd.h`, add a field at the end of `SimLcdText`:

```cpp
  bool customFont;  // printed with setFont(&FreeSans9pt7b) (legend tiles)
```

In `test/sim/lib/simdeck/src/Adafruit_ST7789.h`, inside `class Adafruit_ST7789` public section add:

```cpp
  void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t c);
  void setFont(const GFXfont *f = nullptr) { font_ = f; }
```

and to the private members:

```cpp
  const GFXfont *font_ = nullptr;
```

In `test/sim/lib/simdeck/src/fakes.cpp` LCD section, add:

```cpp
void Adafruit_ST7789::fillCircle(int16_t, int16_t, int16_t, uint16_t) {}
```

and change `print` to record the font:

```cpp
size_t Adafruit_ST7789::print(const char *s) {
  board().lcd.texts.push_back({cx_, cy_, textSize_, fg_, s, font_ != nullptr});
  return strlen(s);
}
```

- [ ] **Step 6: Rewrite display.h**

`src/display.h`:

```cpp
#pragma once
#include <stdint.h>
#include "keymap.h"
#include "tile_anim.h"
#include "ui_layout.h"

// ST7789 screen: top bar, 4x3 key legends, status strip. Logic (what changed, label text,
// press animation) lives in deck_core: ui_layout.h, legend.h, tile_anim.h.
void displayBegin();
// anim.tick() runs here, once per drawn frame, so no animation step is lost between frames.
void displayUpdate(const ScreenState &state, const KeyMap &keys, TileAnim &anim, uint32_t nowMs);
void displaySetDim(uint8_t level);  // 255 full .. 0 backlight off + panel sleep
void displaySetApPassword(const char *p);  // shown in the SAUCEDECK-setup banner
void displaySetWifiInfo(const char *info);  // shown with the Wi-Fi joined / starting banners
```

- [ ] **Step 7: Rewrite display.cpp**

Replace `src/display.cpp` entirely with:

```cpp
#include "display.h"
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>
#include "driver/gpio.h"
#include "legend.h"
#include "pins.h"

// 240x320 panel, landscape. 1 was upside down in the case (bench 2026-10-18) -> 3.
static constexpr uint8_t kRotation = 3;
static constexpr uint32_t kSpiHz = 20000000;  // bench-verified (docs/kb/peripherals.md)
static constexpr uint32_t kSplashMs = 1500;
static constexpr uint32_t kMinFrameMs = 30;  // batch fast knob turns into one redraw

// Layout (landscape 320x240): top bar, 4x3 legend grid (key positions 1-12, row-major),
// status strip with two lines. Spec: docs/superpowers/specs/2026-10-31-lcd-legends-status-design.md
static constexpr int16_t kW = 320;
static constexpr int16_t kTopH = 20;
static constexpr int16_t kTileW = 80, kTileH = 56, kGridY = kTopH;  // grid y 20..187
static constexpr int16_t kStripeW = 3, kLabelPad = 3;
static constexpr int16_t kTextW = kTileW - kStripeW - 2 * kLabelPad;  // 71 px per label line
static constexpr int16_t kLine1Y = 190, kLine2Y = 214, kLine2H = 26;
static constexpr int16_t kBarX = 4, kBarW = 120, kBarH = 18;  // outline; interior 1 px inside
static constexpr int16_t kPctX = 132, kMicX = 204;

static constexpr uint16_t kBg = ST77XX_BLACK;
static constexpr uint16_t kFg = ST77XX_WHITE;
static constexpr uint16_t kTileBg = 0x2104;  // dark grey
static constexpr uint16_t kBarOn = ST77XX_CYAN;
static constexpr uint16_t kGrey = 0x4208;
static constexpr uint16_t kRed = ST77XX_RED;
static constexpr uint16_t kGreen = 0x07E0;
static constexpr uint16_t kBlue = 0x041F;
static constexpr uint16_t kPurple = 0xA01F;
static constexpr uint16_t kOrange = 0xFD20;  // failed press (helper not running)

static Adafruit_ST7789 tft(&SPI, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
static ScreenModel model;
static uint32_t splashUntil = 0;
static uint32_t lastFrame = 0;
static bool splashing = true;

static uint16_t typeColor(KeyType t) {
  switch (t) {
    case KeyType::Keystroke: return 0x7BEF;  // grey
    case KeyType::App: return kBlue;
    case KeyType::Url: return kGreen;
    case KeyType::Shortcut: return kPurple;
  }
  return kGrey;
}

static uint8_t glyphAdvance(char c) {
  uint8_t u = static_cast<uint8_t>(c);
  if (u < FreeSans9pt7b.first || u > FreeSans9pt7b.last) return 0;
  return FreeSans9pt7b.glyph[u - FreeSans9pt7b.first].xAdvance;
}

static void printCentered(const char *s, int16_t x0, int16_t w, int16_t baseline) {
  int16_t px = 0;
  for (const char *p = s; *p; p++) px += glyphAdvance(*p);
  tft.setCursor(x0 + (w - px) / 2, baseline);
  tft.print(s);
}

// Tile i = key position i + 1. Fill blends from the idle grey to the accent by the press
// level; custom fonts draw no background, so the fill always goes first.
static void drawTile(uint8_t i, const KeyEntry &e, uint8_t level, bool failed) {
  const int16_t x = (i % 4) * kTileW, y = kGridY + (i / 4) * kTileH;
  const uint16_t accent = failed ? kOrange : typeColor(e.type);
  tft.fillRect(x + 1, y + 1, kTileW - 2, kTileH - 2, blend565(kTileBg, accent, level, TileAnim::kFull));
  tft.fillRect(x + 1, y + 1, kStripeW, kTileH - 2, accent);

  char text[kLegendLineMax + 1];
  displayLabel(e, text, sizeof text);
  LabelLines lines;
  wrapLabel(text, kTextW, glyphAdvance, lines);
  const int16_t textX = x + kStripeW + kLabelPad;
  const uint16_t fg = level * 2 >= TileAnim::kFull ? kBg : kFg;  // dark text on a mostly-accent fill
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(fg, fg);
  if (lines.line2[0]) {
    printCentered(lines.line1, textX, kTextW, y + 24);
    printCentered(lines.line2, textX, kTextW, y + 45);
  } else {
    printCentered(lines.line1, textX, kTextW, y + 34);
  }
  tft.setFont(nullptr);
}

static void drawTopBar(const ScreenState &s) {
  tft.fillRect(0, 0, kW, kTopH, kBg);
  tft.fillCircle(296, 10, 5, s.helperPresent ? kGreen : kGrey);
  if (s.wifiOn) tft.fillCircle(310, 10, 5, kBlue);
}

static uint16_t barColor(bool muted) { return muted ? kGrey : kBarOn; }

static void drawBarFull(const ScreenState &s) {
  const uint16_t inner = kBarW - 2, fill = barFill(s.volumePercent, inner);
  tft.drawRect(kBarX, kLine1Y, kBarW, kBarH, kFg);
  tft.fillRect(kBarX + 1, kLine1Y + 1, fill, kBarH - 2, barColor(s.speakerMuted));
  tft.fillRect(kBarX + 1 + fill, kLine1Y + 1, inner - fill, kBarH - 2, kBg);
}

static void drawBarDelta(const ScreenState &prev, const ScreenState &cur) {
  const uint16_t inner = kBarW - 2;
  Span sp = barDeltaH(barFill(prev.volumePercent, inner), barFill(cur.volumePercent, inner), kBarX + 1);
  if (sp.w) tft.fillRect(sp.x, kLine1Y + 1, sp.w, kBarH - 2, sp.filled ? barColor(cur.speakerMuted) : kBg);
}

static void drawVolumeText(const ScreenState &s) {
  char buf[8];
  if (s.speakerMuted) snprintf(buf, sizeof buf, "MUTED");
  else snprintf(buf, sizeof buf, "%3u%% ", s.volumePercent);  // 5 chars: overwrites "MUTED"
  tft.setTextSize(2);
  tft.setTextColor(s.speakerMuted ? kRed : kFg, kBg);  // opaque text: no clear, no flicker
  tft.setCursor(kPctX, kLine1Y + 2);
  tft.print(buf);
}

static void drawMic(const ScreenState &s) {
  char buf[10];
  if (s.micMuted) snprintf(buf, sizeof buf, "MIC MUTE");
  else if (s.micLevelKnown) snprintf(buf, sizeof buf, "MIC %3u%%", s.micPercent);
  else snprintf(buf, sizeof buf, "MIC LIVE");
  tft.setTextSize(2);
  tft.setTextColor(s.micMuted ? kRed : kGreen, kBg);
  tft.setCursor(kMicX, kLine1Y + 2);
  tft.print(buf);
}

static void drawSplash() {
  tft.fillScreen(kBg);
  tft.setTextColor(kFg, kBg);
  tft.setTextSize(4);
  tft.setCursor((320 - 9 * 24) / 2, 90);
  tft.print("SAUCEDECK");
  tft.setTextSize(2);
  tft.setCursor((320 - 12 * 12) / 2, 140);
  tft.print("macro deck v1");
}

void displayBegin() {
  // BL on GPIO9: 19 mA measured 2026-10-24 (limit 20 mA). Reduced drive strength stays.
  // PWM so the backlight can fade out when idle (IdleDimmer). 5 kHz, 8 bit, core 3.x API.
  ledcAttach(TFT_BL_PIN, 5000, 8);
  gpio_set_drive_capability(static_cast<gpio_num_t>(TFT_BL_PIN), GPIO_DRIVE_CAP_1);
  ledcWrite(TFT_BL_PIN, 255);

  SPI.begin(TFT_SCL_PIN, -1, TFT_SDA_PIN, -1);  // MISO -1: GPIO13 is LCD DC
  tft.init(240, 320);
  tft.setSPISpeed(kSpiHz);
  tft.setRotation(kRotation);
  tft.setTextWrap(false);
  drawSplash();
  splashUntil = millis() + kSplashMs;
}

static char apPassword[9] = "";
static char wifiInfo[28] = "";
void displaySetWifiInfo(const char *info) { snprintf(wifiInfo, sizeof wifiInfo, "%s", info); }
void displaySetApPassword(const char *p) { snprintf(apPassword, sizeof apPassword, "%s", p); }

// Strip line 2. Plan 2 shares this line with status cards (banner wins).
static void drawBanner(uint8_t b, uint8_t arg) {
  tft.fillRect(0, kLine2Y, kW, kLine2H, kBg);
  if (b == BANNER_NONE) return;
  char text[40];
  switch (b) {
    case BANNER_WIFI_COUNTDOWN: snprintf(text, sizeof text, "Wi-Fi in %us", arg); break;
    case BANNER_WIFI_STARTING: snprintf(text, sizeof text, "Wi-Fi.. %s", wifiInfo); break;
    case BANNER_WIFI_PORTAL: snprintf(text, sizeof text, "SETUP %s", apPassword); break;
    case BANNER_WIFI_JOINED: snprintf(text, sizeof text, "%s", wifiInfo); break;  // IP + RSSI
    case BANNER_APPLY: snprintf(text, sizeof text, "Apply? knob1 %us", arg); break;
    case BANNER_HELPER_MISSING: snprintf(text, sizeof text, "helper not running"); break;
    case BANNER_KEYMAP_RESET: snprintf(text, sizeof text, "Key map reset"); break;
    case BANNER_WIFI_FAILED: snprintf(text, sizeof text, "Wi-Fi failed"); break;
    case BANNER_INVALID_KEY: snprintf(text, sizeof text, "Invalid key %u", arg); break;
    case BANNER_BROWNOUT: snprintf(text, sizeof text, "Reset: brownout"); break;
    default: text[0] = 0; break;
  }
  tft.setTextSize(2);
  tft.setTextColor(b == BANNER_APPLY ? ST77XX_YELLOW : kFg, kBg);
  tft.setCursor(4, kLine2Y + 5);
  tft.print(text);
}

void displayUpdate(const ScreenState &state, const KeyMap &keys, TileAnim &anim, uint32_t nowMs) {
  if (splashing) {
    if (static_cast<int32_t>(nowMs - splashUntil) < 0) return;
    splashing = false;
    tft.fillScreen(kBg);
    model.invalidate();
  }
  if (nowMs - lastFrame < kMinFrameMs) return;
  lastFrame = nowMs;

  uint16_t dirty = model.update(state, anim.tick(nowMs));
  if (!dirty) return;
  const ScreenState &cur = model.current();
  if (dirty & DIRTY_TOPBAR) drawTopBar(cur);
  if (dirty & DIRTY_TILES) {
    uint16_t m = model.tileMask();
    for (uint8_t i = 0; i < kKeyCount; i++)
      if (m & (1u << i)) drawTile(i, keys.entry(i), anim.level(i), anim.failed(i));
  }
  if (dirty & DIRTY_BAR_COLOR) drawBarFull(cur);
  else if (dirty & DIRTY_BAR) drawBarDelta(model.previous(), cur);
  if (dirty & DIRTY_VOLUME_TEXT) drawVolumeText(cur);
  if (dirty & DIRTY_MIC) drawMic(cur);
  if (dirty & DIRTY_BANNER) drawBanner(cur.banner, cur.bannerArg);
}

static bool panelAsleep = false;

void displaySetDim(uint8_t level) {
  ledcWrite(TFT_BL_PIN, static_cast<uint32_t>(level) * level / 255);  // squared: perceptual fade
  if (level == 0 && !panelAsleep) {
    tft.enableSleep(true);  // SLPIN: panel keeps its frame, draws almost nothing
    panelAsleep = true;
  } else if (level > 0 && panelAsleep) {
    tft.enableSleep(false);
    panelAsleep = false;
  }
}
```

Note: the old comment "BL current not measured yet" is replaced with the 2026-10-24 measurement (CLAUDE.md).

- [ ] **Step 8: Wire main.cpp**

In `src/main.cpp`:

Add include after `#include "idle_dimmer.h"`:

```cpp
#include "tile_anim.h"
```

Add statics after `static IdleDimmer dimmer;`:

```cpp
static TileAnim tileAnim;    // legend tile press feedback
static uint8_t keymapGen = 0;  // bumped when a key map goes live: legends repaint
```

In `run()`, change the commit case to:

```cpp
      case ActionType::KeyMapCommitted:
        keymapSave(deck.keyMap());
        keymapGen++;
        break;
```

In `loop()`, right after `run(actions);` add:

```cpp
  tileAnim.onKeys(deck.keysHeld(), helperMissingKeys(actions), now);
```

Replace the final `displayUpdate(...)` call with:

```cpp
  displayUpdate({pct, spkMuted, micMuted, micKnown, micPct, banner, arg, keymapGen, hl.present(now), wifi.on()},
                deck.keyMap(), tileAnim, now);
```

- [ ] **Step 9: Run everything**

Run: `pio test -e native && pio test -e sim && pio run -e saucedeck -e bringup`
Expected: native all PASS; sim all PASS (test_sim_keys has no LCD assertions); both firmware envs `[SUCCESS]`; no new warnings in our sources (inspect build output for `warning:` lines under `src/` or `lib/deck_core/`).

- [ ] **Step 10: Commit**

```bash
git add lib/deck_core/src/ui_layout.h test/native/test_ui/test_main.cpp src/display.h src/display.cpp src/main.cpp \
  test/sim/lib/simdeck/src/Adafruit_GFX.h test/sim/lib/simdeck/src/Adafruit_ST7789.h test/sim/lib/simdeck/src/fakes.cpp \
  test/sim/lib/simdeck/src/sim/lcd.h test/sim/lib/simdeck/src/Fonts/FreeSans9pt7b.h
git commit -m "LCD: 4x3 key legends + status strip, tile press feedback

Replaces the volume screen (vertical bar, speaker/mic icons) with the split layout from
the 2026-10-31 spec. Not bench-verified: legibility and fade feel need the panel.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Sim display scenarios

**Files:**
- Modify: `test/sim/lib/simdeck/src/sim/lcd.h`, `test/sim/lib/simdeck/src/fakes.cpp` (rect recording)
- Modify: `test/sim/lib/simdeck/src/sim_deck.cpp` (preset labels)
- Create: `test/native/test_sim_display/test_main.cpp`

**Interfaces:**
- Consumes: Task 4 firmware layout (tile i body rect = `(x+1, y+1, 78, 54)` with `x = (i%4)*80`, `y = 20 + (i/4)*56`; failed colour `0xFD20`), `SimDeck`, `SIM_TEST`, `board().nvs`, `serializeKeyMap`.
- Produces: `struct SimLcdRect { int16_t x, y, w, h; uint16_t c; };`, `SimLcd::rects`.

- [ ] **Step 1: Write the failing scenarios**

Create `test/native/test_sim_display/test_main.cpp`:

```cpp
// Display scenarios: the real src/ firmware drawing legends and press feedback on the sim LCD.
#include <unity.h>
#include <vector>
#include "keymap.h"
#include "sim_deck.h"
#include "sim_test.h"

void setUp() {}
void tearDown() {}

static constexpr uint32_t kAfterSplash = 1600;  // splash 1500 ms (src/display.cpp)
static constexpr uint32_t kSettle = 30;         // > 25 ms debounce

// Tile body of key position pos (1..12): src/display.cpp drawTile.
static bool isBody(const SimLcdRect &r, uint8_t pos) {
  const int16_t i = pos - 1, x = (i % 4) * 80, y = 20 + (i / 4) * 56;
  return r.x == x + 1 && r.y == y + 1 && r.w == 78 && r.h == 54;
}

static bool insideTile(const SimLcdRect &r, uint8_t pos) {
  const int16_t i = pos - 1, x = (i % 4) * 80, y = 20 + (i / 4) * 56;
  return r.x >= x && r.y >= y && r.x + r.w <= x + 80 && r.y + r.h <= y + 56;
}

static std::vector<SimLcdRect> bodies(uint8_t pos) {
  std::vector<SimLcdRect> out;
  for (const SimLcdRect &r : board().lcd.rects)
    if (isBody(r, pos)) out.push_back(r);
  return out;
}

SIM_TEST(test_boot_shows_default_legends_f13_to_f24) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  char t[8];
  for (unsigned k = 13; k <= 24; k++) {
    snprintf(t, sizeof t, "F%u", k);
    TEST_ASSERT_TRUE_MESSAGE(board().lcd.printed(t), t);
  }
  TEST_ASSERT_TRUE(board().lcd.printed("MIC LIVE"));
}

SIM_TEST(test_preset_labels_become_legends) {
  SimDeck sim;
  TEST_ASSERT_TRUE(sim.loadPresetIntoNvs(SIM_PROJECT_DIR "/presets/mac-daily.json"));
  sim.boot();
  sim.run(kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Chrome"));
  TEST_ASSERT_TRUE(board().lcd.printed("YouTube"));
  TEST_ASSERT_TRUE(board().lcd.printed("Weather"));
  TEST_ASSERT_FALSE(board().lcd.printed("F14"));
}

SIM_TEST(test_key_press_repaints_only_its_tile) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  board().lcd.rects.clear();
  sim.press(6);
  sim.run(kSettle + 40);
  TEST_ASSERT_FALSE(board().lcd.rects.empty());
  for (const SimLcdRect &r : board().lcd.rects) TEST_ASSERT_TRUE(insideTile(r, 6));
}

SIM_TEST(test_release_fades_back_to_idle_in_four_repaints) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  const uint16_t idle = bodies(6).back().c;
  sim.press(6);
  sim.run(kSettle + 40);
  TEST_ASSERT_NOT_EQUAL(idle, bodies(6).back().c);
  board().lcd.rects.clear();
  sim.release(6);
  sim.run(kSettle + 300);
  std::vector<SimLcdRect> fade = bodies(6);
  TEST_ASSERT_EQUAL(4, fade.size());  // levels 3, 2, 1, 0
  TEST_ASSERT_EQUAL_HEX16(idle, fade.back().c);
}

SIM_TEST(test_app_key_without_helper_flashes_orange) {
  KeyMap m;
  TEST_ASSERT_EQUAL(KeyMapError::None, m.set(0, KeyType::App, "Calculator"));
  uint8_t blob[kKeyMapBlobMax];
  size_t n = serializeKeyMap(m, blob, sizeof blob);
  board().nvs.ns["saucedeck"]["keymap"].assign(blob, blob + n);  // src/keymap_store.cpp names
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  sim.press(1);
  sim.run(kSettle + 40);
  TEST_ASSERT_EQUAL_HEX16(0xFD20, bodies(1).back().c);  // kOrange, src/display.cpp
  TEST_ASSERT_TRUE(board().lcd.printed("helper not running"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_shows_default_legends_f13_to_f24);
  RUN_TEST(test_preset_labels_become_legends);
  RUN_TEST(test_key_press_repaints_only_its_tile);
  RUN_TEST(test_release_fades_back_to_idle_in_four_repaints);
  RUN_TEST(test_app_key_without_helper_flashes_orange);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, confirm failure**

Run: `pio test -e sim -f test_sim_display`
Expected: compile error `'SimLcdRect' was not declared` / `'struct SimLcd' has no member named 'rects'`.

- [ ] **Step 3: Rect recording**

In `test/sim/lib/simdeck/src/sim/lcd.h`, update the top comment to `// ST7789 via the Adafruit API: records printed text and filled rects (no framebuffer).`, add before `struct SimLcd`:

```cpp
struct SimLcdRect {
  int16_t x, y, w, h;
  uint16_t c;
};
```

and inside `SimLcd` after `texts`:

```cpp
  std::vector<SimLcdRect> rects;  // every fillRect, in order
```

In `fakes.cpp`, replace the `fillRect` fake:

```cpp
void Adafruit_ST7789::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  board().lcd.fills++;
  board().lcd.rects.push_back({x, y, w, h, c});
}
```

- [ ] **Step 4: Run, check which scenarios still fail**

Run: `pio test -e sim -f test_sim_display`
Expected: `test_preset_labels_become_legends` FAILS (preset loader ignores labels, so "Chrome" is not printed); the other four PASS.

- [ ] **Step 5: Preset loader reads labels**

In `test/sim/lib/simdeck/src/sim_deck.cpp` `SimDeck::loadPresetIntoNvs`, replace the loop body:

```cpp
  for (uint8_t i = 0; i < kKeyCount; i++) {
    std::string label, type, value;  // presets list label, type, value per key
    if (!nextField(s, pos, "label", label) || !nextField(s, pos, "type", type) ||
        !nextField(s, pos, "value", value))
      return false;
    if (m.setFromStrings(i, type.c_str(), value.c_str()) != KeyMapError::None) return false;
    if (m.setLabel(i, label.c_str()) != KeyMapError::None) return false;
  }
```

- [ ] **Step 6: Run all sim + native**

Run: `pio test -e sim && pio test -e native`
Expected: all PASS (test_sim_keys unchanged; its mac-daily scenarios still pass with labels loaded).

- [ ] **Step 7: Commit**

```bash
git add test/sim/lib/simdeck/src/sim/lcd.h test/sim/lib/simdeck/src/fakes.cpp test/sim/lib/simdeck/src/sim_deck.cpp \
  test/native/test_sim_display/test_main.cpp
git commit -m "Sim: legend and press-feedback scenarios on the real display code

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Portal label fields + preset loader labels

**Files:**
- Modify: `src/wifi_portal.cpp`

**Interfaces:**
- Consumes: `KeyMap::setLabel`, `kKeyLabelMax`, `KeyEntry::label` (Task 1).
- Produces: portal form fields `l1`…`l12` ("Key N label"); preset JSON `label` fills them; staged map carries labels.

No native test: `wifi_portal.cpp` is radio glue (CI-checked as the only Wi-Fi file) and the WiFiManager fake has no form-post path. Label validation itself is covered by Task 1. Verified by build + bench step in Task 7.

- [ ] **Step 1: Buffers and params for 3 fields per key**

In `src/wifi_portal.cpp` replace the static field declarations:

```cpp
static char typeBuf[kKeyCount][10];
static char valueBuf[kKeyCount][kKeyValueMax + 1];
static char labelBuf[kKeyCount][kKeyLabelMax + 1];
static constexpr size_t kFields = 3;  // per key: type, value, label
static char ids[kKeyCount * kFields][4];
static char labels[kKeyCount * kFields][24];
static WiFiManagerParameter *params[kKeyCount * kFields];
```

- [ ] **Step 2: Preset loader fills labels**

In `kPresetHtml`, after the value check line `"throw Error('key '+(i+1)+': value 1-56 chars');});"` the validation `forEach` closes. Replace the validation and fill section so it reads:

```cpp
    "k.forEach(function(x,i){if(['key','app','url','shortcut'].indexOf(x.type)<0)"
    "throw Error('key '+(i+1)+': bad type');"
    "if(typeof x.value!='string'||!x.value.length||x.value.length>56)"
    "throw Error('key '+(i+1)+': value 1-56 chars');"
    "if(x.label!==undefined&&(typeof x.label!='string'||x.label.length>16))"
    "throw Error('key '+(i+1)+': label 0-16 chars');});"
    "k.forEach(function(x,i){document.getElementById('t'+(i+1)).value=x.type;"
    "document.getElementById('v'+(i+1)).value=x.value;"
    "document.getElementById('l'+(i+1)).value=x.label||'';});"
```

and update the comment above `kPresetHtml` from "fills the 24 inputs" to "fills the 36 inputs".

- [ ] **Step 3: buildParams with labels**

Replace `buildParams`:

```cpp
static void buildParams(const KeyMap &m) {
  if (!paramsAdded) wm.addParameter(&presetParam);
  for (uint8_t i = 0; i < kKeyCount; i++) {
    WiFiManagerParameter **p = &params[kFields * i];
    snprintf(typeBuf[i], sizeof typeBuf[i], "%s", keyTypeName(m.entry(i).type));
    snprintf(valueBuf[i], sizeof valueBuf[i], "%s", m.entry(i).value);
    snprintf(labelBuf[i], sizeof labelBuf[i], "%s", m.entry(i).label);
    if (paramsAdded) {
      p[0]->setValue(typeBuf[i], 9);
      p[1]->setValue(valueBuf[i], kKeyValueMax);
      p[2]->setValue(labelBuf[i], kKeyLabelMax);
      continue;
    }
    const unsigned n = i + 1;
    snprintf(ids[kFields * i], sizeof ids[0], "t%u", n);
    snprintf(ids[kFields * i + 1], sizeof ids[0], "v%u", n);
    snprintf(ids[kFields * i + 2], sizeof ids[0], "l%u", n);
    snprintf(labels[kFields * i], sizeof labels[0], "Key %u type", n);
    snprintf(labels[kFields * i + 1], sizeof labels[0], "Key %u value", n);
    snprintf(labels[kFields * i + 2], sizeof labels[0], "Key %u label", n);
    p[0] = new WiFiManagerParameter(ids[kFields * i], labels[kFields * i], typeBuf[i], 9);
    p[1] = new WiFiManagerParameter(ids[kFields * i + 1], labels[kFields * i + 1], valueBuf[i], kKeyValueMax);
    p[2] = new WiFiManagerParameter(ids[kFields * i + 2], labels[kFields * i + 2], labelBuf[i], kKeyLabelMax);
    for (size_t f = 0; f < kFields; f++) wm.addParameter(p[f]);
  }
  paramsAdded = true;
}
```

- [ ] **Step 4: Staged map takes labels**

Replace the loop in `portalTakeStaged`:

```cpp
  for (uint8_t i = 0; i < kKeyCount; i++) {
    WiFiManagerParameter **p = &params[kFields * i];
    if (m.setFromStrings(i, p[0]->getValue(), p[1]->getValue()) != KeyMapError::None ||
        m.setLabel(i, p[2]->getValue()) != KeyMapError::None) {
      badKey = i + 1;
      return true;
    }
  }
```

- [ ] **Step 5: Build + tests + radio scope check**

Run: `pio run -e saucedeck && pio test -e native && pio test -e sim && tools/check_radio_scope.sh`
Expected: `[SUCCESS]`, all tests PASS, radio check passes (no new Wi-Fi symbols outside `wifi_portal.cpp`). If `check_radio_scope.sh` needs arguments, run it as CI does: `grep -n check_radio_scope .github/workflows/*.yml`.

- [ ] **Step 6: Commit**

```bash
git add src/wifi_portal.cpp
git commit -m "Portal: per-key label field; preset loader fills labels

Not bench-verified: needs a portal save + knob 1 confirm on the deck.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Verification, size record, bench steps

**Files:**
- Modify: `docs/kb/toolchain.md`
- Create: `docs/bringup/2026-MM-DD.log` (date of the bench session)

- [ ] **Step 1: Full Done check**

Run: `pio test -e native && pio test -e sim && pio run -e saucedeck -e bringup 2>&1 | tee /tmp/build.log; grep -n "warning:" /tmp/build.log | grep -E "src/|lib/deck_core/" || echo "no warnings in our sources"`
Expected: all PASS, both envs `[SUCCESS]`, "no warnings in our sources".

- [ ] **Step 2: Record firmware size**

From the `saucedeck` build output take the `Flash: [...] xx.x% (used N bytes from 3145728 bytes)` line. Append to `docs/kb/toolchain.md`:

```markdown
- **Firmware with legend layout + FreeSans9pt7b (Plan 1, 2026-10-31) = N B = xx.x % of the 3 MB huge_app slot.** `[verified: build output YYYY-MM-DD]`
```

(Replace `N`, `xx.x`, and the date with the actual numbers from the build output.)

- [ ] **Step 3: Coverage report**

Run: `tools/coverage.sh`
Expected: report prints; `legend.h`, `tile_anim.h`, `ui_layout.h` at or near 100 % line coverage. Note the numbers in the commit message.

- [ ] **Step 4: Bench (operator)**

Flash: `pio run -t upload` (if "Couldn't find a board": `ls /dev/cu.usbmodem*`, then pass `--upload-port`). Then ask the operator, one at a time, and log answers:
1. After the splash, do 12 tiles show F13…F24 (or the stored preset names) in the right physical positions (top row = keys 1–4)?
2. Are labels readable at arm's length? Any line clipped at the tile edge?
3. Press and hold key 6: tile fills? Release: does it fade back smoothly (not one jump)?
4. Turn knob 1 fast: bar follows, no missed keys while a tile fades (press a key mid-turn)?
5. Mute (knob 1 press): bar grey, "MUTED" red?
6. Portal: hold both knobs 5 s, open the page, set key 2 label "Test", Save, press knob 1 → tile 2 shows "Test"?

Save the transcript to `docs/bringup/<date>.log`. Any confirmed fact (e.g. a real tile text width limit) → `docs/kb/peripherals.md` with `[verified: bench <date>]`.

- [ ] **Step 5: Commit**

```bash
git add docs/kb/toolchain.md docs/bringup/
git commit -m "Bench + size: legend layout (Plan 1 of LCD legends/status)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
