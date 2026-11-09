# LCD Status Cards + Clock (Plan 2/3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Mac helper can push the time and short text cards over the existing USB CDC link; the deck shows the clock and slot 0 in the top bar and rotates slots 1–7 on strip line 2 (banners win).

**Architecture:** Two new display-only host→deck messages (`MSG_TIME` 0x04, `MSG_CARD` 0x05) decoded in `helper_proto.h`; `DeckClock` and `StatusBoard` are pure deck_core models; `helper_link.cpp` routes decoded messages; `display.cpp` draws from the models; `tools/helper_sim.py` sends the new frames for bench use. Builds on Plan 1 (legend layout, `ScreenModel` v2, `fitLine`-capable `legend.h`).

**Tech Stack:** PlatformIO, pioarduino 55.03.35, Adafruit GFX 1.12.6, Unity (native + sim), C++17, Python 3 (pyserial) for the bench tool.

**Spec:** `docs/superpowers/specs/2026-10-31-lcd-legends-status-design.md` §4.4–4.7, §5, §8, §10, §11. Requires Plan 1 (`docs/superpowers/plans/2026-10-31-lcd-legends-plan1-deck.md`) Tasks 1–6 merged first.

## Global Constraints

- `lib/deck_core/` = no `Arduino.h`, pure C++17; builds under `-std=gnu++17 -Wall -Wextra -Werror`.
- TDD: failing test first, see it fail for the right reason, then minimal code.
- Use `~/.platformio/penv/bin/pio` (the `pio` on PATH ignores `-f`).
- `MSG_TIME` 0x04 payload (len 6): `epoch` u32 LE (UTC seconds), `tzOffsetMin` i16 LE, valid −720…+840.
- `MSG_CARD` 0x05 payload (len 5 + tlen): `slot` 0–7, `icon` 0–7, `ttlS` u16 LE (0 = until replaced), `tlen` 0–48, text. Text bytes 0x20–0x7E only. `tlen` = 0 clears the slot.
- Any field out of range, any non-printable text byte, or `len != 5 + tlen` → frame rejected (counted in `rejected`).
- Icons: 0 none, 1 music, 2 calendar, 3 cpu, 4 weather, 5 mail, 6 chat, 7 battery.
- Slot 0 = top bar. Slots 1–7 rotate on strip line 2 every 5 s; expired slots skipped; `n/m` index shown. Any banner (`BANNER_*` != `BANNER_NONE`) takes line 2 over.
- Helper lost (`HostLink::present` false) → all cards cleared; clock keeps running. No `TIME` since boot → `--:--`.
- Host→deck messages only change what the LCD shows. No command path.
- Done = `pio test -e native` green, `pio test -e sim` green, `pio run -e saucedeck -e bringup` SUCCESS, zero new warnings. Show the output.
- Commit messages end with a `Co-Authored-By:` trailer naming the model that wrote the commit (`<noreply@anthropic.com>`).

## File Structure

| File | Action | Responsibility |
| --- | --- | --- |
| `lib/deck_core/src/helper_proto.h` | Modify | `MSG_TIME`, `MSG_CARD`, `TimeMsg`, `CardMsg`, `decodeTime`, `decodeCard` |
| `lib/deck_core/src/deck_clock.h` | Create | `DeckClock` (local minute of day from TIME + millis), `formatHhmm` |
| `lib/deck_core/src/status_board.h` | Create | `StatusBoard` (8 slots, ttl, rotation, gen counter) |
| `lib/deck_core/src/legend.h` | Modify | `fitLine` (single-line cut with "..."); `wrapLabel` reuses it |
| `lib/deck_core/src/ui_layout.h` | Modify | `ScreenState.clockMinute`, `ScreenState.statusGen` + dirty rules |
| `src/helper_link.h`, `src/helper_link.cpp` | Modify | Route TIME/CARD; expose clock and board |
| `src/display.h`, `src/display.cpp` | Modify | Clock + slot 0 in top bar; card + icon + `n/m` on line 2 |
| `src/main.cpp` | Modify | Clear/tick the board; pass clock minute, gen, board to display |
| `test/native/test_proto/test_main.cpp` | Modify | TIME/CARD decode + fuzz |
| `test/native/test_status/test_main.cpp` | Create | `DeckClock`, `formatHhmm`, `StatusBoard` |
| `test/native/test_legend/test_main.cpp` | Modify | `fitLine` |
| `test/native/test_ui/test_main.cpp` | Modify | New dirty rules |
| `test/native/test_sim_display/test_main.cpp` | Modify | Clock / card / rotation / helper-loss / banner scenarios |
| `tools/helper_sim.py` | Modify | `--clock`, `--card=SLOT:ICON:TEXT` |

---

### Task 1: TIME and CARD messages (decode)

**Files:**
- Modify: `lib/deck_core/src/helper_proto.h`
- Test: `test/native/test_proto/test_main.cpp`

**Interfaces:**
- Consumes: `kProtoVersion`, `kReportSize`, existing `decodeHost` (unchanged: it must keep rejecting types 0x04/0x05).
- Produces:
  - `MSG_TIME = 0x04`, `MSG_CARD = 0x05` (added to the existing message enum)
  - `constexpr size_t kCardTextMax = 48; constexpr uint8_t kCardSlots = 8; constexpr uint8_t kCardIcons = 8;`
  - `struct TimeMsg { uint32_t epoch; int16_t tzOffsetMin; };`
  - `struct CardMsg { uint8_t slot; uint8_t icon; uint16_t ttlS; char text[kCardTextMax + 1]; };`
  - `bool decodeTime(const uint8_t *buf, size_t n, TimeMsg &out);`
  - `bool decodeCard(const uint8_t *buf, size_t n, CardMsg &out);`

- [ ] **Step 1: Write the failing tests**

Add to `test/native/test_proto/test_main.cpp` above `int main` (it already includes `<string.h>` and `helper_proto.h`; add `#include <stdlib.h>` at the top if `rand` is not yet available):

