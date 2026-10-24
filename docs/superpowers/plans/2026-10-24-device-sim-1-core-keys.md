# Device Sim — Plan 1: Coverage Tooling, Sim Core, Key Scenarios

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the real HID firmware (`src/`) on the host against a simulated SAUCEDECK board, prove the 12 keys end to end (MCP23017 pin → HID report the Mac receives), and measure coverage with a script that later becomes the CI gate.

**Architecture:** A PlatformIO library `test/sim/lib/simdeck` provides host stand-ins for every hardware header `src/` includes, backed by small board models (clock, GPIO, MCP23017 register file, USB host, NVS, …) collected in one `SimBoard`. Env `sim` compiles the real `src/` (minus bringup/spikes) against it. Each `SIM_TEST` runs in a `fork()`ed child so `setup()` sees fresh statics, like a power-on.

**Tech Stack:** PlatformIO `native` platform, Unity, C++17, POSIX `fork`/`pipe`, gcov + gcovr 8.6.

**Spec:** `docs/superpowers/specs/2026-10-24-device-sim-coverage-design.md`. This is plan 1 of 4:
1. this plan: tooling + sim core + keys;
2. knobs (PCNT model), display + LED models and suites;
3. Wi-Fi/portal + helper suites, `reboot()`;
4. bringup env (serial commands, bit-level I2C, ST7789 decoder), gap closing, CI gate.

## Global Constraints

- Coverage target (enforced in plan 4): **≥ 95 % lines and ≥ 95 % branches** over `lib/deck_core/src`, `src/`, `src/bringup`; excluded: `src/spike_*`, `test/`, the fakes.
- gcovr flags: `--exclude-unreachable-branches --exclude-throw-branches`. Max 10 `GCOVR_EXCL` markers in `src/` + `lib/`, each with a reason.
- Production code compiles unchanged: **no `#ifdef TEST` (or similar) in `src/` or `lib/`.** If host compilation exposes a real warning in `src/`, fix the code properly; never silence it.
- Every fake/model header states the hardware behaviour it assumes with a source or `[unverified]`.
- Build flags for sim code: `-std=gnu++17 -Wall -Wextra -Werror` (same as `native`).
- `pio` means `~/.platformio/penv/bin/pio` locally (PATH has an obsolete 6.1.19). CI uses its own pinned `pio`.
- Done per task: `pio test -e native` green, `pio test -e sim` green, `pio run -e saucedeck -e bringup` SUCCESS with zero new warnings. Show output.
- Chat with the user in caveman style (CLAUDE.md); code, comments, docs and commits in normal English.

## File Structure

```
platformio.ini                         modify: native test_ignore; envs native_cov, sim
tools/coverage_flags.py                create: adds --coverage to LINKFLAGS
tools/coverage.sh                      create: per-suite gcovr JSON, merge, report, gate
test/sim/lib/simdeck/library.json      create: library manifest
test/sim/lib/simdeck/src/
  sim/clock.h                          create: sim time
  sim/gpio.h                           create: pin levels, pull-ups
  sim/mcp23017.h                       create: MCP23017 register model
  sim/host.h                           create: what the Mac receives (HID, consumer)
  sim/nvs.h                            create: Preferences store
  sim/serial_port.h                    create: USB CDC byte pipes
  sim/wifi.h                           create: Wi-Fi + WiFiManager state (minimal here)
  sim/lcd.h                            create: LCD call recorder (minimal here)
  sim/leds.h                           create: NeoPixel buffer
  sim/pcnt.h                           create: PCNT units (counting model in plan 2)
  sim_board.h / sim_board.cpp          create: SimBoard aggregate, board(), I2C routing
  sim_deck.h / sim_deck.cpp            create: test facade (boot, run, press, faults, presets)
  sim_test.h / sim_test.cpp            create: SIM_TEST fork-per-test harness
  Arduino.h  Wire.h  SPI.h  USB.h  USBHIDKeyboard.h  USBHIDConsumerControl.h
  Preferences.h  WiFi.h  WiFiManager.h  ESPmDNS.h  Adafruit_GFX.h  Adafruit_ST7789.h
  Adafruit_NeoPixel.h  esp_err.h  esp_random.h  esp_system.h
  driver/gpio.h  driver/pulse_cnt.h    create: fakes
  fakes.cpp                            create: fake globals + function bodies
test/native/test_sim_keys/test_main.cpp  create: harness + key scenarios
.github/workflows/ci.yml               modify: add "Device sim tests" step
CLAUDE.md                              modify: commands
```

---

### Task 1: Coverage tooling and baseline

**Files:**
- Create: `tools/coverage_flags.py`, `tools/coverage.sh`
- Modify: `platformio.ini` (env `native`: add `test_ignore`; add env `native_cov`)

**Interfaces:**
- Produces: env `native_cov`; script `tools/coverage.sh` honouring `MIN_LINE`, `MIN_BRANCH` (default 95), `PIO` (default `pio`); output `.pio/coverage/summary.txt`, `.pio/coverage/index.html`, `.pio/coverage/json/*.json`. Later tasks add envs `sim` (this plan) and `sim_bringup` (plan 4); the script already routes suites `native/test_sim_bringup` → `sim_bringup`, other `native/test_sim_*` → `sim`, everything else → `native_cov`, skipping envs not defined yet.

- [ ] **Step 1: Add the linker flag script**

`tools/coverage_flags.py`:

```python
# PlatformIO extra script for coverage envs. build_flags already passes --coverage to every
# compile (project and libraries); PlatformIO does not pass it to the linker, so add it here.
# Without it the link fails with "Undefined symbols: ___llvm_gcov_writeout" (seen 2026-10-24).
Import("env")
env.Append(LINKFLAGS=["--coverage"])
```

- [ ] **Step 2: Add the `native_cov` env and keep sim suites out of `native`**

In `platformio.ini`, in `[env:native]`, add after `test_filter = native/*`:

```ini
test_ignore = native/test_sim_*
```

After the `[env:native]` block (before the spike envs), add:

```ini
; native + gcov instrumentation, used by tools/coverage.sh
[env:native_cov]
extends = env:native
build_flags = ${env:native.build_flags} --coverage
extra_scripts = tools/coverage_flags.py
```

- [ ] **Step 3: Write the coverage script**

`tools/coverage.sh`:

```bash
#!/usr/bin/env bash
# Coverage over lib/deck_core/src, src/ and src/bringup (spikes excluded).
# Runs every test suite in its env, saves one gcovr JSON per suite (PlatformIO rebuilds
# between suites), merges them, prints a summary and fails below MIN_LINE / MIN_BRANCH.
#   tools/coverage.sh                    gate at 95 % / 95 %
#   MIN_LINE=0 MIN_BRANCH=0 tools/coverage.sh   report only
set -euo pipefail
cd "$(dirname "$0")/.."

PIO=${PIO:-pio}
MIN_LINE=${MIN_LINE:-95}
MIN_BRANCH=${MIN_BRANCH:-95}
MAX_EXCL=10
OUT=.pio/coverage
if [ "$(uname)" = Darwin ]; then GCOV_EXE="xcrun llvm-cov gcov"; else GCOV_EXE=gcov; fi

COMMON=(--root . --filter 'lib/deck_core/src/' --filter 'src/' --exclude 'src/spike_'
        --exclude-unreachable-branches --exclude-throw-branches)

env_defined() { grep -q "^\[env:$1\]" platformio.ini; }

rm -rf "$OUT"
mkdir -p "$OUT/json"

for dir in test/native/test_*/; do
  suite="native/$(basename "$dir")"
  case "$suite" in
    native/test_sim_bringup) env=sim_bringup ;;
    native/test_sim_*) env=sim ;;
    *) env=native_cov ;;
  esac
  if ! env_defined "$env"; then
    echo "skip $suite (env $env not defined yet)"
    continue
  fi
  find ".pio/build/$env" -name '*.gcda' -delete 2>/dev/null || true
  "$PIO" test -e "$env" -f "$suite"
  gcovr "${COMMON[@]}" --gcov-executable "$GCOV_EXE" --object-directory ".pio/build/$env" \
    --json "$OUT/json/${env}_$(basename "$dir").json" ".pio/build/$env"
done

TRACES=()
for f in "$OUT"/json/*.json; do TRACES+=(--add-tracefile "$f"); done
gcovr "${TRACES[@]}" --txt "$OUT/summary.txt" --html-details "$OUT/index.html" \
  --print-summary --fail-under-line "$MIN_LINE" --fail-under-branch "$MIN_BRANCH"

excl=$( (grep -rE 'GCOVR_EXCL' src lib || true) | grep -vc '^src/spike_' || true)
echo "GCOVR_EXCL markers: $excl (max $MAX_EXCL)"
if [ "$excl" -gt "$MAX_EXCL" ]; then
  echo "too many GCOVR_EXCL markers"
  exit 1
fi
```

Make it executable: `chmod +x tools/coverage.sh`.

- [ ] **Step 4: Run the baseline (report only)**

Run: `PIO=~/.platformio/penv/bin/pio MIN_LINE=0 MIN_BRANCH=0 tools/coverage.sh 2>&1 | tail -15`

Expected: all native suites PASS, `skip native/test_sim_*` lines absent (no sim suites yet), a summary like `lines: NN.N% … branches: NN.N%`, exit 0. `ls .pio/coverage/json | wc -l` = number of native suites (10).
Record the two baseline numbers in the commit message.

If gcovr reports `stamp mismatch` or zero files: check `find .pio/build/native_cov -name '*.gcno' | head` is non-empty; if empty, the `--coverage` build flag did not reach the compile, fix the env before continuing.

- [ ] **Step 5: Check the normal env is unaffected**

Run: `~/.platformio/penv/bin/pio test -e native 2>&1 | grep "test cases"`
Expected: `198 test cases: 198 succeeded`.

- [ ] **Step 6: Commit**

```bash
git add tools/coverage_flags.py tools/coverage.sh platformio.ini
git commit -m "Coverage tooling: native_cov env + tools/coverage.sh (baseline: lines X%, branches Y%)"
```

(Replace X/Y with the measured numbers. End the message with the Co-Authored-By line from the session.)

---

### Task 2: Sim library, fakes and fork harness; firmware boots on the host

**Files:**
- Create: everything under `test/sim/lib/simdeck/` listed in File Structure
- Create: `test/native/test_sim_keys/test_main.cpp` (harness + boot tests only in this task)
- Modify: `platformio.ini` (add env `sim`)