```cpp
// --- TIME / CARD (status display, host -> deck) ---

static size_t timeMsg(uint8_t *b, uint32_t epoch, int16_t tz, uint8_t len = 6) {
  memset(b, 0, kReportSize);
  b[0] = kProtoVersion; b[1] = MSG_TIME; b[2] = len;
  for (int k = 0; k < 4; k++) b[3 + k] = static_cast<uint8_t>(epoch >> (8 * k));
  b[7] = static_cast<uint8_t>(tz); b[8] = static_cast<uint8_t>(static_cast<uint16_t>(tz) >> 8);
  return kReportSize;
}

static size_t cardMsg(uint8_t *b, uint8_t slot, uint8_t icon, uint16_t ttl, const char *text) {
  size_t tl = strlen(text);
  memset(b, 0, kReportSize);
  b[0] = kProtoVersion; b[1] = MSG_CARD; b[2] = static_cast<uint8_t>(5 + tl);
  b[3] = slot; b[4] = icon; b[5] = ttl & 0xFF; b[6] = ttl >> 8; b[7] = static_cast<uint8_t>(tl);
  memcpy(b + 8, text, tl);
  return kReportSize;
}

void test_decode_time() {
  uint8_t b[kReportSize];
  TimeMsg t;
  TEST_ASSERT_TRUE(decodeTime(b, timeMsg(b, 0x6A2B3C4Du, -60), t));
  TEST_ASSERT_EQUAL_HEX32(0x6A2B3C4Du, t.epoch);
  TEST_ASSERT_EQUAL_INT16(-60, t.tzOffsetMin);
  TEST_ASSERT_TRUE(decodeTime(b, timeMsg(b, 1, -720), t));
  TEST_ASSERT_TRUE(decodeTime(b, timeMsg(b, 1, 840), t));
}

void test_decode_time_rejects() {
  uint8_t b[kReportSize];
  TimeMsg t;
  TEST_ASSERT_FALSE(decodeTime(b, timeMsg(b, 1, 841), t));
  TEST_ASSERT_FALSE(decodeTime(b, timeMsg(b, 1, -721), t));
  TEST_ASSERT_FALSE(decodeTime(b, timeMsg(b, 1, 0, 5), t));  // wrong len
  timeMsg(b, 1, 0); b[0] = 2;
  TEST_ASSERT_FALSE(decodeTime(b, kReportSize, t));          // version
  timeMsg(b, 1, 0); b[1] = MSG_CARD;
  TEST_ASSERT_FALSE(decodeTime(b, kReportSize, t));          // other type
  TEST_ASSERT_FALSE(decodeTime(b, 8, t));                    // truncated buffer
}

void test_decode_card() {
  uint8_t b[kReportSize];
  CardMsg c;
  TEST_ASSERT_TRUE(decodeCard(b, cardMsg(b, 1, 1, 300, "Daft Punk - Around the World"), c));
  TEST_ASSERT_EQUAL_UINT8(1, c.slot);
  TEST_ASSERT_EQUAL_UINT8(1, c.icon);
  TEST_ASSERT_EQUAL_UINT16(300, c.ttlS);
  TEST_ASSERT_EQUAL_STRING("Daft Punk - Around the World", c.text);
  TEST_ASSERT_TRUE(decodeCard(b, cardMsg(b, 7, 7, 0, ""), c));  // empty = clear slot
  TEST_ASSERT_EQUAL_STRING("", c.text);
  char max[kCardTextMax + 1];
  memset(max, 'x', kCardTextMax);
  max[kCardTextMax] = 0;
  TEST_ASSERT_TRUE(decodeCard(b, cardMsg(b, 0, 0, 0, max), c));
  TEST_ASSERT_EQUAL_size_t(kCardTextMax, strlen(c.text));
}

void test_decode_card_rejects() {
  uint8_t b[kReportSize];
  CardMsg c;
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 8, 1, 0, "x"), c));      // slot
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 1, 8, 0, "x"), c));      // icon
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 1, 1, 0, "a\x7F"), c));  // DEL
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 1, 1, 0, "a\x1F"), c));  // control
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 1, 1, 0, "Caf\xC3\xA9"), c));  // UTF-8
  cardMsg(b, 1, 1, 0, "abc"); b[2] = 9;                               // len != 5 + tlen
  TEST_ASSERT_FALSE(decodeCard(b, kReportSize, c));
  char big[kCardTextMax + 2];
  memset(big, 'x', kCardTextMax + 1);
  big[kCardTextMax + 1] = 0;
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 1, 1, 0, big), c));      // tlen 49
  cardMsg(b, 1, 1, 0, "x"); b[1] = MSG_TIME;
  TEST_ASSERT_FALSE(decodeCard(b, kReportSize, c));
  HostMsg h;
  TEST_ASSERT_FALSE(decodeHost(b, kReportSize, h));                   // decodeHost ignores 0x04
  cardMsg(b, 1, 1, 0, "x");
  TEST_ASSERT_FALSE(decodeHost(b, kReportSize, h));                   // ... and 0x05
}

void test_decode_status_fuzz_never_accepts_out_of_range() {
  srand(12345);
  uint8_t b[kReportSize];
  for (int it = 0; it < 20000; it++) {
    for (size_t i = 0; i < kReportSize; i++) b[i] = static_cast<uint8_t>(rand());
    b[0] = kProtoVersion;
    b[1] = (it & 1) ? MSG_CARD : MSG_TIME;
    if (it % 3 == 0) b[2] = static_cast<uint8_t>(5 + (b[7] % 50));  // make len plausible
    CardMsg c;
    if (decodeCard(b, kReportSize, c)) {
      TEST_ASSERT_TRUE(c.slot < kCardSlots && c.icon < kCardIcons);
      size_t n = strlen(c.text);
      TEST_ASSERT_TRUE(n <= kCardTextMax);
      for (size_t k = 0; k < n; k++) TEST_ASSERT_TRUE(c.text[k] >= 0x20 && c.text[k] <= 0x7E);
    }
    TimeMsg t;
    if (decodeTime(b, kReportSize, t)) TEST_ASSERT_TRUE(t.tzOffsetMin >= -720 && t.tzOffsetMin <= 840);
  }
}
```

Register at the end of `main`'s list:

```cpp
  RUN_TEST(test_decode_time);
  RUN_TEST(test_decode_time_rejects);
  RUN_TEST(test_decode_card);
  RUN_TEST(test_decode_card_rejects);
  RUN_TEST(test_decode_status_fuzz_never_accepts_out_of_range);
```

- [ ] **Step 2: Run, confirm failure**

Run: `~/.platformio/penv/bin/pio test -e native -f native/test_proto`
Expected: compile errors `'MSG_TIME' was not declared`, `'TimeMsg' was not declared`, `'decodeCard' was not declared`.

- [ ] **Step 3: Implement**

In `lib/deck_core/src/helper_proto.h`:

Update the header comment's first line to: `// Deck <-> Mac helper messages (63-byte messages, framed on USB CDC by frame.h).`

Add to the message enum after `MSG_MIC = 0x03,`:

```cpp
  MSG_TIME = 0x04,  // status display: wall clock
  MSG_CARD = 0x05,  // status display: one text card
```

Add after the `decodeHost` function:

```cpp
// Status display (spec 2026-10-31 §4.4). Display-only like VOLUME/MIC: nothing here can
// trigger a key or an action.
constexpr size_t kCardTextMax = 48;
constexpr uint8_t kCardSlots = 8;  // 0 = top bar, 1..7 rotate on the strip
constexpr uint8_t kCardIcons = 8;  // 0 none, 1 music, 2 calendar, 3 cpu, 4 weather, 5 mail, 6 chat, 7 battery

struct TimeMsg {
  uint32_t epoch;       // UTC seconds
  int16_t tzOffsetMin;  // local = UTC + offset, -720..+840
};

struct CardMsg {
  uint8_t slot;
  uint8_t icon;
  uint16_t ttlS;  // 0 = until replaced
  char text[kCardTextMax + 1];  // printable ASCII, empty = clear the slot
};

inline bool decodeTime(const uint8_t *buf, size_t n, TimeMsg &out) {
  if (n < 9 || buf[0] != kProtoVersion || buf[1] != MSG_TIME || buf[2] != 6) return false;
  const uint8_t *p = buf + 3;
  uint32_t e = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
  int16_t tz = static_cast<int16_t>(static_cast<uint16_t>(p[4] | (p[5] << 8)));
  if (tz < -720 || tz > 840) return false;
  out = {e, tz};
  return true;
}

inline bool decodeCard(const uint8_t *buf, size_t n, CardMsg &out) {
  if (n < 8 || buf[0] != kProtoVersion || buf[1] != MSG_CARD) return false;
  uint8_t len = buf[2];
  const uint8_t *p = buf + 3;
  if (len < 5 || 3u + len > n) return false;
  uint8_t tlen = p[4];
  if (p[0] >= kCardSlots || p[1] >= kCardIcons || tlen > kCardTextMax || len != 5 + tlen) return false;
  for (uint8_t i = 0; i < tlen; i++)
    if (p[5 + i] < 0x20 || p[5 + i] > 0x7E) return false;
  out.slot = p[0];
  out.icon = p[1];
  out.ttlS = static_cast<uint16_t>(p[2] | (p[3] << 8));
  memcpy(out.text, p + 5, tlen);
  out.text[tlen] = 0;
  return true;
}
```