**Interfaces:**
- Produces (used by Tasks 3–5 and plans 2–4):
  - `SimBoard &board();` with members `clock, gpio, mcp, host, nvs, serial, wifi, lcd, leds, pcnt, ledcDuty[49], driveCap[49], resetReason, usbStoppedHandler, rng`.
  - `class SimDeck { void boot(); void run(uint32_t ms); void press(uint8_t pos); void release(uint8_t pos); void knobPress(uint8_t n); void knobRelease(uint8_t n); SimHost &host(); SimMcp23017 &mcp(); SimFaults fault(); bool loadPresetIntoNvs(const char *path); static uint8_t mcpBitForKey(uint8_t pos); }`
  - `struct SimFaults { void mcpAbsent(bool absent); }`
  - `SIM_TEST(name)` macro; `void simRunForked(void (*body)());`
  - `struct KbReport { uint8_t mods; uint8_t keys[6]; };` `SimHost::{kb, consumer, current(), keyDown(usage), mods(), allKeysUp()}`
  - Macro `SIM_PROJECT_DIR` (absolute project path string).

- [ ] **Step 1: Write the failing boot + harness tests**

`test/native/test_sim_keys/test_main.cpp`:

```cpp
// Scenario tests: the real src/ firmware on the simulated board (test/sim/lib/simdeck).
#include <stdio.h>
#include <unity.h>
#include <vector>
#include "mcp23017.h"
#include "sim_deck.h"
#include "sim_test.h"

void setUp() {}
void tearDown() {}

// --- harness ---

SIM_TEST(test_harness_a_mutates_board) {
  board().mcp.extLow = 0xFFFF;
  board().clock.advanceMs(1234);
  SimDeck sim;
  sim.boot();
}

// Runs after test_harness_a: passes only if each SIM_TEST gets a fresh process.
SIM_TEST(test_harness_b_sees_fresh_board) {
  TEST_ASSERT_EQUAL_HEX16(0, board().mcp.extLow);
  TEST_ASSERT_EQUAL_UINT32(0, board().clock.millis());
  TEST_ASSERT_EQUAL_HEX8(0xFF, board().mcp.reg[MCP_IODIRA]);  // POR value, setup() never ran here
}

// --- boot ---

SIM_TEST(test_boot_configures_mcp_inputs_pullups_and_output_only_pins) {
  SimDeck sim;
  sim.boot();
  SimMcp23017 &m = sim.mcp();
  TEST_ASSERT_EQUAL_HEX8(0x7F, m.reg[MCP_IODIRA]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, m.reg[MCP_IODIRB]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, m.reg[MCP_GPPUA]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, m.reg[MCP_GPPUB]);
  TEST_ASSERT_EQUAL_HEX8(0x00, m.reg[MCP_OLATA]);
  TEST_ASSERT_EQUAL_HEX8(0x00, m.reg[MCP_OLATB]);
  sim.run(50);
  TEST_ASSERT_FALSE(m.outputOnlyAsInput);
  TEST_ASSERT_TRUE(sim.host().kb.empty());  // idle deck sends nothing
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_harness_a_mutates_board);
  RUN_TEST(test_harness_b_sees_fresh_board);
  RUN_TEST(test_boot_configures_mcp_inputs_pullups_and_output_only_pins);
  return UNITY_END();
}
```

- [ ] **Step 2: Add the `sim` env**

In `platformio.ini`, after `[env:native_cov]`:

```ini
; Real src/ (HID firmware) on the host against the simulated board (test/sim/lib/simdeck)
[env:sim]
platform = native
test_framework = unity
test_filter = native/test_sim_*
test_ignore = native/test_sim_bringup
test_build_src = yes
build_src_filter = +<*> -<bringup/> -<spike_wifi/> -<spike_hid/>
lib_extra_dirs = test/sim/lib
lib_deps = simdeck
build_flags =
    -std=gnu++17 -Wall -Wextra -Werror --coverage
    '-DSIM_PROJECT_DIR="$PROJECT_DIR"'
extra_scripts = tools/coverage_flags.py
```

- [ ] **Step 3: Run to see it fail**

Run: `~/.platformio/penv/bin/pio test -e sim 2>&1 | tail -5`
Expected: build FAILS, e.g. `Library Manager: simdeck` not found or `'sim_deck.h' file not found`.

- [ ] **Step 4: Library manifest**

`test/sim/lib/simdeck/library.json`:

```json
{
  "name": "simdeck",
  "version": "0.1.0",
  "description": "Host fakes + simulated SAUCEDECK board for scenario tests (test only)",
  "frameworks": "*",
  "platforms": "native",
  "build": { "flags": "-Wno-unused-parameter" }
}
```

(`-Wno-unused-parameter` applies to the fakes only, whose signatures mirror vendor APIs.)

- [ ] **Step 5: Board models**

`test/sim/lib/simdeck/src/sim/clock.h`:

```cpp
#pragma once
#include <stdint.h>

// Sim time. Firmware sees millis()/micros()/delay(); tests move time with SimDeck::run().
struct SimClock {
  uint64_t us = 0;
  uint32_t millis() const { return static_cast<uint32_t>(us / 1000); }
  uint32_t micros() const { return static_cast<uint32_t>(us); }
  void advanceMs(uint32_t ms) { us += static_cast<uint64_t>(ms) * 1000; }
  void advanceUs(uint32_t u) { us += u; }
};
```

`test/sim/lib/simdeck/src/sim/gpio.h`:

```cpp
#pragma once
#include <stdint.h>

// ESP32-S3 GPIO levels as the firmware reads them.
// Model choices: an input nothing drives reads 1 with a pull-up, else 0 (real pin floats).
struct SimGpio {
  static constexpr int kPins = 49;  // GPIO0..48
  enum Mode : uint8_t { In, Out, InPullup };
  Mode mode[kPins] = {};
  bool out[kPins] = {};
  bool pullup[kPins] = {};
  int8_t ext[kPins];  // level driven from outside (switch, encoder), -1 = none

  SimGpio() {
    for (int i = 0; i < kPins; i++) ext[i] = -1;
  }
  int read(int pin) const {
    if (pin < 0 || pin >= kPins) return 0;
    if (mode[pin] == Out) return out[pin] ? 1 : 0;
    if (ext[pin] >= 0) return ext[pin];
    return (pullup[pin] || mode[pin] == InPullup) ? 1 : 0;
  }
};
```

`test/sim/lib/simdeck/src/sim/mcp23017.h`:

```cpp
#pragma once
#include <stdint.h>
#include <string.h>

// MCP23017, IOCON.BANK = 0 (POR). Registers: Microchip DS20001952D Table 3-1 (docs/kb/peripherals.md).
// GPA7/GPB7 are output-only on MCP23017 (DS20001952D pin table): reading GPIO while IODIR bit 7
// is 1 sets outputOnlyAsInput so tests can catch it.
// Sequential mode (SEQOP = 0): the address pointer increments after each byte.
// [unverified: wrap point; this model wraps 0x15 -> 0x00]
// Input without pull-up and not driven reads 0 here (a real pin floats).
struct SimMcp23017 {
  static constexpr uint8_t kRegs = 0x16;
  uint8_t addr = 0x27;  // Waveshare board, A0-A2 open (bench-verified 2026-10-18)
  bool present = true;  // false: NACKs every transfer (unplugged / bad contact)
  uint8_t reg[kRegs] = {};
  uint8_t ptr = 0;
  uint16_t extLow = 0;  // bit n (GPA0..7 = 0..7, GPB0..7 = 8..15) pulled LOW from outside
  bool outputOnlyAsInput = false;

  SimMcp23017() { reset(); }
  void reset() {
    memset(reg, 0, sizeof reg);
    reg[0x00] = 0xFF;  // IODIRA POR
    reg[0x01] = 0xFF;  // IODIRB POR
    ptr = 0;
  }
  void writeReg(uint8_t r, uint8_t v) {
    if (r == 0x12) r = 0x14;  // write to GPIOA writes OLATA
    if (r == 0x13) r = 0x15;
    if (r < kRegs) reg[r] = v;
  }
  uint8_t port(int p) {
    uint8_t iodir = reg[0x00 + p], gppu = reg[0x0C + p], olat = reg[0x14 + p];
    if (iodir & 0x80) outputOnlyAsInput = true;
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
      uint8_t bit = 1u << i;
      bool level;
      if (!(iodir & bit)) level = olat & bit;
      else if (extLow & (1u << (p * 8 + i))) level = false;
      else level = gppu & bit;
      if (level) v |= bit;
    }
    return v;
  }
  uint8_t readReg(uint8_t r) {
    if (r == 0x12) return port(0);
    if (r == 0x13) return port(1);
    return r < kRegs ? reg[r] : 0;
  }
  void next() { ptr = static_cast<uint8_t>((ptr + 1) % kRegs); }
};
```

`test/sim/lib/simdeck/src/sim/host.h`:

```cpp
#pragma once
#include <stdint.h>
#include <vector>

// What the Mac receives. Boot keyboard report: modifiers + 6 key slots (USB HID 1.11, B.1).
struct KbReport {
  uint8_t mods;
  uint8_t keys[6];
};

struct SimHost {
  std::vector<KbReport> kb;
  std::vector<uint16_t> consumer;  // usage on press, 0 on release

  KbReport current() const { return kb.empty() ? KbReport{0, {0, 0, 0, 0, 0, 0}} : kb.back(); }
  bool keyDown(uint8_t usage) const {
    KbReport r = current();
    for (uint8_t k : r.keys)
      if (k == usage) return true;
    return false;
  }
  uint8_t mods() const { return current().mods; }
  bool allKeysUp() const {
    KbReport r = current();
    if (r.mods) return false;
    for (uint8_t k : r.keys)
      if (k) return false;
    return true;
  }
};
```

`test/sim/lib/simdeck/src/sim/nvs.h`:

```cpp
#pragma once
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

// NVS as Preferences sees it: namespace -> key -> bytes.
struct SimNvs {
  std::map<std::string, std::map<std::string, std::vector<uint8_t>>> ns;
  bool hasNamespace(const std::string &n) const { return ns.count(n) != 0; }
};
```

`test/sim/lib/simdeck/src/sim/serial_port.h`:

```cpp
#pragma once
#include <stdint.h>
#include <deque>
#include <string>
#include <vector>

// USB CDC as both ends see it. rx = host -> deck, tx = deck -> host.
struct SimSerialPort {
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  uint32_t baud = 0;
  std::string text() const { return std::string(tx.begin(), tx.end()); }
};
```

`test/sim/lib/simdeck/src/sim/wifi.h`:

```cpp
#pragma once
#include <stdint.h>
#include <string>

class WiFiManager;

// Wi-Fi radio + WiFiManager as the firmware sees them. Behaviour knobs are set by tests
// (plan 3 adds form submit and link drops).
struct SimWifi {
  bool joinOk = false;     // saved credentials work
  bool portalOk = true;    // SAUCEDECK-setup AP comes up
  bool connected = false;
  bool portalActive = false;
  bool webPortal = false;
  int mode = 0;            // wifi_mode_t
  int txPower = 0;
  int8_t rssi = -55;
  uint8_t ip[4] = {192, 168, 1, 50};
  std::string mdnsName;
  std::string apSsid, apPass;
  WiFiManager *manager = nullptr;
};
```

`test/sim/lib/simdeck/src/sim/lcd.h`:

```cpp
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// ST7789 via the Adafruit API: records what was printed (plan 2 adds a framebuffer).
struct SimLcdText {
  int16_t x, y;
  uint8_t size;
  uint16_t fg;
  std::string s;
};

struct SimLcd {
  bool inited = false;
  bool asleep = false;
  uint8_t rotation = 0;
  uint32_t spiHz = 0;
  uint32_t fills = 0;
  std::vector<SimLcdText> texts;
  bool printed(const std::string &s) const {
    for (const SimLcdText &t : texts)
      if (t.s.find(s) != std::string::npos) return true;
    return false;
  }
};
```

`test/sim/lib/simdeck/src/sim/leds.h`:

```cpp
#pragma once
#include <stdint.h>
#include <vector>

// WS2812 strip via the Adafruit NeoPixel API: last shown frame.
struct SimRgb {
  uint8_t r, g, b;
};

struct SimLeds {
  uint16_t count = 0;
  int16_t pin = -1;
  uint8_t brightness = 255;
  uint32_t shows = 0;
  std::vector<SimRgb> pending, shown;
};
```

`test/sim/lib/simdeck/src/sim/pcnt.h`:

```cpp
#pragma once
#include <stdint.h>

// ESP32-S3 PCNT units as the IDF 5 driver exposes them. Plan 1: configuration is stored and
// the count stays where tests put it. Plan 2 adds the edge/level counting model.
struct SimPcntChannel {
  int edgePin = -1, levelPin = -1;
  int posEdge = 0, negEdge = 0;   // pcnt_channel_edge_action_t
  int highLevel = 0, lowLevel = 0;  // pcnt_channel_level_action_t
};

struct SimPcntUnit {
  bool used = false, enabled = false, started = false;
  int low = 0, high = 0, count = 0;
  uint32_t glitchNs = 0;
  SimPcntChannel ch[2];
  int channels = 0;
};

struct SimPcnt {
  static constexpr int kUnits = 4;  // SOC_PCNT_UNITS_PER_GROUP (docs/kb/esp32s3.md)
  SimPcntUnit unit[kUnits];
  bool failNewUnit = false;  // fault: driver refuses a unit -> firmware falls back to polling
};
```

- [ ] **Step 6: SimBoard**

`test/sim/lib/simdeck/src/sim_board.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_system.h"
#include "sim/clock.h"
#include "sim/gpio.h"
#include "sim/host.h"
#include "sim/lcd.h"
#include "sim/leds.h"
#include "sim/mcp23017.h"
#include "sim/nvs.h"
#include "sim/pcnt.h"
#include "sim/serial_port.h"
#include "sim/wifi.h"

typedef const char *esp_event_base_t;
typedef void (*sim_usb_handler_t)(void *arg, esp_event_base_t base, int32_t id, void *data);

// Everything on (and attached to) the simulated SAUCEDECK. One instance per process; each
// SIM_TEST runs in a fresh child process, so it always starts at power-on state.
struct SimBoard {
  SimClock clock;
  SimGpio gpio;
  SimMcp23017 mcp;
  SimHost host;
  SimNvs nvs;
  SimSerialPort serial;
  SimWifi wifi;
  SimLcd lcd;
  SimLeds leds;
  SimPcnt pcnt;
  uint32_t ledcDuty[SimGpio::kPins] = {};
  int driveCap[SimGpio::kPins] = {};
  esp_reset_reason_t resetReason = ESP_RST_POWERON;
  sim_usb_handler_t usbStoppedHandler = nullptr;
  uint32_t rng = 0x2545F491u;

  // I2C transfers as Wire sees them. Return values follow Arduino TwoWire::endTransmission:
  // 0 = ACK, 2 = address NACK.
  uint8_t i2cWrite(uint8_t addr, const uint8_t *data, size_t n);
  size_t i2cRead(uint8_t addr, uint8_t *out, size_t n);
};

SimBoard &board();
```

`test/sim/lib/simdeck/src/sim_board.cpp`:

```cpp
#include "sim_board.h"

SimBoard &board() {
  static SimBoard b;
  return b;
}

uint8_t SimBoard::i2cWrite(uint8_t addr, const uint8_t *data, size_t n) {
  if (addr != mcp.addr || !mcp.present) return 2;
  if (n == 0) return 0;  // address-only probe (bus scan)
  mcp.ptr = data[0] % SimMcp23017::kRegs;
  for (size_t i = 1; i < n; i++) {
    mcp.writeReg(mcp.ptr, data[i]);
    mcp.next();
  }
  return 0;
}

size_t SimBoard::i2cRead(uint8_t addr, uint8_t *out, size_t n) {
  if (addr != mcp.addr || !mcp.present) return 0;
  for (size_t i = 0; i < n; i++) {
    out[i] = mcp.readReg(mcp.ptr);
    mcp.next();
  }
  return n;
}
```

- [ ] **Step 7: Core fakes (Arduino, ESP-IDF bits)**

`test/sim/lib/simdeck/src/esp_err.h`:

```cpp
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
```

`test/sim/lib/simdeck/src/esp_system.h`:

```cpp
#pragma once
// Order as in ESP-IDF 5 esp_system.h [unverified: exact enum values; firmware only compares names]
typedef enum {
  ESP_RST_UNKNOWN,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO,
} esp_reset_reason_t;

esp_reset_reason_t esp_reset_reason();
```

`test/sim/lib/simdeck/src/esp_random.h`:

```cpp
#pragma once
#include <stdint.h>
// Deterministic in the sim (xorshift32), so the AP password is reproducible in tests.
uint32_t esp_random();
```

`test/sim/lib/simdeck/src/Arduino.h`:

```cpp
#pragma once
// Arduino-ESP32 core 3.x subset used by src/. Behaviour lives in sim_board.h models.
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "esp_err.h"
#include "esp_system.h"

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05

typedef bool boolean;

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t val);
bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
bool ledcWrite(uint8_t pin, uint32_t duty);

// Minimal Arduino String (only what src/ uses: construction + c_str()).
class String {
 public:
  String(const char *s = "") : s_(s) {}
  explicit String(const std::string &s) : s_(s) {}
  const char *c_str() const { return s_.c_str(); }

 private:
  std::string s_;
};

// USB CDC serial (ARDUINO_USB_CDC_ON_BOOT=1). Bytes go to board().serial.
class HWCDC {
 public:
  void begin(uint32_t baud);
  void setTxTimeoutMs(uint32_t ms);
  int available();
  int read();
  size_t write(uint8_t b);
  size_t write(const uint8_t *buf, size_t n);
  size_t print(const char *s);
  size_t println(const char *s = "");
  size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};
extern HWCDC Serial;
```

`test/sim/lib/simdeck/src/driver/gpio.h`:

```cpp
#pragma once
#include "esp_err.h"

enum gpio_num_t : int {};
typedef enum { GPIO_DRIVE_CAP_0, GPIO_DRIVE_CAP_1, GPIO_DRIVE_CAP_2, GPIO_DRIVE_CAP_3 } gpio_drive_cap_t;

esp_err_t gpio_set_drive_capability(gpio_num_t pin, gpio_drive_cap_t cap);
esp_err_t gpio_pullup_en(gpio_num_t pin);
```

`test/sim/lib/simdeck/src/driver/pulse_cnt.h`:

```cpp
#pragma once
#include <stdint.h>
#include "esp_err.h"

// ESP-IDF 5 PCNT driver API subset (driver/pulse_cnt.h), backed by board().pcnt.
struct sim_pcnt_unit_t;
struct sim_pcnt_chan_t;
typedef sim_pcnt_unit_t *pcnt_unit_handle_t;
typedef sim_pcnt_chan_t *pcnt_channel_handle_t;

typedef struct {
  int low_limit;
  int high_limit;
  int intr_priority;
  struct {
    uint32_t accum_count : 1;
  } flags;
} pcnt_unit_config_t;

typedef struct {
  uint32_t max_glitch_ns;
} pcnt_glitch_filter_config_t;

typedef struct {
  int edge_gpio_num;
  int level_gpio_num;
  struct {
    uint32_t invert_edge_input : 1;
    uint32_t invert_level_input : 1;
    uint32_t virt_edge_io_level : 1;
    uint32_t virt_level_io_level : 1;
    uint32_t io_loop_back : 1;
  } flags;
} pcnt_chan_config_t;

typedef enum {
  PCNT_CHANNEL_EDGE_ACTION_HOLD,
  PCNT_CHANNEL_EDGE_ACTION_INCREASE,
  PCNT_CHANNEL_EDGE_ACTION_DECREASE,
} pcnt_channel_edge_action_t;

typedef enum {
  PCNT_CHANNEL_LEVEL_ACTION_KEEP,
  PCNT_CHANNEL_LEVEL_ACTION_INVERSE,
  PCNT_CHANNEL_LEVEL_ACTION_HOLD,
} pcnt_channel_level_action_t;

esp_err_t pcnt_new_unit(const pcnt_unit_config_t *config, pcnt_unit_handle_t *ret_unit);
esp_err_t pcnt_del_unit(pcnt_unit_handle_t unit);
esp_err_t pcnt_unit_set_glitch_filter(pcnt_unit_handle_t unit, const pcnt_glitch_filter_config_t *config);
esp_err_t pcnt_new_channel(pcnt_unit_handle_t unit, const pcnt_chan_config_t *config,
                           pcnt_channel_handle_t *ret_chan);
esp_err_t pcnt_del_channel(pcnt_channel_handle_t chan);
esp_err_t pcnt_channel_set_edge_action(pcnt_channel_handle_t chan, pcnt_channel_edge_action_t pos,
                                       pcnt_channel_edge_action_t neg);
esp_err_t pcnt_channel_set_level_action(pcnt_channel_handle_t chan, pcnt_channel_level_action_t high,
                                        pcnt_channel_level_action_t low);
esp_err_t pcnt_unit_add_watch_point(pcnt_unit_handle_t unit, int watch_point);
esp_err_t pcnt_unit_enable(pcnt_unit_handle_t unit);
esp_err_t pcnt_unit_disable(pcnt_unit_handle_t unit);
esp_err_t pcnt_unit_start(pcnt_unit_handle_t unit);
esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t unit);
esp_err_t pcnt_unit_get_count(pcnt_unit_handle_t unit, int *value);
```