- [ ] **Step 4: Run, confirm pass**

Run: `~/.platformio/penv/bin/pio test -e native -f native/test_proto`
Expected: all test_proto tests PASS (19 old + 5 new).

- [ ] **Step 5: Commit**

```bash
git add lib/deck_core/src/helper_proto.h test/native/test_proto/test_main.cpp
git commit -m "HelperProto: TIME and CARD status messages (display-only, fuzz-tested)

Co-Authored-By: <model> <noreply@anthropic.com>"
```

---

### Task 2: DeckClock + StatusBoard models

**Files:**
- Create: `lib/deck_core/src/deck_clock.h`
- Create: `lib/deck_core/src/status_board.h`
- Create: `test/native/test_status/test_main.cpp`

**Interfaces:**
- Consumes: Task 1 `CardMsg`, `kCardTextMax`, `kCardSlots`.
- Produces:
  - `class DeckClock { void onTime(uint32_t epoch, int16_t tzOffsetMin, uint32_t nowMs); bool known() const; int16_t minuteOfDay(uint32_t nowMs) const; };` (−1 before the first TIME)
  - `void formatHhmm(int16_t minuteOfDay, char out[6]);` (`"14:32"`, `"--:--"` when out of 0–1439)
  - `struct StatusCard { uint8_t icon; char text[kCardTextMax + 1]; };`
  - `class StatusBoard { static constexpr uint32_t kRotateMs = 5000; void onCard(const CardMsg &c, uint32_t nowMs); void clear(); void tick(uint32_t nowMs); const StatusCard *top() const; const StatusCard *current() const; uint8_t count() const; uint8_t index() const; uint8_t gen() const; };`

- [ ] **Step 1: Write the failing tests**

Create `test/native/test_status/test_main.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "deck_clock.h"
#include "status_board.h"

void setUp() {}
void tearDown() {}

// --- DeckClock ---

static constexpr uint32_t kDay = 20000u * 86400u;  // some UTC midnight

void test_clock_unknown_until_time() {
  DeckClock c;
  TEST_ASSERT_FALSE(c.known());
  TEST_ASSERT_EQUAL_INT16(-1, c.minuteOfDay(12345));
}

void test_clock_utc_and_offsets() {
  DeckClock c;
  c.onTime(kDay + 12 * 3600 + 34 * 60, 0, 1000);
  TEST_ASSERT_TRUE(c.known());
  TEST_ASSERT_EQUAL_INT16(12 * 60 + 34, c.minuteOfDay(1000));
  c.onTime(kDay + 12 * 3600 + 34 * 60, 60, 1000);
  TEST_ASSERT_EQUAL_INT16(13 * 60 + 34, c.minuteOfDay(1000));
  c.onTime(kDay + 12 * 3600 + 34 * 60, -720, 1000);
  TEST_ASSERT_EQUAL_INT16(34, c.minuteOfDay(1000));
}

void test_clock_runs_on_millis() {
  DeckClock c;
  c.onTime(kDay + 12 * 3600 + 34 * 60, 0, 1000);
  TEST_ASSERT_EQUAL_INT16(12 * 60 + 34, c.minuteOfDay(1000 + 59999 - 0));  // 12:34:59
  TEST_ASSERT_EQUAL_INT16(13 * 60, c.minuteOfDay(1000 + 26u * 60u * 1000u));
}

void test_clock_midnight_wrap_and_negative_local() {
  DeckClock c;
  c.onTime(kDay + 23 * 3600 + 59 * 60, 0, 0);
  TEST_ASSERT_EQUAL_INT16(0, c.minuteOfDay(60000));
  c.onTime(0, -60, 0);  // 1970-01-01 00:00 UTC = 23:00 the day before
  TEST_ASSERT_EQUAL_INT16(23 * 60, c.minuteOfDay(0));
}

void test_clock_millis_wrap() {
  DeckClock c;
  c.onTime(kDay, 0, 0xFFFFFF00u);
  TEST_ASSERT_EQUAL_INT16(0, c.minuteOfDay(0x00001000u));  // ~4.3 s later
}

void test_format_hhmm() {
  char b[6];
  formatHhmm(12 * 60 + 34, b);
  TEST_ASSERT_EQUAL_STRING("12:34", b);
  formatHhmm(5, b);
  TEST_ASSERT_EQUAL_STRING("00:05", b);
  formatHhmm(1439, b);
  TEST_ASSERT_EQUAL_STRING("23:59", b);
  formatHhmm(-1, b);
  TEST_ASSERT_EQUAL_STRING("--:--", b);
  formatHhmm(1440, b);
  TEST_ASSERT_EQUAL_STRING("--:--", b);
}

// --- StatusBoard ---

static CardMsg card(uint8_t slot, const char *t, uint16_t ttl = 0, uint8_t icon = 1) {
  CardMsg c{};
  c.slot = slot;
  c.icon = icon;
  c.ttlS = ttl;
  strncpy(c.text, t, kCardTextMax);
  return c;
}

void test_board_empty() {
  StatusBoard b;
  b.tick(0);
  TEST_ASSERT_NULL(b.top());
  TEST_ASSERT_NULL(b.current());
  TEST_ASSERT_EQUAL_UINT8(0, b.count());
  TEST_ASSERT_EQUAL_UINT8(0, b.index());
}

void test_board_slot0_is_top_not_rotation() {
  StatusBoard b;
  b.onCard(card(0, "Standup in 12m", 0, 2), 0);
  b.tick(0);
  TEST_ASSERT_NOT_NULL(b.top());
  TEST_ASSERT_EQUAL_STRING("Standup in 12m", b.top()->text);
  TEST_ASSERT_EQUAL_UINT8(2, b.top()->icon);
  TEST_ASSERT_NULL(b.current());
  TEST_ASSERT_EQUAL_UINT8(0, b.count());
}

void test_board_single_card_shows() {
  StatusBoard b;
  b.onCard(card(2, "CPU 12%"), 0);
  b.tick(0);
  TEST_ASSERT_EQUAL_STRING("CPU 12%", b.current()->text);
  TEST_ASSERT_EQUAL_UINT8(1, b.index());
  TEST_ASSERT_EQUAL_UINT8(1, b.count());
}

void test_board_rotates_every_5s_and_wraps() {
  StatusBoard b;
  b.onCard(card(1, "A"), 0);
  b.onCard(card(3, "B"), 0);
  b.tick(0);
  TEST_ASSERT_EQUAL_STRING("A", b.current()->text);
  TEST_ASSERT_EQUAL_UINT8(1, b.index());
  TEST_ASSERT_EQUAL_UINT8(2, b.count());
  b.tick(4999);
  TEST_ASSERT_EQUAL_STRING("A", b.current()->text);
  b.tick(5000);
  TEST_ASSERT_EQUAL_STRING("B", b.current()->text);
  TEST_ASSERT_EQUAL_UINT8(2, b.index());
  b.tick(10000);
  TEST_ASSERT_EQUAL_STRING("A", b.current()->text);
}

void test_board_ttl_expires() {
  StatusBoard b;
  b.onCard(card(1, "Song", 2), 0);
  b.tick(1999);
  TEST_ASSERT_NOT_NULL(b.current());
  b.tick(2000);
  TEST_ASSERT_NULL(b.current());
  TEST_ASSERT_EQUAL_UINT8(0, b.count());
}

void test_board_resend_refreshes_ttl() {
  StatusBoard b;
  b.onCard(card(1, "Song", 2), 0);
  b.onCard(card(1, "Song", 2), 1500);
  b.tick(3000);
  TEST_ASSERT_NOT_NULL(b.current());
  b.tick(3500);
  TEST_ASSERT_NULL(b.current());
}

void test_board_empty_text_clears_slot() {
  StatusBoard b;
  b.onCard(card(4, "Mail 3"), 0);
  b.tick(0);
  b.onCard(card(4, ""), 100);
  b.tick(100);
  TEST_ASSERT_NULL(b.current());
}

void test_board_current_cleared_moves_on_without_waiting() {
  StatusBoard b;
  b.onCard(card(1, "A"), 0);
  b.onCard(card(2, "B"), 0);
  b.tick(0);
  b.onCard(card(1, ""), 100);
  b.tick(100);
  TEST_ASSERT_EQUAL_STRING("B", b.current()->text);
  TEST_ASSERT_EQUAL_UINT8(1, b.index());
  TEST_ASSERT_EQUAL_UINT8(1, b.count());
}

void test_board_gen_changes_only_when_shown_content_changes() {
  StatusBoard b;
  uint8_t g0 = b.gen();
  b.onCard(card(1, "A"), 0);
  b.tick(0);
  uint8_t g1 = b.gen();
  TEST_ASSERT_NOT_EQUAL(g0, g1);
  b.onCard(card(1, "A"), 1000);  // same content resent
  b.tick(1000);
  TEST_ASSERT_EQUAL_UINT8(g1, b.gen());
  b.onCard(card(1, "A2"), 2000);
  TEST_ASSERT_NOT_EQUAL(g1, b.gen());
}

void test_board_clear_is_idempotent() {
  StatusBoard b;
  b.onCard(card(0, "T"), 0);
  b.onCard(card(1, "A"), 0);
  b.tick(0);
  uint8_t g = b.gen();
  b.clear();
  TEST_ASSERT_NULL(b.top());
  TEST_ASSERT_NULL(b.current());
  TEST_ASSERT_NOT_EQUAL(g, b.gen());
  g = b.gen();
  b.clear();
  b.tick(10);
  TEST_ASSERT_EQUAL_UINT8(g, b.gen());
}

void test_board_ignores_bad_slot() {
  StatusBoard b;
  uint8_t g = b.gen();
  b.onCard(card(kCardSlots, "X"), 0);
  b.tick(0);
  TEST_ASSERT_EQUAL_UINT8(g, b.gen());
  TEST_ASSERT_NULL(b.current());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_clock_unknown_until_time);
  RUN_TEST(test_clock_utc_and_offsets);
  RUN_TEST(test_clock_runs_on_millis);
  RUN_TEST(test_clock_midnight_wrap_and_negative_local);
  RUN_TEST(test_clock_millis_wrap);
  RUN_TEST(test_format_hhmm);
  RUN_TEST(test_board_empty);
  RUN_TEST(test_board_slot0_is_top_not_rotation);
  RUN_TEST(test_board_single_card_shows);
  RUN_TEST(test_board_rotates_every_5s_and_wraps);
  RUN_TEST(test_board_ttl_expires);
  RUN_TEST(test_board_resend_refreshes_ttl);
  RUN_TEST(test_board_empty_text_clears_slot);
  RUN_TEST(test_board_current_cleared_moves_on_without_waiting);
  RUN_TEST(test_board_gen_changes_only_when_shown_content_changes);
  RUN_TEST(test_board_clear_is_idempotent);
  RUN_TEST(test_board_ignores_bad_slot);
  return UNITY_END();
}
```