- [ ] **Step 8: Bus, USB and peripheral fakes**

`test/sim/lib/simdeck/src/Wire.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

// Arduino-ESP32 TwoWire subset. Transfers go to SimBoard::i2cWrite / i2cRead.
class TwoWire {
 public:
  bool begin(int sda, int scl, uint32_t frequency);
  void beginTransmission(uint8_t address);
  size_t write(uint8_t b);
  uint8_t endTransmission(bool sendStop = true);
  uint8_t requestFrom(uint8_t address, uint8_t quantity);
  int available();
  int read();

  int sda = -1, scl = -1;
  uint32_t frequency = 0;

 private:
  uint8_t txAddr_ = 0;
  std::vector<uint8_t> tx_, rx_;
  size_t rxPos_ = 0;
};
extern TwoWire Wire;
```

`test/sim/lib/simdeck/src/SPI.h`:

```cpp
#pragma once
#include <stdint.h>

// Arduino SPIClass subset for display.cpp (plan 4 adds byte transfers for the bringup driver).
class SPIClass {
 public:
  void begin(int8_t sck, int8_t miso, int8_t mosi, int8_t ss);
  int8_t sck = -1, miso = -1, mosi = -1, ss = -1;
};
extern SPIClass SPI;
```

`test/sim/lib/simdeck/src/USB.h`:

```cpp
#pragma once
#include <stdint.h>
#include "sim_board.h"

typedef enum {
  ARDUINO_USB_STARTED_EVENT,
  ARDUINO_USB_STOPPED_EVENT,
  ARDUINO_USB_SUSPEND_EVENT,
  ARDUINO_USB_RESUME_EVENT,
} arduino_usb_event_t;

class ESPUSB {
 public:
  bool begin();
  void onEvent(arduino_usb_event_t event, sim_usb_handler_t handler);
};
extern ESPUSB USB;
```

`test/sim/lib/simdeck/src/USBHIDKeyboard.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sim/host.h"

// Report building copied from Arduino-ESP32 core 3.x libraries/USB/src/USBHIDKeyboard.cpp
// (press/release/pressRaw/releaseRaw/releaseAll). Every change sends a report, as the core does.
#define KEY_LEFT_CTRL 0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT 0x82
#define KEY_LEFT_GUI 0x83

class USBHIDKeyboard {
 public:
  void begin();
  size_t press(uint8_t k);
  size_t release(uint8_t k);
  size_t pressRaw(uint8_t k);
  size_t releaseRaw(uint8_t k);
  void releaseAll();

 private:
  void send();
  KbReport report_ = {0, {0, 0, 0, 0, 0, 0}};
};
```

`test/sim/lib/simdeck/src/USBHIDConsumerControl.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

// Usage IDs: Arduino-ESP32 USBHIDConsumerControl.h (HID Usage Tables, Consumer page 0x0C).
#define CONSUMER_CONTROL_SCAN_NEXT 0x00B5
#define CONSUMER_CONTROL_SCAN_PREVIOUS 0x00B6
#define CONSUMER_CONTROL_MUTE 0x00E2
#define CONSUMER_CONTROL_VOLUME_INCREMENT 0x00E9
#define CONSUMER_CONTROL_VOLUME_DECREMENT 0x00EA

class USBHIDConsumerControl {
 public:
  void begin();
  size_t press(uint16_t k);
  size_t release();
};
```

`test/sim/lib/simdeck/src/Preferences.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

// Arduino-ESP32 Preferences subset over board().nvs. Mirrors the core: begin() read-only on a
// namespace that does not exist fails (nvs_open NOT_FOUND); getBytes returns 0 if the value
// is larger than the buffer; writes fail when opened read-only.
class Preferences {
 public:
  bool begin(const char *name, bool readOnly = false);
  void end();
  bool isKey(const char *key);
  size_t getBytes(const char *key, void *buf, size_t maxLen);
  size_t putBytes(const char *key, const void *value, size_t len);

 private:
  std::string ns_;
  bool open_ = false, readOnly_ = false;
};
```

`test/sim/lib/simdeck/src/WiFi.h`:

```cpp
#pragma once
#include <stdint.h>
#include "Arduino.h"

typedef enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 } wifi_mode_t;
typedef enum { WIFI_POWER_11dBm = 44, WIFI_POWER_19_5dBm = 78 } wifi_power_t;
typedef enum {
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_DISCONNECTED = 6,
} wl_status_t;

class IPAddress {
 public:
  explicit IPAddress(const uint8_t o[4]) {
    for (int i = 0; i < 4; i++) o_[i] = o[i];
  }
  String toString() const;

 private:
  uint8_t o_[4];
};

class WiFiClass {
 public:
  bool mode(wifi_mode_t m);
  bool setTxPower(wifi_power_t p);
  bool disconnect(bool wifiOff = false);
  wl_status_t status();
  IPAddress localIP();
  int8_t RSSI();
};
extern WiFiClass WiFi;
```

`test/sim/lib/simdeck/src/WiFiManager.h`:

```cpp
#pragma once
#include <functional>
#include <string>
#include <vector>

// tzapu WiFiManager 2.0.17 subset used by wifi_portal.cpp. State in board().wifi.
// autoConnect: saved credentials work (wifi.joinOk) -> true; else the AP portal comes up if
// wifi.portalOk. Plan 3 adds form submission (submitForm) and link drops.
class WiFiManagerParameter {
 public:
  explicit WiFiManagerParameter(const char *custom);
  WiFiManagerParameter(const char *id, const char *label, const char *defaultValue, int length);
  const char *getID() const { return id_; }
  const char *getValue() const { return value_.c_str(); }
  int getValueLength() const { return length_; }
  const char *getCustomHTML() const { return custom_; }
  void setValue(const char *v, int length);

 private:
  const char *id_ = nullptr;
  const char *label_ = nullptr;
  const char *custom_ = nullptr;
  std::string value_;
  int length_ = 0;
};

class WiFiManager {
 public:
  WiFiManager();
  bool addParameter(WiFiManagerParameter *p);
  void setSaveParamsCallback(std::function<void()> f) { saveCb_ = f; }
  void setConfigPortalBlocking(bool b) { blocking_ = b; }
  void setConnectTimeout(unsigned long s) { connectTimeout_ = s; }
  void setShowInfoUpdate(bool b) { showInfoUpdate_ = b; }
  bool autoConnect(const char *ssid, const char *pass);
  bool getConfigPortalActive();
  void stopWebPortal();
  bool stopConfigPortal();
  bool process();
  void startWebPortal();

  // sim side
  std::vector<WiFiManagerParameter *> params;
  std::function<void()> saveCb_;
  bool blocking_ = true, showInfoUpdate_ = true;
  unsigned long connectTimeout_ = 0;
};
```

`test/sim/lib/simdeck/src/ESPmDNS.h`:

```cpp
#pragma once
class MDNSResponder {
 public:
  bool begin(const char *hostName);
  void end();
};
extern MDNSResponder MDNS;
```

`test/sim/lib/simdeck/src/Adafruit_GFX.h`:

```cpp
#pragma once
#include "Arduino.h"
// Drawing primitives are declared on Adafruit_ST7789 (Adafruit_ST7789.h) in the sim.
```

`test/sim/lib/simdeck/src/Adafruit_ST7789.h`:

```cpp
#pragma once
#include <stdint.h>
#include "Adafruit_GFX.h"
#include "SPI.h"

// Adafruit ST7789 1.11.0 API subset used by display.cpp; records into board().lcd.
// Colors: Adafruit_ST77xx.h (RGB565).
#define ST77XX_BLACK 0x0000
#define ST77XX_WHITE 0xFFFF
#define ST77XX_RED 0xF800
#define ST77XX_CYAN 0x07FF
#define ST77XX_YELLOW 0xFFE0

class Adafruit_ST7789 {
 public:
  Adafruit_ST7789(SPIClass *spi, int8_t cs, int8_t dc, int8_t rst);
  void init(uint16_t width, uint16_t height, uint8_t spiMode = 0);
  void setSPISpeed(uint32_t hz);
  void setRotation(uint8_t r);
  void setTextWrap(bool w);
  void enableSleep(bool on);
  void fillScreen(uint16_t c);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c);
  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c);
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c);
  void drawCircleHelper(int16_t x, int16_t y, int16_t r, uint8_t corner, uint16_t c);
  void setTextSize(uint8_t s) { textSize_ = s; }
  void setTextColor(uint16_t fg, uint16_t bg) { fg_ = fg; bg_ = bg; }
  void setCursor(int16_t x, int16_t y) { cx_ = x; cy_ = y; }
  size_t print(const char *s);

 private:
  uint8_t textSize_ = 1;
  uint16_t fg_ = 0xFFFF, bg_ = 0;
  int16_t cx_ = 0, cy_ = 0;
};
```

`test/sim/lib/simdeck/src/Adafruit_NeoPixel.h`:

```cpp
#pragma once
#include <stdint.h>
#include "Arduino.h"

// Adafruit NeoPixel 1.15.5 API subset; frame lands in board().leds on show().
typedef uint16_t neoPixelType;
#define NEO_GRB ((1 << 6) | (1 << 4) | (0 << 2) | (2))
#define NEO_KHZ800 0x0000

class Adafruit_NeoPixel {
 public:
  Adafruit_NeoPixel(uint16_t n, int16_t pin, neoPixelType type);
  void begin();
  void setBrightness(uint8_t b);
  void clear();
  void show();
  void setPixelColor(uint16_t i, uint8_t r, uint8_t g, uint8_t b);
};
```

- [ ] **Step 9: Fake bodies**

`test/sim/lib/simdeck/src/fakes.cpp`:

```cpp
// Bodies of the host fakes. Each forwards to a model in sim_board.h.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "Adafruit_NeoPixel.h"
#include "Adafruit_ST7789.h"
#include "Arduino.h"
#include "ESPmDNS.h"
#include "Preferences.h"
#include "SPI.h"
#include "USB.h"
#include "USBHIDConsumerControl.h"
#include "USBHIDKeyboard.h"
#include "WiFi.h"
#include "WiFiManager.h"
#include "Wire.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_random.h"
#include "sim_board.h"

HWCDC Serial;
TwoWire Wire;
SPIClass SPI;
ESPUSB USB;
WiFiClass WiFi;
MDNSResponder MDNS;

// --- time, GPIO, LEDC ---

uint32_t millis() { return board().clock.millis(); }
uint32_t micros() { return board().clock.micros(); }
void delay(uint32_t ms) { board().clock.advanceMs(ms); }
void delayMicroseconds(uint32_t us) { board().clock.advanceUs(us); }

void pinMode(uint8_t pin, uint8_t mode) {
  if (pin >= SimGpio::kPins) return;
  SimGpio &g = board().gpio;
  g.mode[pin] = mode == OUTPUT ? SimGpio::Out : (mode == INPUT_PULLUP ? SimGpio::InPullup : SimGpio::In);
}
int digitalRead(uint8_t pin) { return board().gpio.read(pin); }
void digitalWrite(uint8_t pin, uint8_t val) {
  if (pin < SimGpio::kPins) board().gpio.out[pin] = val != LOW;
}
bool ledcAttach(uint8_t pin, uint32_t, uint8_t) { return pin < SimGpio::kPins; }
bool ledcWrite(uint8_t pin, uint32_t duty) {
  if (pin >= SimGpio::kPins) return false;
  board().ledcDuty[pin] = duty;
  return true;
}

esp_err_t gpio_set_drive_capability(gpio_num_t pin, gpio_drive_cap_t cap) {
  if (pin < 0 || pin >= SimGpio::kPins) return ESP_FAIL;
  board().driveCap[pin] = cap;
  return ESP_OK;
}
esp_err_t gpio_pullup_en(gpio_num_t pin) {
  if (pin < 0 || pin >= SimGpio::kPins) return ESP_FAIL;
  board().gpio.pullup[pin] = true;
  return ESP_OK;
}

esp_reset_reason_t esp_reset_reason() { return board().resetReason; }

uint32_t esp_random() {
  uint32_t &x = board().rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

// --- USB CDC serial ---

void HWCDC::begin(uint32_t baud) { board().serial.baud = baud; }
void HWCDC::setTxTimeoutMs(uint32_t) {}
int HWCDC::available() { return static_cast<int>(board().serial.rx.size()); }
int HWCDC::read() {
  SimSerialPort &s = board().serial;
  if (s.rx.empty()) return -1;
  int b = s.rx.front();
  s.rx.pop_front();
  return b;
}
size_t HWCDC::write(uint8_t b) {
  board().serial.tx.push_back(b);
  return 1;
}
size_t HWCDC::write(const uint8_t *buf, size_t n) {
  board().serial.tx.insert(board().serial.tx.end(), buf, buf + n);
  return n;
}
size_t HWCDC::print(const char *s) { return write(reinterpret_cast<const uint8_t *>(s), strlen(s)); }
size_t HWCDC::println(const char *s) { return print(s) + print("\r\n"); }
size_t HWCDC::printf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n < 0) return 0;
  return print(buf);
}

// --- I2C ---

bool TwoWire::begin(int sdaPin, int sclPin, uint32_t freq) {
  sda = sdaPin;
  scl = sclPin;
  frequency = freq;
  return true;
}
void TwoWire::beginTransmission(uint8_t address) {
  txAddr_ = address;
  tx_.clear();
}
size_t TwoWire::write(uint8_t b) {
  tx_.push_back(b);
  return 1;
}
uint8_t TwoWire::endTransmission(bool) { return board().i2cWrite(txAddr_, tx_.data(), tx_.size()); }
uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity) {
  rx_.assign(quantity, 0);
  rxPos_ = 0;
  size_t n = board().i2cRead(address, rx_.data(), quantity);
  rx_.resize(n);
  return static_cast<uint8_t>(n);
}
int TwoWire::available() { return static_cast<int>(rx_.size() - rxPos_); }
int TwoWire::read() { return rxPos_ < rx_.size() ? rx_[rxPos_++] : -1; }

void SPIClass::begin(int8_t sckPin, int8_t misoPin, int8_t mosiPin, int8_t ssPin) {
  sck = sckPin;
  miso = misoPin;
  mosi = mosiPin;
  ss = ssPin;
}

// --- USB device ---

bool ESPUSB::begin() { return true; }
void ESPUSB::onEvent(arduino_usb_event_t event, sim_usb_handler_t handler) {
  if (event == ARDUINO_USB_STOPPED_EVENT) board().usbStoppedHandler = handler;
}

// US-layout ASCII -> HID usage, from the core's _asciimap: letters, digits, space, enter.
// 0x80 flag = needs shift. Other characters map to 0 (not sent), as in the core for unmapped.
static uint8_t asciiUsage(uint8_t c) {
  if (c >= 'a' && c <= 'z') return static_cast<uint8_t>(0x04 + (c - 'a'));
  if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(0x80 | (0x04 + (c - 'A')));
  if (c >= '1' && c <= '9') return static_cast<uint8_t>(0x1E + (c - '1'));
  if (c == '0') return 0x27;
  if (c == ' ') return 0x2C;
  if (c == '\n') return 0x28;
  return 0;
}

void USBHIDKeyboard::begin() {}
void USBHIDKeyboard::send() { board().host.kb.push_back(report_); }

size_t USBHIDKeyboard::pressRaw(uint8_t k) {
  if (k >= 0xE0 && k < 0xE8) {
    report_.mods |= static_cast<uint8_t>(1u << (k - 0xE0));
  } else if (k && k < 0xA5) {
    bool present = false;
    for (uint8_t s : report_.keys)
      if (s == k) present = true;
    if (!present) {
      int i = 0;
      for (; i < 6; i++)
        if (report_.keys[i] == 0) {
          report_.keys[i] = k;
          break;
        }
      if (i == 6) return 0;
    }
  } else if (report_.mods == 0) {
    return 0;
  }
  send();
  return 1;
}

size_t USBHIDKeyboard::releaseRaw(uint8_t k) {
  if (k >= 0xE0 && k < 0xE8) {
    report_.mods &= static_cast<uint8_t>(~(1u << (k - 0xE0)));
  } else if (k && k < 0xA5) {
    for (uint8_t &s : report_.keys)
      if (s == k) s = 0;
  } else {
    return 0;
  }
  send();
  return 1;
}

size_t USBHIDKeyboard::press(uint8_t k) {
  if (k >= 0x88) {
    k = static_cast<uint8_t>(k - 0x88);
  } else if (k >= 0x80) {
    report_.mods |= static_cast<uint8_t>(1u << (k - 0x80));
    k = 0;
  } else {
    k = asciiUsage(k);
    if (!k) return 0;
    if (k & 0x80) {
      report_.mods |= 0x02;
      k &= 0x7F;
    }
  }
  return pressRaw(k);
}

size_t USBHIDKeyboard::release(uint8_t k) {
  if (k >= 0x88) {
    k = static_cast<uint8_t>(k - 0x88);
  } else if (k >= 0x80) {
    report_.mods &= static_cast<uint8_t>(~(1u << (k - 0x80)));
    k = 0;
  } else {
    k = asciiUsage(k);
    if (!k) return 0;
    if (k & 0x80) {
      report_.mods &= static_cast<uint8_t>(~0x02);
      k &= 0x7F;
    }
  }
  return releaseRaw(k);
}

void USBHIDKeyboard::releaseAll() {
  report_ = KbReport{0, {0, 0, 0, 0, 0, 0}};
  send();
}

void USBHIDConsumerControl::begin() {}
size_t USBHIDConsumerControl::press(uint16_t k) {
  board().host.consumer.push_back(k);
  return 1;
}
size_t USBHIDConsumerControl::release() {
  board().host.consumer.push_back(0);
  return 1;
}

// --- NVS ---

bool Preferences::begin(const char *name, bool readOnly) {
  if (readOnly && !board().nvs.hasNamespace(name)) return false;
  ns_ = name;
  readOnly_ = readOnly;
  open_ = true;
  if (!readOnly) board().nvs.ns[ns_];  // creating the namespace, as nvs_open(READWRITE) does
  return true;
}
void Preferences::end() { open_ = false; }
bool Preferences::isKey(const char *key) {
  if (!open_) return false;
  return board().nvs.ns[ns_].count(key) != 0;
}
size_t Preferences::getBytes(const char *key, void *buf, size_t maxLen) {
  if (!open_) return 0;
  auto &m = board().nvs.ns[ns_];
  auto it = m.find(key);
  if (it == m.end() || it->second.size() > maxLen) return 0;
  memcpy(buf, it->second.data(), it->second.size());
  return it->second.size();
}
size_t Preferences::putBytes(const char *key, const void *value, size_t len) {
  if (!open_ || readOnly_) return 0;
  const uint8_t *p = static_cast<const uint8_t *>(value);
  board().nvs.ns[ns_][key].assign(p, p + len);
  return len;
}

// --- Wi-Fi ---

String IPAddress::toString() const {
  char b[16];
  snprintf(b, sizeof b, "%u.%u.%u.%u", o_[0], o_[1], o_[2], o_[3]);
  return String(b);
}
bool WiFiClass::mode(wifi_mode_t m) {
  board().wifi.mode = m;
  return true;
}
bool WiFiClass::setTxPower(wifi_power_t p) {
  board().wifi.txPower = p;
  return true;
}
bool WiFiClass::disconnect(bool) {
  board().wifi.connected = false;
  return true;
}
wl_status_t WiFiClass::status() { return board().wifi.connected ? WL_CONNECTED : WL_DISCONNECTED; }
IPAddress WiFiClass::localIP() { return IPAddress(board().wifi.ip); }
int8_t WiFiClass::RSSI() { return board().wifi.rssi; }

WiFiManagerParameter::WiFiManagerParameter(const char *custom) : custom_(custom) {}
WiFiManagerParameter::WiFiManagerParameter(const char *id, const char *label, const char *defaultValue,
                                           int length)
    : id_(id), label_(label) {
  setValue(defaultValue, length);
}
void WiFiManagerParameter::setValue(const char *v, int length) {
  if (!id_) return;  // custom HTML parameters have no value (as in the library)
  length_ = length;
  value_.assign(v ? v : "");
  if (static_cast<int>(value_.size()) > length) value_.resize(static_cast<size_t>(length));
}

WiFiManager::WiFiManager() { board().wifi.manager = this; }
bool WiFiManager::addParameter(WiFiManagerParameter *p) {
  params.push_back(p);
  return true;
}
bool WiFiManager::autoConnect(const char *ssid, const char *pass) {
  SimWifi &w = board().wifi;
  w.apSsid = ssid;
  w.apPass = pass;
  if (w.joinOk) {
    w.connected = true;
    return true;
  }
  w.portalActive = w.portalOk;
  return false;
}
bool WiFiManager::getConfigPortalActive() { return board().wifi.portalActive; }
void WiFiManager::stopWebPortal() { board().wifi.webPortal = false; }
bool WiFiManager::stopConfigPortal() {
  board().wifi.portalActive = false;
  return true;
}
bool WiFiManager::process() { return true; }
void WiFiManager::startWebPortal() { board().wifi.webPortal = true; }

bool MDNSResponder::begin(const char *hostName) {
  board().wifi.mdnsName = hostName;
  return true;
}
void MDNSResponder::end() { board().wifi.mdnsName.clear(); }

// --- LCD ---

Adafruit_ST7789::Adafruit_ST7789(SPIClass *, int8_t, int8_t, int8_t) {}
void Adafruit_ST7789::init(uint16_t, uint16_t, uint8_t) { board().lcd.inited = true; }
void Adafruit_ST7789::setSPISpeed(uint32_t hz) { board().lcd.spiHz = hz; }
void Adafruit_ST7789::setRotation(uint8_t r) { board().lcd.rotation = r; }
void Adafruit_ST7789::setTextWrap(bool) {}
void Adafruit_ST7789::enableSleep(bool on) { board().lcd.asleep = on; }
void Adafruit_ST7789::fillScreen(uint16_t) { board().lcd.fills++; }
void Adafruit_ST7789::fillRect(int16_t, int16_t, int16_t, int16_t, uint16_t) { board().lcd.fills++; }
void Adafruit_ST7789::drawRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
void Adafruit_ST7789::fillRoundRect(int16_t, int16_t, int16_t, int16_t, int16_t, uint16_t) {}
void Adafruit_ST7789::fillTriangle(int16_t, int16_t, int16_t, int16_t, int16_t, int16_t, uint16_t) {}
void Adafruit_ST7789::drawLine(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
void Adafruit_ST7789::drawCircleHelper(int16_t, int16_t, int16_t, uint8_t, uint16_t) {}
size_t Adafruit_ST7789::print(const char *s) {
  board().lcd.texts.push_back({cx_, cy_, textSize_, fg_, s});
  return strlen(s);
}

// --- LEDs ---

Adafruit_NeoPixel::Adafruit_NeoPixel(uint16_t n, int16_t pin, neoPixelType) {
  SimLeds &l = board().leds;
  l.count = n;
  l.pin = pin;
  l.pending.assign(n, SimRgb{0, 0, 0});
}
void Adafruit_NeoPixel::begin() {}
void Adafruit_NeoPixel::setBrightness(uint8_t b) { board().leds.brightness = b; }
void Adafruit_NeoPixel::clear() {
  SimLeds &l = board().leds;
  l.pending.assign(l.count, SimRgb{0, 0, 0});
}
void Adafruit_NeoPixel::show() {
  SimLeds &l = board().leds;
  l.shown = l.pending;
  l.shows++;
}
void Adafruit_NeoPixel::setPixelColor(uint16_t i, uint8_t r, uint8_t g, uint8_t b) {
  SimLeds &l = board().leds;
  if (i < l.pending.size()) l.pending[i] = SimRgb{r, g, b};
}

// --- PCNT (configuration only in plan 1) ---

struct sim_pcnt_unit_t {
  int index;
};
struct sim_pcnt_chan_t {
  int unit, index;
};
static sim_pcnt_unit_t gUnits[SimPcnt::kUnits] = {{0}, {1}, {2}, {3}};
static sim_pcnt_chan_t gChans[SimPcnt::kUnits][2] = {{{0, 0}, {0, 1}}, {{1, 0}, {1, 1}},
                                                     {{2, 0}, {2, 1}}, {{3, 0}, {3, 1}}};

static SimPcntUnit &unitOf(pcnt_unit_handle_t u) { return board().pcnt.unit[u->index]; }
static SimPcntChannel &chanOf(pcnt_channel_handle_t c) { return board().pcnt.unit[c->unit].ch[c->index]; }

esp_err_t pcnt_new_unit(const pcnt_unit_config_t *config, pcnt_unit_handle_t *ret_unit) {
  SimPcnt &p = board().pcnt;
  if (p.failNewUnit) return ESP_FAIL;
  for (int i = 0; i < SimPcnt::kUnits; i++) {
    if (p.unit[i].used) continue;
    p.unit[i] = SimPcntUnit{};
    p.unit[i].used = true;
    p.unit[i].low = config->low_limit;
    p.unit[i].high = config->high_limit;
    *ret_unit = &gUnits[i];
    return ESP_OK;
  }
  return ESP_FAIL;
}
esp_err_t pcnt_del_unit(pcnt_unit_handle_t unit) {
  if (!unit) return ESP_FAIL;
  unitOf(unit).used = false;
  return ESP_OK;
}
esp_err_t pcnt_unit_set_glitch_filter(pcnt_unit_handle_t unit, const pcnt_glitch_filter_config_t *config) {
  unitOf(unit).glitchNs = config->max_glitch_ns;
  return ESP_OK;
}
esp_err_t pcnt_new_channel(pcnt_unit_handle_t unit, const pcnt_chan_config_t *config,
                           pcnt_channel_handle_t *ret_chan) {
  SimPcntUnit &u = unitOf(unit);
  if (u.channels >= 2) return ESP_FAIL;
  int i = u.channels++;
  u.ch[i].edgePin = config->edge_gpio_num;
  u.ch[i].levelPin = config->level_gpio_num;
  *ret_chan = &gChans[unit->index][i];
  return ESP_OK;
}
esp_err_t pcnt_del_channel(pcnt_channel_handle_t) { return ESP_OK; }
esp_err_t pcnt_channel_set_edge_action(pcnt_channel_handle_t chan, pcnt_channel_edge_action_t pos,
                                       pcnt_channel_edge_action_t neg) {
  chanOf(chan).posEdge = pos;
  chanOf(chan).negEdge = neg;
  return ESP_OK;
}
esp_err_t pcnt_channel_set_level_action(pcnt_channel_handle_t chan, pcnt_channel_level_action_t high,
                                        pcnt_channel_level_action_t low) {
  chanOf(chan).highLevel = high;
  chanOf(chan).lowLevel = low;
  return ESP_OK;
}
esp_err_t pcnt_unit_add_watch_point(pcnt_unit_handle_t, int) { return ESP_OK; }
esp_err_t pcnt_unit_enable(pcnt_unit_handle_t unit) {
  if (!unit) return ESP_FAIL;
  unitOf(unit).enabled = true;
  return ESP_OK;
}
esp_err_t pcnt_unit_disable(pcnt_unit_handle_t unit) {
  if (!unit) return ESP_FAIL;
  unitOf(unit).enabled = false;
  return ESP_OK;
}
esp_err_t pcnt_unit_start(pcnt_unit_handle_t unit) {
  unitOf(unit).started = true;
  return ESP_OK;
}
esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t unit) {
  unitOf(unit).count = 0;
  return ESP_OK;
}
esp_err_t pcnt_unit_get_count(pcnt_unit_handle_t unit, int *value) {
  *value = unitOf(unit).count;
  return ESP_OK;
}
```