- [ ] **Step 2: Run, confirm failure**

Run: `~/.platformio/penv/bin/pio test -e native -f native/test_status`
Expected: compile error `deck_clock.h: No such file or directory`.

- [ ] **Step 3: Implement DeckClock**

Create `lib/deck_core/src/deck_clock.h`:

```cpp
#pragma once
#include <stdint.h>

// Wall clock for the top bar, hardware-free. The helper sends TIME (UTC + offset) every
// 60 s; between messages the deck counts on millis(). Before the first TIME: unknown.
class DeckClock {
 public:
  void onTime(uint32_t epoch, int16_t tzOffsetMin, uint32_t nowMs) {
    epoch_ = epoch;
    tz_ = tzOffsetMin;
    atMs_ = nowMs;
    known_ = true;
  }

  bool known() const { return known_; }

  // Local minute of the day 0..1439, or -1 before the first TIME.
  int16_t minuteOfDay(uint32_t nowMs) const {
    if (!known_) return -1;
    int64_t local = static_cast<int64_t>(epoch_) + (nowMs - atMs_) / 1000 + static_cast<int64_t>(tz_) * 60;
    int64_t m = (local / 60) % 1440;
    if (local < 0 && local % 60 != 0) m -= 1;  // floor for times before the epoch
    if (m < 0) m += 1440;
    return static_cast<int16_t>(m);
  }

 private:
  uint32_t epoch_ = 0, atMs_ = 0;
  int16_t tz_ = 0;
  bool known_ = false;
};

// "HH:MM", or "--:--" outside 0..1439 (unknown clock).
inline void formatHhmm(int16_t minuteOfDay, char out[6]) {
  if (minuteOfDay < 0 || minuteOfDay >= 1440) {
    out[0] = out[1] = out[3] = out[4] = '-';
  } else {
    int h = minuteOfDay / 60, m = minuteOfDay % 60;
    out[0] = static_cast<char>('0' + h / 10);
    out[1] = static_cast<char>('0' + h % 10);
    out[3] = static_cast<char>('0' + m / 10);
    out[4] = static_cast<char>('0' + m % 10);
  }
  out[2] = ':';
  out[5] = 0;
}
```

- [ ] **Step 4: Implement StatusBoard**

Create `lib/deck_core/src/status_board.h`:

```cpp
#pragma once
#include <stdint.h>
#include <string.h>
#include "helper_proto.h"

struct StatusCard {
  uint8_t icon;
  char text[kCardTextMax + 1];
};

// Status text the Mac helper pushes (CARD), hardware-free. Slot 0 = top bar; slots 1..7
// rotate on strip line 2 every kRotateMs. A card with a ttl expires; empty text clears its
// slot; clear() (helper lost) empties everything. gen() changes whenever anything shown
// changes, so the glue repaints only then (resending the same text is free).
class StatusBoard {
 public:
  static constexpr uint32_t kRotateMs = 5000;

  void onCard(const CardMsg &c, uint32_t nowMs) {
    if (c.slot >= kCardSlots) return;
    Slot &s = slots_[c.slot];
    if (c.text[0] == 0) {
      if (s.live) gen_++;
      s.live = false;
      return;
    }
    bool same = s.live && s.card.icon == c.icon && strcmp(s.card.text, c.text) == 0;
    s.live = true;
    s.card.icon = c.icon;
    memcpy(s.card.text, c.text, sizeof s.card.text);
    s.expires = c.ttlS != 0;
    s.expiresAt = nowMs + static_cast<uint32_t>(c.ttlS) * 1000u;
    if (!same) gen_++;
  }

  void clear() {
    bool any = false;
    for (Slot &s : slots_) {
      any = any || s.live;
      s.live = false;
    }
    if (any) gen_++;
    cur_ = 0;
  }

  void tick(uint32_t nowMs) {
    for (Slot &s : slots_)
      if (s.live && s.expires && static_cast<int32_t>(nowMs - s.expiresAt) >= 0) {
        s.live = false;
        gen_++;
      }
    if (cur_ == 0 || !slots_[cur_].live) {
      uint8_t n = next(cur_);
      if (n != cur_) {
        cur_ = n;
        shownAt_ = nowMs;
        gen_++;
      }
    } else if (nowMs - shownAt_ >= kRotateMs) {
      uint8_t n = next(cur_);
      shownAt_ = nowMs;
      if (n != cur_) {
        cur_ = n;
        gen_++;
      }
    }
  }

  const StatusCard *top() const { return slots_[0].live ? &slots_[0].card : nullptr; }
  const StatusCard *current() const { return cur_ && slots_[cur_].live ? &slots_[cur_].card : nullptr; }

  uint8_t count() const {  // live rotating cards
    uint8_t n = 0;
    for (uint8_t i = 1; i < kCardSlots; i++) n += slots_[i].live;
    return n;
  }

  uint8_t index() const {  // 1-based position of current() among live cards, 0 if none
    if (!current()) return 0;
    uint8_t n = 0;
    for (uint8_t i = 1; i <= cur_; i++) n += slots_[i].live;
    return n;
  }

  uint8_t gen() const { return gen_; }

 private:
  struct Slot {
    bool live = false;
    bool expires = false;
    uint32_t expiresAt = 0;
    StatusCard card{};
  };

  // Next live rotating slot after `from` (wrapping); `from` itself if it is the only live
  // one; 0 if none.
  uint8_t next(uint8_t from) const {
    for (uint8_t k = 1; k < kCardSlots; k++) {
      uint8_t i = static_cast<uint8_t>((from + k - 1) % (kCardSlots - 1) + 1);
      if (slots_[i].live) return i;
    }
    return 0;
  }

  Slot slots_[kCardSlots];
  uint8_t cur_ = 0;
  uint32_t shownAt_ = 0;
  uint8_t gen_ = 0;
};
```