- [ ] **Step 10: Fork harness**

`test/sim/lib/simdeck/src/sim_test.h`:

```cpp
#pragma once
#include <unity.h>

// Runs body in a fork()ed child: the firmware's statics start fresh, like a power-on.
// The child's Unity output is captured through a pipe; a failing assertion or a crash in the
// child becomes a FAIL of the test in the parent, with the child's message.
void simRunForked(void (*body)());

#define SIM_TEST(name)                     \
  static void name##_body();               \
  void name() { simRunForked(name##_body); } \
  static void name##_body()
```

`test/sim/lib/simdeck/src/sim_test.cpp`:

```cpp
#include "sim_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>

void simRunForked(void (*body)()) {
  int fds[2];
  if (pipe(fds) != 0) TEST_FAIL_MESSAGE("pipe() failed");
  fflush(stdout);
  fflush(stderr);
  pid_t pid = fork();
  if (pid < 0) TEST_FAIL_MESSAGE("fork() failed");
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDOUT_FILENO);
    if (TEST_PROTECT()) body();
    fflush(stdout);
    exit(Unity.CurrentTestFailed ? 1 : 0);  // exit(), not _exit(): gcov writes counters at exit
  }
  close(fds[1]);
  std::string out;
  char buf[512];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof buf)) > 0) out.append(buf, static_cast<size_t>(n));
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFSIGNALED(status)) {
    char m[64];
    snprintf(m, sizeof m, "device process crashed: signal %d", WTERMSIG(status));
    TEST_FAIL_MESSAGE(m);
  }
  if (WEXITSTATUS(status) != 0) {
    size_t at = out.find("FAIL");
    std::string msg = at == std::string::npos ? out : out.substr(at);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
    if (msg.empty()) msg = "device process failed";
    TEST_FAIL_MESSAGE(msg.c_str());
  }
}
```

- [ ] **Step 11: SimDeck facade**

`test/sim/lib/simdeck/src/sim_deck.h`:

```cpp
#pragma once
#include <stdint.h>
#include "sim_board.h"

void setup();  // src/main.cpp
void loop();

struct SimFaults {
  void mcpAbsent(bool absent) { board().mcp.present = !absent; }
};

// Test-facing view of the simulated deck. Key positions are physical (1..12, wiring.md).
class SimDeck {
 public:
  void boot() { setup(); }
  // One loop() per simulated millisecond (timing logic only compares millis() values).
  void run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
      loop();
      board().clock.advanceMs(1);
    }
  }
  void press(uint8_t pos) { board().mcp.extLow |= static_cast<uint16_t>(1u << mcpBitForKey(pos)); }
  void release(uint8_t pos) { board().mcp.extLow &= static_cast<uint16_t>(~(1u << mcpBitForKey(pos))); }
  void knobPress(uint8_t n) { board().mcp.extLow |= static_cast<uint16_t>(1u << knobBit(n)); }
  void knobRelease(uint8_t n) { board().mcp.extLow &= static_cast<uint16_t>(~(1u << knobBit(n))); }

  SimHost &host() { return board().host; }
  SimMcp23017 &mcp() { return board().mcp; }
  SimFaults fault() { return SimFaults{}; }

  // Writes a presets/*.json file into NVS the way the firmware stores a confirmed key map.
  bool loadPresetIntoNvs(const char *path);

  // MCP pin bit (GPA0..7 = 0..7, GPB0..7 = 8..15) per physical key, from wiring.md
  // (independent of the firmware's own table in lib/deck_core/src/mcp23017.cpp).
  static uint8_t mcpBitForKey(uint8_t pos);
  static uint8_t knobBit(uint8_t n) { return n == 1 ? 12 : 13; }  // GPB4 / GPB5
};
```

`test/sim/lib/simdeck/src/sim_deck.cpp`:

```cpp
#include "sim_deck.h"
#include <stdio.h>
#include <string>
#include "keymap.h"

uint8_t SimDeck::mcpBitForKey(uint8_t pos) {
  // wiring.md "MCP23017": positions 1-4 = GPB0-GPB3, 5/6/7 = GPA2/GPA1/GPA0, 8 = GPB6,
  // 9/10/11/12 = GPA3/GPA6/GPA5/GPA4 (bench-verified 2026-10-20).
  static const uint8_t kBit[13] = {0, 8, 9, 10, 11, 2, 1, 0, 14, 3, 6, 5, 4};
  return pos >= 1 && pos <= 12 ? kBit[pos] : 15;  // 15 = GPB7, never a key
}

// Pulls the next quoted string after `field` out of the preset file. The preset format is
// ours (presets/*.json: one object per key with "type" and "value"), so a scanner is enough.
static bool nextField(const std::string &s, size_t &pos, const char *field, std::string &out) {
  std::string key = std::string("\"") + field + "\"";
  size_t k = s.find(key, pos);
  if (k == std::string::npos) return false;
  size_t q1 = s.find('"', s.find(':', k) + 1);
  size_t q2 = s.find('"', q1 + 1);
  if (q1 == std::string::npos || q2 == std::string::npos) return false;
  out = s.substr(q1 + 1, q2 - q1 - 1);
  pos = q2 + 1;
  return true;
}

bool SimDeck::loadPresetIntoNvs(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  std::string s;
  char buf[1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);

  KeyMap m;
  size_t pos = s.find("\"keys\"");
  for (uint8_t i = 0; i < kKeyCount; i++) {
    std::string type, value;
    if (!nextField(s, pos, "type", type) || !nextField(s, pos, "value", value)) return false;
    if (m.setFromStrings(i, type.c_str(), value.c_str()) != KeyMapError::None) return false;
  }
  uint8_t blob[kKeyMapBlobMax];
  size_t len = serializeKeyMap(m, blob, sizeof blob);
  if (len == 0) return false;
  board().nvs.ns["saucedeck"]["keymap"].assign(blob, blob + len);  // src/keymap_store.cpp names
  return true;
}
```

- [ ] **Step 12: Run the tests**

Run: `~/.platformio/penv/bin/pio test -e sim 2>&1 | grep -E "error|warning|PASS|FAIL|test cases" | head -30`
Expected: `3 test cases: 3 succeeded`.

Troubleshooting, in order:
- `-Werror` hit in **`src/`** compiled for the host: read the warning. If it is a real defect (e.g. gcc `-Wformat-truncation`), fix it in `src/` properly and note it in the commit. Never add `#ifdef` or pragmas to `src/`.
- `-Werror` hit in **the fakes**: fix the fake.
- `SIM_PROJECT_DIR` undefined or not expanded: try `'-DSIM_PROJECT_DIR="${PROJECT_DIR}"'`; check with `pio test -e sim -vvv | grep SIM_PROJECT_DIR`.
- LDF does not find `simdeck`: check `lib_extra_dirs` path and that `library.json` is valid JSON.
- `unity.h` not found while compiling `simdeck/src/sim_test.cpp`: the Unity package is not on the
  library's include path. Add `lib_ldf_mode = deep+` to `[env:sim]`; if still missing, add
  `"dependencies": [{"name": "Unity"}]` to `library.json`. Do not copy Unity into the repo.
- `keymap.h` not found while compiling `simdeck/src/sim_deck.cpp`: same cause for `deck_core`;
  add `deck_core` to `lib_deps` in `[env:sim]`.

- [ ] **Step 13: Prove the harness catches what it should (then revert)**

a) In `simRunForked`, temporarily replace the `fork()` branch so the body runs in-process (`body(); return;` at the top). Run `pio test -e sim -f native/test_sim_keys`: `test_harness_b_sees_fresh_board` must FAIL. Revert.
b) Temporarily add `TEST_ASSERT_EQUAL(1, 2);` to `test_boot_configures_mcp_inputs_pullups_and_output_only_pins`. Run: it must FAIL with a message containing `Expected 1 Was 2`. Revert.
c) Temporarily add `*(volatile int *)nullptr = 0;` at the start of the same test. Run: FAIL with `device process crashed: signal 11` (Linux) or `signal 10/11` (macOS). Revert.

Run: `git diff --stat` — only the new files and `platformio.ini` changed.

- [ ] **Step 14: Firmware builds untouched**

Run: `~/.platformio/penv/bin/pio test -e native 2>&1 | grep "test cases"; ~/.platformio/penv/bin/pio run -e saucedeck -e bringup 2>&1 | grep -E "warning|SUCCESS|FAILED" | tail -3`
Expected: 198 succeeded; both envs SUCCESS, no warnings.

- [ ] **Step 15: Commit**

```bash
git add platformio.ini test/sim test/native/test_sim_keys
git commit -m "Device sim: fakes + SimBoard + fork-per-test harness; firmware boots on host"
```

---

### Task 3: Keys end to end with the default map

**Files:**
- Modify: `test/native/test_sim_keys/test_main.cpp`

**Interfaces:**
- Consumes: `SimDeck::{boot, run, press, release}`, `SimHost::{keyDown, allKeysUp, kb}` (Task 2).

- [ ] **Step 1: Write the tests**

Add before `int main`:

```cpp
// --- keys, factory default map (F13..F24) ---

static constexpr uint32_t kSettle = 30;  // > 25 ms debounce (lib/deck_core/src/debounce.h)

SIM_TEST(test_default_map_each_key_sends_its_f_key) {
  SimDeck sim;
  sim.boot();
  for (uint8_t pos = 1; pos <= 12; pos++) {
    char msg[24];
    snprintf(msg, sizeof msg, "key %u", pos);
    sim.press(pos);
    sim.run(kSettle);
    TEST_ASSERT_TRUE_MESSAGE(sim.host().keyDown(static_cast<uint8_t>(0x68 + pos - 1)), msg);
    sim.release(pos);
    sim.run(kSettle);
    TEST_ASSERT_TRUE_MESSAGE(sim.host().allKeysUp(), msg);
  }
}

SIM_TEST(test_bounce_shorter_than_debounce_sends_nothing) {
  SimDeck sim;
  sim.boot();
  sim.press(3);
  sim.run(10);
  sim.release(3);
  sim.run(kSettle);
  TEST_ASSERT_TRUE(sim.host().kb.empty());
}

SIM_TEST(test_two_keys_held_then_one_released) {
  SimDeck sim;
  sim.boot();
  sim.press(1);
  sim.press(2);
  sim.run(kSettle);
  TEST_ASSERT_TRUE(sim.host().keyDown(0x68));
  TEST_ASSERT_TRUE(sim.host().keyDown(0x69));
  sim.release(1);
  sim.run(kSettle);
  TEST_ASSERT_FALSE(sim.host().keyDown(0x68));
  TEST_ASSERT_TRUE(sim.host().keyDown(0x69));
}
```

Add to `main`:

```cpp
  RUN_TEST(test_default_map_each_key_sends_its_f_key);
  RUN_TEST(test_bounce_shorter_than_debounce_sends_nothing);
  RUN_TEST(test_two_keys_held_then_one_released);
```

- [ ] **Step 2: Run**

Run: `~/.platformio/penv/bin/pio test -e sim 2>&1 | grep -E "PASS|FAIL|test cases"`
Expected: `6 test cases: 6 succeeded`.

If `test_default_map_each_key_sends_its_f_key` fails for one position, compare `SimDeck::mcpBitForKey` (wiring.md) with `kRawBit` in `lib/deck_core/src/mcp23017.cpp`. A mismatch is a real finding: stop and ask which one is right; do not edit either table to make the test pass.

- [ ] **Step 3: Mutation spot-check (then revert)**

In `lib/deck_core/src/mcp23017.cpp`, swap the first two entries of `kRawBit` (`8, 9` → `9, 8`). Run `pio test -e sim`: `test_default_map_each_key_sends_its_f_key` must FAIL with `key 1`. Revert with `git checkout lib/deck_core/src/mcp23017.cpp`.

- [ ] **Step 4: Commit**

```bash
git add test/native/test_sim_keys/test_main.cpp
git commit -m "Sim: 12 keys end to end with the default map, debounce, two-key hold"
```

---

### Task 4: Preset key map from NVS, modifier order

**Files:**
- Modify: `test/native/test_sim_keys/test_main.cpp`

**Interfaces:**
- Consumes: `SimDeck::loadPresetIntoNvs(path)`, `SIM_PROJECT_DIR`, `KbReport`.

- [ ] **Step 1: Write the tests**