- [ ] **Step 5: Run, confirm pass**

Run: `~/.platformio/penv/bin/pio test -e native -f native/test_status`
Expected: 17 tests PASS. If `test_clock_midnight_wrap_and_negative_local` fails on the negative case, check the floor logic: local = −3600 s → −60 min exactly → minute 1380 (23:00).

- [ ] **Step 6: Full native run, commit**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: all suites PASS.

```bash
git add lib/deck_core/src/deck_clock.h lib/deck_core/src/status_board.h test/native/test_status/test_main.cpp
git commit -m "deck_core: DeckClock and StatusBoard (status cards, rotation, ttl)

Co-Authored-By: <model> <noreply@anthropic.com>"
```

---

### Task 3: fitLine + ScreenModel status dirty rules

**Files:**
- Modify: `lib/deck_core/src/legend.h`
- Modify: `lib/deck_core/src/ui_layout.h`
- Test: `test/native/test_legend/test_main.cpp`, `test/native/test_ui/test_main.cpp`

**Interfaces:**
- Consumes: Plan 1 `legend.h` (`GlyphWidthFn`, `legend_detail::fit`, `legend_detail::width`, `wrapLabel`, `LabelLines`), Plan 1 `ScreenState`/`ScreenModel` (fields up to `wifiOn`).
- Produces:
  - `void fitLine(const char *text, uint16_t maxPx, GlyphWidthFn w, char *out, size_t cap);` (cap ≥ 4; text beyond cap−4 chars is cut)
  - `ScreenState` gains, after `wifiOn`: `int16_t clockMinute;` (−1 unknown) and `uint8_t statusGen;`
  - Dirty rules: `clockMinute` change → `DIRTY_TOPBAR`; `statusGen` change → `DIRTY_TOPBAR | DIRTY_BANNER`.

- [ ] **Step 1: Failing tests**

In `test/native/test_legend/test_main.cpp` add above `int main`:

```cpp
// --- fitLine: one line, cut with "..." when too wide ---

void test_fitline_fits() {
  char out[49];
  fitLine("CPU 12%", 100, w10, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("CPU 12%", out);
}

void test_fitline_cuts_with_dots() {
  char out[49];
  fitLine("Daft Punk - Around the World", 100, w10, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("Daft Pu...", out);  // 7 x 10 + 30
}

void test_fitline_trims_space_before_dots() {
  char out[49];
  fitLine("Daft Punk - Around", 90, w10, out, sizeof out);  // 60 px left for text -> "Daft P"
  TEST_ASSERT_EQUAL_STRING("Daft P...", out);
  fitLine("Daft Punk", 80, w10, out, sizeof out);  // 50 px -> "Daft " -> trimmed "Daft"
  TEST_ASSERT_EQUAL_STRING("Daft...", out);
}

void test_fitline_respects_cap() {
  char out[8];
  fitLine("abcdefghijklmnop", 1000, w10, out, sizeof out);  // wide enough, but cap 8
  TEST_ASSERT_EQUAL_STRING("abcd...", out);
}
```

Register them in `main` after the wrap tests:

```cpp
  RUN_TEST(test_fitline_fits);
  RUN_TEST(test_fitline_cuts_with_dots);
  RUN_TEST(test_fitline_trims_space_before_dots);
  RUN_TEST(test_fitline_respects_cap);
```

In `test/native/test_ui/test_main.cpp`, change `base()` so the new fields are explicit:

```cpp
static ScreenState base() {
  ScreenState s{};
  s.volumePercent = 20;
  s.banner = BANNER_NONE;
  s.clockMinute = -1;
  return s;
}
```

and add:

```cpp
void test_model_clock_minute_marks_top_bar() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.clockMinute = 14 * 60 + 32;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TOPBAR, m.update(s, 0));
}

void test_model_status_gen_marks_top_bar_and_line2() {
  ScreenModel m;
  m.update(base(), 0);
  ScreenState s = base();
  s.statusGen = 1;
  TEST_ASSERT_EQUAL_HEX16(DIRTY_TOPBAR | DIRTY_BANNER, m.update(s, 0));
}
```

registered after `test_model_helper_and_wifi_mark_top_bar`.

- [ ] **Step 2: Run, confirm failure**

Run: `~/.platformio/penv/bin/pio test -e native -f native/test_legend -f native/test_ui`
Expected: compile errors `'fitLine' was not declared`, `'struct ScreenState' has no member named 'clockMinute'`.

- [ ] **Step 3: Implement fitLine and reuse it in wrapLabel**

In `lib/deck_core/src/legend.h`, add after the `legend_detail` namespace (before `wrapLabel`):

```cpp
// One line at most maxPx wide; overflow is cut and ends with "...". out holds cap bytes
// (cap >= 4); text that does not fit in cap is cut the same way.
inline void fitLine(const char *text, uint16_t maxPx, GlyphWidthFn w, char *out, size_t cap) {
  using namespace legend_detail;
  size_t n = strlen(text);
  if (n < cap && fit(text, n, maxPx, w) == n) {
    memcpy(out, text, n);
    out[n] = 0;
    return;
  }
  uint16_t dots = width("...", 3, w);
  size_t c = dots >= maxPx ? 0 : fit(text, n, static_cast<uint16_t>(maxPx - dots), w);
  if (c > cap - 4) c = cap - 4;
  while (c && text[c - 1] == ' ') c--;
  memcpy(out, text, c);
  memcpy(out + c, "...", 4);
}
```

Then in `wrapLabel` replace the tail, from `if (fit(rest, rn, maxPx, w) == rn) {` to the end of the function, with:

```cpp
  fitLine(rest, maxPx, w, out.line2, sizeof out.line2);
}
```

(`rest` is NUL-terminated because it points into `text`; `rn` is then unused, so delete its declaration and the `rn--` in the space-skip loop: the loop becomes `while (*rest == ' ') rest++;`.)

- [ ] **Step 4: Implement the ScreenState fields**

In `lib/deck_core/src/ui_layout.h`, add to `struct ScreenState` after `bool wifiOn;`:

```cpp
  int16_t clockMinute;  // local minute of day, -1 = unknown (top bar clock)
  uint8_t statusGen;    // StatusBoard::gen(): top bar text or strip card changed
```

In `ScreenModel::update`, after the `helperPresent`/`wifiOn` line add:

```cpp
      if (next.clockMinute != cur_.clockMinute) dirty |= DIRTY_TOPBAR;
      if (next.statusGen != cur_.statusGen) dirty |= DIRTY_TOPBAR | DIRTY_BANNER;
```

Update the `DIRTY_BANNER` and `DIRTY_TOPBAR` comments to `// strip line 2: banner, else status card` and `// clock, slot 0 text, helper / Wi-Fi dots`.

- [ ] **Step 5: Run, confirm pass + builds**

Run: `~/.platformio/penv/bin/pio test -e native && ~/.platformio/penv/bin/pio run -e saucedeck`
Before building, give main.cpp's aggregate the two new fields so the clock reads `--:--` until Task 4 wires the real values: in `src/main.cpp` change `hl.present(now), wifi.on()}` inside the `displayUpdate({...` call to `hl.present(now), wifi.on(), -1, 0}`.

Expected: all PASS; build SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add lib/deck_core/src/legend.h lib/deck_core/src/ui_layout.h src/main.cpp test/native/test_legend/test_main.cpp test/native/test_ui/test_main.cpp
git commit -m "deck_core: fitLine (wrapLabel reuses it); screen model tracks clock and status

Co-Authored-By: <model> <noreply@anthropic.com>"
```

---

### Task 4: Wire clock + cards into helper link, main and display

**Files:**
- Modify: `src/helper_link.h`, `src/helper_link.cpp`
- Modify: `src/main.cpp`
- Modify: `src/display.h`, `src/display.cpp`

**Interfaces:**
- Consumes: Task 1 `decodeTime`/`decodeCard`, Task 2 `DeckClock`/`StatusBoard`/`StatusCard`/`formatHhmm`, Task 3 `fitLine`/`ScreenState.clockMinute`/`statusGen`, Plan 1 `display.cpp` (`glyphAdvance`, `drawTopBar`, `drawBanner`, colours, `kLine2Y`, `kLine2H`, `kTopH`, `kW`).
- Produces:
  - `const DeckClock &helperClock();` `StatusBoard &helperStatus();` (helper_link.h)
  - `void displayUpdate(const ScreenState &state, const KeyMap &keys, TileAnim &anim, const StatusBoard &status, uint32_t nowMs);`
  - Top bar: `HH:MM` built-in font size 2 at (4, 2); slot 0 text FreeSans9pt at x 72, baseline 15, max 212 px; dots unchanged.
  - Line 2 (no banner): icon box 18×18 at (4, kLine2Y + 4); card text FreeSans9pt at x 28, baseline kLine2Y + 19, max 248 px; `n/m` built-in size 2 at (280, kLine2Y + 5).

- [ ] **Step 1: helper_link routes TIME and CARD**

`src/helper_link.h`: add includes and accessors:

```cpp
#include "deck_clock.h"
#include "status_board.h"
```

```cpp
const DeckClock &helperClock();  // last TIME from the helper (display only)
StatusBoard &helperStatus();     // CARD slots (display only); main ticks and clears it
```

`src/helper_link.cpp`: update the second header comment line to:

```cpp
// Host->deck messages can only update displayed state (HostLink, DeckClock, StatusBoard);
// nothing here runs commands.
```

Add statics after `static FrameReader reader;`:

```cpp
static DeckClock deckClock;
static StatusBoard statusBoard;
```

Replace the decode lines inside `helperPoll`'s loop:

```cpp
    HostMsg m;
    TimeMsg tm;
    CardMsg cm;
    if (decodeHost(msg, sizeof msg, m)) hostLink.onMessage(m, nowMs);
    else if (decodeTime(msg, sizeof msg, tm)) deckClock.onTime(tm.epoch, tm.tzOffsetMin, nowMs);
    else if (decodeCard(msg, sizeof msg, cm)) statusBoard.onCard(cm, nowMs);
    else rejected++;
```

Add after `helperLink()`:

```cpp
const DeckClock &helperClock() { return deckClock; }
StatusBoard &helperStatus() { return statusBoard; }
```

- [ ] **Step 2: main.cpp feeds the display**

In `loop()`, just before the `// Helper connected: show the Mac's real volume` block, add:

```cpp
  StatusBoard &status = helperStatus();
  if (!hl.present(now)) status.clear();  // cards are only true while the helper runs
  status.tick(now);
```

Replace the `displayUpdate(...)` call with:

```cpp
  displayUpdate({pct, spkMuted, micMuted, micKnown, micPct, banner, arg, keymapGen, hl.present(now), wifi.on(),
                 helperClock().minuteOfDay(now), status.gen()},
                deck.keyMap(), tileAnim, status, now);
```

- [ ] **Step 3: display.h signature**

In `src/display.h` add `#include "status_board.h"` and change the declaration:

```cpp
void displayUpdate(const ScreenState &state, const KeyMap &keys, TileAnim &anim, const StatusBoard &status,
                   uint32_t nowMs);
```

- [ ] **Step 4: display.cpp draws clock, slot 0 and cards**

In `src/display.cpp`:

Add `#include "deck_clock.h"` next to `#include "legend.h"`.

Add layout constants after `kPctX, kMicX`:

```cpp
static constexpr int16_t kTopTextX = 72, kTopTextW = 212;          // slot 0 text, up to the dots
static constexpr int16_t kCardTextX = 28, kCardTextW = 248, kIdxX = 280;
```

Add after the colour constants:

```cpp
// CARD icon ids (spec §4.4): coloured box with a letter. 0 = none.
struct CardIcon {
  uint16_t color;
  char letter;
};
static const CardIcon kIcons[kCardIcons] = {
    {kBg, ' '},     {kPurple, 'M'}, {kBlue, 'C'},  {kGreen, '%'},
    {0xFFE0, 'W'},  {kOrange, '@'}, {0x07FF, 'T'}, {kGreen, 'B'},
};
```

Replace `drawTopBar`:

```cpp
static void drawTopBar(const ScreenState &s, const StatusBoard &status) {
  tft.fillRect(0, 0, kW, kTopH, kBg);
  char hhmm[6];
  formatHhmm(s.clockMinute, hhmm);
  tft.setTextSize(2);
  tft.setTextColor(kFg, kBg);
  tft.setCursor(4, 2);
  tft.print(hhmm);
  if (const StatusCard *c = status.top()) {
    char line[kCardTextMax + 1];
    fitLine(c->text, kTopTextW, glyphAdvance, line, sizeof line);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(kFg, kFg);
    tft.setCursor(kTopTextX, 15);
    tft.print(line);
    tft.setFont(nullptr);
  }
  tft.fillCircle(296, 10, 5, s.helperPresent ? kGreen : kGrey);
  if (s.wifiOn) tft.fillCircle(310, 10, 5, kBlue);
}
```

Add after `drawBanner`:

```cpp
static void drawIcon(uint8_t icon, int16_t x, int16_t y) {
  if (icon == 0 || icon >= kCardIcons) return;
  tft.fillRoundRect(x, y, 18, 18, 4, kIcons[icon].color);
  char s[2] = {kIcons[icon].letter, 0};
  tft.setTextSize(2);
  tft.setTextColor(kBg, kIcons[icon].color);
  tft.setCursor(x + 3, y + 2);
  tft.print(s);
}

// Strip line 2: a banner wins; otherwise the rotating status card with its n/m index.
static void drawLine2(const ScreenState &s, const StatusBoard &status) {
  if (s.banner != BANNER_NONE) {
    drawBanner(s.banner, s.bannerArg);
    return;
  }
  tft.fillRect(0, kLine2Y, kW, kLine2H, kBg);
  const StatusCard *c = status.current();
  if (!c) return;
  drawIcon(c->icon, 4, kLine2Y + 4);
  char line[kCardTextMax + 1];
  fitLine(c->text, kCardTextW, glyphAdvance, line, sizeof line);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(kFg, kFg);
  tft.setCursor(kCardTextX, kLine2Y + 19);
  tft.print(line);
  tft.setFont(nullptr);
  char idx[8];
  snprintf(idx, sizeof idx, "%u/%u", static_cast<unsigned>(status.index()), static_cast<unsigned>(status.count()));
  tft.setTextSize(2);
  tft.setTextColor(kGrey, kBg);
  tft.setCursor(kIdxX, kLine2Y + 5);
  tft.print(idx);
}
```

Change the `displayUpdate` signature to match display.h, and in its body replace:

```cpp
  if (dirty & DIRTY_TOPBAR) drawTopBar(cur);
```

with

```cpp
  if (dirty & DIRTY_TOPBAR) drawTopBar(cur, status);
```

and

```cpp
  if (dirty & DIRTY_BANNER) drawBanner(cur.banner, cur.bannerArg);
```

with

```cpp
  if (dirty & DIRTY_BANNER) drawLine2(cur, status);
```

Update the comment above `drawBanner` to `// Banner text on strip line 2 (drawLine2 decides banner vs card).`

- [ ] **Step 5: Sim fake: fillRoundRect already exists; check it builds**

Run: `~/.platformio/penv/bin/pio test -e native && ~/.platformio/penv/bin/pio test -e sim && ~/.platformio/penv/bin/pio run -e saucedeck -e bringup`
Expected: all PASS / SUCCESS, no new warnings under `src/` or `lib/deck_core/`.

- [ ] **Step 6: Commit**

```bash
git add src/helper_link.h src/helper_link.cpp src/main.cpp src/display.h src/display.cpp
git commit -m "LCD: clock and slot 0 in the top bar, rotating status cards on the strip

Not bench-verified: needs the helper (tools/helper_sim.py --clock --card=...).

Co-Authored-By: <model> <noreply@anthropic.com>"
```

---

### Task 5: Sim status scenarios

**Files:**
- Modify: `test/native/test_sim_display/test_main.cpp`

**Interfaces:**
- Consumes: Task 4 layout (line 2 clear rect `(0, 214, 320, 26)`), `frameMessage`, `kFrameSize`, `kReportSize`, `MSG_*`, `board().serial.rx` (host → deck bytes), `SimDeck::knobPress/knobRelease`.
- Produces: nothing for later tasks.

- [ ] **Step 1: Add helpers and scenarios**

In `test/native/test_sim_display/test_main.cpp` add `#include <string.h>` and `#include "frame.h"` with the other includes, then add above `int main`:

```cpp
// --- fake Mac helper on the CDC port (host -> deck frames) ---

static void sendMsg(uint8_t type, const uint8_t *p, uint8_t len) {
  uint8_t m[kReportSize] = {kProtoVersion, type, len};
  memcpy(m + 3, p, len);
  uint8_t f[kFrameSize];
  frameMessage(m, f);
  board().serial.rx.insert(board().serial.rx.end(), f, f + kFrameSize);
}

static void hello() {
  const uint8_t p[1] = {1};
  sendMsg(MSG_HELLO, p, 1);
}

static void sendTime(uint32_t epoch, int16_t tz) {
  const uint8_t p[6] = {static_cast<uint8_t>(epoch), static_cast<uint8_t>(epoch >> 8),
                        static_cast<uint8_t>(epoch >> 16), static_cast<uint8_t>(epoch >> 24),
                        static_cast<uint8_t>(tz), static_cast<uint8_t>(static_cast<uint16_t>(tz) >> 8)};
  sendMsg(MSG_TIME, p, 6);
}

static void sendCard(uint8_t slot, uint8_t icon, const char *text, uint16_t ttl = 0) {
  uint8_t p[5 + kCardTextMax];
  size_t n = strlen(text);
  p[0] = slot; p[1] = icon; p[2] = ttl & 0xFF; p[3] = ttl >> 8; p[4] = static_cast<uint8_t>(n);
  memcpy(p + 5, text, n);
  sendMsg(MSG_CARD, p, static_cast<uint8_t>(5 + n));
}

// Runs ms with the helper alive (HELLO every second, like the real helper).
static void runWithHelper(SimDeck &sim, uint32_t ms) {
  while (ms) {
    hello();
    uint32_t step = ms < 1000 ? ms : 1000;
    sim.run(step);
    ms -= step;
  }
}

static bool line2Cleared() {
  for (const SimLcdRect &r : board().lcd.rects)
    if (r.x == 0 && r.y == 214 && r.w == 320 && r.h == 26) return true;
  return false;
}

SIM_TEST(test_clock_dashes_until_time_then_shows_local_time) {
  SimDeck sim;
  sim.boot();
  sim.run(kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("--:--"));
  sendTime(20000u * 86400u + 13 * 3600 + 32 * 60, 60);  // 13:32 UTC, +1 h
  runWithHelper(sim, 100);
  TEST_ASSERT_TRUE(board().lcd.printed("14:32"));
}

SIM_TEST(test_slot0_card_in_top_bar) {
  SimDeck sim;
  sim.boot();
  hello();
  sendCard(0, 2, "Standup in 12m");
  runWithHelper(sim, kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Standup in 12m"));
}

SIM_TEST(test_cards_rotate_with_index) {
  SimDeck sim;
  sim.boot();
  hello();
  sendCard(1, 1, "Daft Punk");
  sendCard(3, 3, "CPU 12%");
  runWithHelper(sim, kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Daft Punk"));
  TEST_ASSERT_TRUE(board().lcd.printed("1/2"));
  board().lcd.texts.clear();
  runWithHelper(sim, StatusBoard::kRotateMs);
  TEST_ASSERT_TRUE(board().lcd.printed("CPU 12%"));
  TEST_ASSERT_TRUE(board().lcd.printed("2/2"));
}

SIM_TEST(test_helper_silence_clears_cards) {
  SimDeck sim;
  sim.boot();
  hello();
  sendCard(1, 1, "Daft Punk");
  runWithHelper(sim, kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("Daft Punk"));
  board().lcd.texts.clear();
  board().lcd.rects.clear();
  sim.run(3500);  // no HELLO: HostLink::kTimeoutMs = 3000
  TEST_ASSERT_TRUE(line2Cleared());
  TEST_ASSERT_FALSE(board().lcd.printed("Daft Punk"));
}

SIM_TEST(test_banner_takes_line2_then_card_returns) {
  SimDeck sim;
  sim.boot();
  hello();
  sendCard(1, 1, "Daft Punk");
  runWithHelper(sim, kAfterSplash);
  sim.knobPress(1);
  sim.knobPress(2);
  runWithHelper(sim, 1500);  // > 500 ms: Wi-Fi countdown banner
  TEST_ASSERT_TRUE(board().lcd.printed("Wi-Fi in"));
  sim.knobRelease(1);
  sim.knobRelease(2);
  board().lcd.texts.clear();
  runWithHelper(sim, 500);
  TEST_ASSERT_TRUE(board().lcd.printed("Daft Punk"));
}

SIM_TEST(test_bad_card_frame_is_ignored) {
  SimDeck sim;
  sim.boot();
  hello();
  sendCard(1, 1, "ok");
  uint8_t p[5 + 3] = {1, 1, 0, 0, 3, 'a', 0x01, 'b'};  // control char -> rejected
  sendMsg(MSG_CARD, p, sizeof p);
  runWithHelper(sim, kAfterSplash);
  TEST_ASSERT_TRUE(board().lcd.printed("ok"));
  TEST_ASSERT_TRUE(board().lcd.printed("1/1"));
}
```