```cpp
// --- keys, presets/mac-daily.json loaded from NVS ---

static void bootWithMacDaily(SimDeck &sim) {
  TEST_ASSERT_TRUE(sim.loadPresetIntoNvs(SIM_PROJECT_DIR "/presets/mac-daily.json"));
  sim.boot();
}

SIM_TEST(test_mac_daily_keys_1_to_8_send_f13_to_f20) {
  SimDeck sim;
  bootWithMacDaily(sim);
  for (uint8_t pos = 1; pos <= 8; pos++) {
    sim.press(pos);
    sim.run(kSettle);
    TEST_ASSERT_TRUE(sim.host().keyDown(static_cast<uint8_t>(0x68 + pos - 1)));
    TEST_ASSERT_EQUAL_HEX8(0, sim.host().mods());
    sim.release(pos);
    sim.run(kSettle);
  }
}

SIM_TEST(test_mac_daily_key_9_sends_cmd_h) {
  SimDeck sim;
  bootWithMacDaily(sim);
  sim.press(9);
  sim.run(kSettle);
  TEST_ASSERT_EQUAL_HEX8(0x08, sim.host().mods());  // left GUI = bit 3
  TEST_ASSERT_TRUE(sim.host().keyDown(0x0B));       // H
}

SIM_TEST(test_mac_daily_keys_10_to_12_send_ctrl_alt_cmd_f13_to_f15) {
  SimDeck sim;
  bootWithMacDaily(sim);
  for (uint8_t pos = 10; pos <= 12; pos++) {
    sim.press(pos);
    sim.run(kSettle);
    TEST_ASSERT_EQUAL_HEX8(0x0D, sim.host().mods());  // ctrl bit0 | alt bit2 | GUI bit3
    TEST_ASSERT_TRUE(sim.host().keyDown(static_cast<uint8_t>(0x68 + pos - 10)));
    sim.release(pos);
    sim.run(kSettle);
    TEST_ASSERT_TRUE(sim.host().allKeysUp());
  }
}

// The Mac must see the modifier before the key and the key gone before the modifier,
// or a chord can arrive as a bare key.
SIM_TEST(test_chord_modifier_before_key_and_after_key_on_release) {
  SimDeck sim;
  bootWithMacDaily(sim);
  size_t at = sim.host().kb.size();
  sim.press(9);
  sim.run(kSettle);
  sim.release(9);
  sim.run(kSettle);
  const std::vector<KbReport> &r = sim.host().kb;
  TEST_ASSERT_EQUAL(at + 4, r.size());
  TEST_ASSERT_EQUAL_HEX8(0x08, r[at].mods);
  TEST_ASSERT_EQUAL_HEX8(0x00, r[at].keys[0]);
  TEST_ASSERT_EQUAL_HEX8(0x08, r[at + 1].mods);
  TEST_ASSERT_EQUAL_HEX8(0x0B, r[at + 1].keys[0]);
  TEST_ASSERT_EQUAL_HEX8(0x08, r[at + 2].mods);
  TEST_ASSERT_EQUAL_HEX8(0x00, r[at + 2].keys[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, r[at + 3].mods);
}
```

Add to `main`:

```cpp
  RUN_TEST(test_mac_daily_keys_1_to_8_send_f13_to_f20);
  RUN_TEST(test_mac_daily_key_9_sends_cmd_h);
  RUN_TEST(test_mac_daily_keys_10_to_12_send_ctrl_alt_cmd_f13_to_f15);
  RUN_TEST(test_chord_modifier_before_key_and_after_key_on_release);
```

- [ ] **Step 2: Run**

Run: `~/.platformio/penv/bin/pio test -e sim 2>&1 | grep -E "PASS|FAIL|test cases"`
Expected: `10 test cases: 10 succeeded`.

- [ ] **Step 3: Mutation spot-check (then revert)**

In `src/main.cpp`, `case ActionType::KeyDown`: move `Keyboard.pressRaw(k.usage);` above the modifier loop. Run: `test_chord_modifier_before_key_and_after_key_on_release` must FAIL. Revert with `git checkout src/main.cpp`.

- [ ] **Step 4: Commit**

```bash
git add test/native/test_sim_keys/test_main.cpp
git commit -m "Sim: mac-daily preset from NVS, chord modifier order as the Mac sees it"
```

---

### Task 5: Expander faults

**Files:**
- Modify: `test/native/test_sim_keys/test_main.cpp`

**Interfaces:**
- Consumes: `SimDeck::fault().mcpAbsent(bool)`.

- [ ] **Step 1: Write the tests**

```cpp
// --- MCP23017 faults (bench 2026-10-18: bad contact on the module header) ---

SIM_TEST(test_bus_loss_while_key_held_releases_it) {
  SimDeck sim;
  sim.boot();
  sim.press(4);
  sim.run(kSettle);
  TEST_ASSERT_TRUE(sim.host().keyDown(0x6B));
  sim.fault().mcpAbsent(true);
  sim.run(25);  // > kBusLostMs = 20 (lib/deck_core/src/deck.h)
  TEST_ASSERT_TRUE(sim.host().allKeysUp());
}

SIM_TEST(test_short_bus_glitch_does_not_release_held_key) {
  SimDeck sim;
  sim.boot();
  sim.press(4);
  sim.run(kSettle);
  size_t at = sim.host().kb.size();
  sim.fault().mcpAbsent(true);
  sim.run(10);  // < kBusLostMs
  sim.fault().mcpAbsent(false);
  sim.run(kSettle);
  TEST_ASSERT_TRUE(sim.host().keyDown(0x6B));
  TEST_ASSERT_EQUAL(at, sim.host().kb.size());  // no release / re-press seen by the Mac
}

SIM_TEST(test_expander_back_after_loss_key_still_held_is_pressed_again) {
  SimDeck sim;
  sim.boot();
  sim.press(4);
  sim.run(kSettle);
  sim.fault().mcpAbsent(true);
  sim.run(25);
  sim.mcp().reset();  // power-cycled module comes back at POR state
  sim.fault().mcpAbsent(false);
  sim.run(60);
  TEST_ASSERT_EQUAL_HEX8(0x7F, sim.mcp().reg[MCP_GPPUA]);  // re-initialised by mcpBegin
  TEST_ASSERT_TRUE(sim.host().keyDown(0x6B));
}

SIM_TEST(test_expander_absent_at_boot_then_plugged) {
  SimDeck sim;
  sim.fault().mcpAbsent(true);
  sim.boot();
  sim.press(1);
  sim.run(50);
  TEST_ASSERT_FALSE(sim.host().keyDown(0x68));
  sim.fault().mcpAbsent(false);
  sim.run(60);
  TEST_ASSERT_TRUE(sim.host().keyDown(0x68));
}
```

Add to `main`:

```cpp
  RUN_TEST(test_bus_loss_while_key_held_releases_it);
  RUN_TEST(test_short_bus_glitch_does_not_release_held_key);
  RUN_TEST(test_expander_back_after_loss_key_still_held_is_pressed_again);
  RUN_TEST(test_expander_absent_at_boot_then_plugged);
```

- [ ] **Step 2: Run**

Run: `~/.platformio/penv/bin/pio test -e sim 2>&1 | grep -E "PASS|FAIL|test cases"`
Expected: `14 test cases: 14 succeeded`.

If `test_short_bus_glitch_does_not_release_held_key` or `test_expander_back_after_loss_key_still_held_is_pressed_again` fails, that is a finding about firmware behaviour, not a test bug to tune away: stop, report the observed reports (`sim.host().kb`) and ask how the deck should behave. Fixing firmware then follows TDD (this test is the failing test).

- [ ] **Step 3: Mutation spot-check (then revert)**

In `lib/deck_core/src/deck.h`, `onBusLost`: replace `out.push(ActionType::ReleaseAll);` with `out.push(ActionType::Sync);` (commenting it out leaves `out` unused and `-Werror` stops the build). Run: `test_bus_loss_while_key_held_releases_it` must FAIL. Revert with `git checkout lib/deck_core/src/deck.h`.

- [ ] **Step 4: Commit**

```bash
git add test/native/test_sim_keys/test_main.cpp
git commit -m "Sim: expander loss, glitch, hot-plug and absent-at-boot scenarios"
```

---

### Task 6: Coverage report with the sim, CI step, docs

**Files:**
- Modify: `.github/workflows/ci.yml`, `CLAUDE.md`

- [ ] **Step 1: Coverage with the sim suite**

Run: `PIO=~/.platformio/penv/bin/pio MIN_LINE=0 MIN_BRANCH=0 tools/coverage.sh 2>&1 | tail -8`
Expected: native suites + `native/test_sim_keys` run, summary printed, exit 0. Note the new line / branch numbers (they must be above the Task 1 baseline). `src/main.cpp` and `src/keymap_store.cpp` now show covered lines in `.pio/coverage/index.html`.

- [ ] **Step 2: CI runs the sim tests (gate comes in plan 4)**

In `.github/workflows/ci.yml`, after the `Unit tests (host)` step, add:

```yaml
      - name: Device sim tests (real src/ on the simulated board)
        run: pio test -e sim
```

- [ ] **Step 3: CLAUDE.md commands**

In CLAUDE.md `## Commands`, after the `pio test -e native` line, add:

```sh
pio test -e sim             # real src/ firmware on the simulated board (test/sim/lib/simdeck)
tools/coverage.sh           # coverage over deck_core + src/ (MIN_LINE=0 MIN_BRANCH=0 = report only)
```

In `## Engineering: TDD`, under `Layout:`, add:

```
- `test/sim/lib/simdeck/` — host fakes for Arduino/ESP-IDF/vendor headers + simulated board models. Each model header lists its hardware assumptions with sources.
- `test/native/test_sim_*/` — scenario tests: real `src/` against the sim. `SIM_TEST` = fresh device (fork) per test.
```

- [ ] **Step 4: Full verification**

Run:
```bash
~/.platformio/penv/bin/pio test -e native 2>&1 | grep "test cases"
~/.platformio/penv/bin/pio test -e sim 2>&1 | grep "test cases"
~/.platformio/penv/bin/pio run -e saucedeck -e bringup 2>&1 | grep -E "warning|SUCCESS|FAILED" | tail -3
```
Expected: 198 succeeded; 14 succeeded; both SUCCESS, no warnings.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/ci.yml CLAUDE.md
git commit -m "CI runs device sim tests; CLAUDE.md: sim + coverage commands (coverage now lines X%, branches Y%)"
```

---

## After this plan

Plan 2 (knobs/display/LEDs) starts from the coverage report of Task 6: it lists the uncovered lines in `src/encoders.cpp`, `src/display.cpp`, `src/leds.cpp`, `src/main.cpp`, which define its scenarios.

Measured at the end of plan 1 (2026-10-24, macOS clang): lines 76.4 % (919/1203), branches
69.6 % (585/840). Per file: wifi_portal.cpp 9 %, display.cpp 24 %, helper_link.cpp 34 %,
main.cpp 59 %, keymap_store.cpp 60 %, encoders.cpp 85 %, leds.cpp 94 %.

**Carry into plan 4:** `src/bringup/main.cpp` is absent from the report (no env compiles it with
coverage yet). gcovr only counts files some test compiled, so the gate must also check that every
`src/**/*.cpp` (except spikes) appears in the merged report, or an untested file passes silently.