Add `#include "status_board.h"` (for `StatusBoard::kRotateMs`) to the includes.

Register at the end of `main`'s list:

```cpp
  RUN_TEST(test_clock_dashes_until_time_then_shows_local_time);
  RUN_TEST(test_slot0_card_in_top_bar);
  RUN_TEST(test_cards_rotate_with_index);
  RUN_TEST(test_helper_silence_clears_cards);
  RUN_TEST(test_banner_takes_line2_then_card_returns);
  RUN_TEST(test_bad_card_frame_is_ignored);
```

- [ ] **Step 2: Run**

Run: `~/.platformio/penv/bin/pio test -e sim -f native/test_sim_display`
Expected: all scenarios PASS (these test behaviour already built in Task 4; TDD red for this behaviour came from the native suites). If one fails, it is a real firmware bug: reproduce it in the matching native suite first (CLAUDE.md "bug fix = failing test first"), fix in `lib/deck_core/`, re-run. Do not loosen the scenario.

To prove the scenarios can fail, temporarily comment out `status.tick(now);` in `src/main.cpp`, run, confirm `test_cards_rotate_with_index` fails, restore it, and paste both outputs in the report.

- [ ] **Step 3: Full suites, commit**

Run: `~/.platformio/penv/bin/pio test -e sim && ~/.platformio/penv/bin/pio test -e native`
Expected: all PASS.

```bash
git add test/native/test_sim_display/test_main.cpp
git commit -m "Sim: clock, status cards, rotation, helper loss and banner scenarios

Co-Authored-By: <model> <noreply@anthropic.com>"
```

---

### Task 6: helper_sim.py sends TIME and CARD

**Files:**
- Modify: `tools/helper_sim.py`

**Interfaces:**
- Consumes: frame format (`frame()` already in the script), Task 1 payload layouts.
- Produces: CLI `--clock` (TIME every second with the Mac's local offset) and repeatable `--card=SLOT:ICON:TEXT` (ICON = 0–7 or name: none music calendar cpu weather mail chat battery).

Bench tool, no unit tests (CLAUDE.md: hardware-side tooling). Verify with a dry run that builds frames without a port.

- [ ] **Step 1: Edit the script**

Update the docstring usage line:

```python
Usage: ~/.platformio/penv/bin/python -I tools/helper_sim.py <volume%> <mic%> [seconds] [--junk] [--clock] [--card=SLOT:ICON:TEXT ...] [--dry]
Sends framed HELLO every second plus VOLUME / MIC (and TIME / CARD when asked), prints any
deck->host messages. --dry prints the frames it would send and exits (no port needed).
```

Add `import datetime, struct` to the imports line (keep the others).

Add after `frame()`:

```python
ICONS = {"none": 0, "music": 1, "calendar": 2, "cpu": 3, "weather": 4, "mail": 5, "chat": 6, "battery": 7}

def time_frame():
    off = int(datetime.datetime.now().astimezone().utcoffset().total_seconds() // 60)
    return frame(0x04, list(struct.pack("<Ih", int(time.time()), off)))

def card_frame(spec):
    slot, icon, text = spec.split(":", 2)
    icon = ICONS[icon] if icon in ICONS else int(icon)
    t = text.encode("ascii", "replace")[:48]  # deck accepts printable ASCII only
    return frame(0x05, [int(slot), icon, 0, 0, len(t)] + list(t))
```

In `main()`, after `junk = "--junk" in sys.argv` add:

```python
    clock = "--clock" in sys.argv
    cards = [card_frame(a[len("--card="):]) for a in sys.argv if a.startswith("--card=")]
    if "--dry" in sys.argv:
        for f in ([time_frame()] if clock else []) + cards:
            print(f.hex())
        return
```

In the send block, replace:

```python
            s.write(frame(0x01, [1]) + frame(0x02, [vol, 0]) + frame(0x03, [mic, 0]))
```

with:

```python
            s.write(frame(0x01, [1]) + frame(0x02, [vol, 0]) + frame(0x03, [mic, 0]))
            if clock:
                s.write(time_frame())
            for c in cards:
                s.write(c)
```

- [ ] **Step 2: Dry run**

Run: `~/.platformio/penv/bin/python -I tools/helper_sim.py 50 50 --clock --card=1:music:Daft\ Punk --card=0:2:Standup --dry`
Expected: three hex lines, each 134 hex chars (67 bytes), starting `a55a0104` (TIME), `a55a0105` (CARD), `a55a0105`.

- [ ] **Step 3: Commit**

```bash
git add tools/helper_sim.py
git commit -m "helper_sim: --clock and --card for bench tests of the status display

Co-Authored-By: <model> <noreply@anthropic.com>"
```

---

### Task 7: Done check, size record, deploy (flash), smoke test

**Files:**
- Modify: `docs/kb/toolchain.md`
- Create: `docs/bringup/2026-10-31.log`

- [ ] **Step 1: Done check**

Run:

```bash
~/.platformio/penv/bin/pio test -e native 2>&1 | tail -5
~/.platformio/penv/bin/pio test -e sim 2>&1 | tail -5
~/.platformio/penv/bin/pio run -e saucedeck -e bringup 2>&1 | tee .superpowers/sdd/build.log | tail -15
grep -n "warning:" .superpowers/sdd/build.log | grep -E "src/|lib/deck_core/" || echo "no warnings in our sources"
```

Expected: all PASS, both envs SUCCESS, "no warnings in our sources".

- [ ] **Step 2: Record size**

From the saucedeck `Flash:` line, append to `docs/kb/toolchain.md`:

```markdown
- **Firmware with legend layout + status cards + FreeSans9pt7b (2026-10-31) = N B = xx.x % of the 3 MB huge_app slot.** `[verified: build output 2026-10-31]`
```

(N and xx.x from the build output.)

- [ ] **Step 3: Flash**

Run: `ls /dev/cu.usbmodem*` (note the port). Then `~/.platformio/penv/bin/pio run -e saucedeck -t upload`.
If it fails with "Couldn't find a board": rerun with `--upload-port /dev/cu.usbmodem<id>` from the `ls`. If the port is busy, check `lsof /dev/cu.usbmodem*` and report which process holds it (do not kill processes you did not start).
Expected: `Hard resetting via RTS pin...` and `[SUCCESS]`.

- [ ] **Step 4: Smoke test over USB**

Wait ~3 s for re-enumeration, then `ls /dev/cu.usbmodem*` (port back). Run:
`~/.platformio/penv/bin/python -I tools/helper_sim.py 40 60 8 --clock --card=0:calendar:Standup\ in\ 12m --card=1:music:Daft\ Punk --card=2:cpu:CPU\ 12%`
Expected: script prints `done` with no traceback (the deck accepted the port at 115200 and did not reset). Any `deck:` lines are fine (none expected without key presses).

- [ ] **Step 5: Log + commit**

Write `docs/bringup/2026-10-31.log` with: firmware commit SHA, the done-check summary lines, flash output tail, smoke-test output, and the line `Operator visual check: pending (legends, fade, clock, cards on the panel).`

```bash
git add docs/kb/toolchain.md docs/bringup/2026-10-31.log
git commit -m "Flash + smoke test: legends, press feedback, status cards (not bench-verified visually)

Co-Authored-By: <model> <noreply@anthropic.com>"
```
