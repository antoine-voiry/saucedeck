# Device simulation + 95% coverage gate — design

Date: 2026-10-24. Status: approved in brainstorming, pending spec review.

## Goal

Test the real firmware "as if there was a device": `src/main.cpp`, `src/bringup/main.cpp` and the
rest of `src/` run on the host against a simulated SAUCEDECK board. Coverage of
`lib/deck_core/src`, `src/` and `src/bringup` must be **≥ 95 % lines and ≥ 95 % branches**,
enforced in CI.

Out of scope: `src/spike_*` (throwaway spikes), `test/`, the fakes themselves. Electrical behaviour
(signal timing, voltage levels, contact faults) stays a bench concern.

## Approach

Fake libraries + a simulated board (`SimDeck`). Rejected: per-call mocks (test call order, not
behaviour; brittle) and emulators (no reliable MCP23017 model, coverage extraction hard, slow CI).

## Architecture

```
test/sim/
  fakes/     host stand-ins for every header src/ includes:
             Arduino.h, Wire.h, SPI.h, USB.h, USBHIDKeyboard.h, USBHIDConsumerControl.h,
             Preferences.h, WiFi.h, WiFiManager.h, ESPmDNS.h, Adafruit_GFX.h,
             Adafruit_ST7789.h, Adafruit_NeoPixel.h, driver/gpio.h, driver/pulse_cnt.h,
             esp_random.h, esp_system.h
  sim/       one small unit per board part:
    clock      millis()/micros()/delay() -> sim time
    gpio       pin levels, pull-ups, open drain
    mcp23017   register model (IODIR, GPPU, GPIO, OLAT; BANK=0), GPA7/GPB7 output-only;
               answers Wire at register level and bit-banged SDA/SCL at bit level
    pcnt       implements the edge/level actions encoders.cpp configures
    st7789     decodes SPI command/data bytes (CASET, RASET, RAMWR, ...) into a framebuffer
    host       "the Mac": HID boot-keyboard reports, consumer usages, serial both directions
    nvs        Preferences store; can be backed by a file so it survives a simulated reboot
    wifi       WiFi + WiFiManager behaviour: join ok/fail, portal up/fail, link drop, form submit
  SimDeck.h  facade used by tests
test/native/test_sim_*/      scenario suites against src/main.cpp
test/native/test_sim_bringup/ scenario suite against src/bringup/main.cpp
```

Rules:

- Production code compiles unchanged. No `#ifdef TEST` in `src/` or `lib/`.
- Every fake/model header lists the hardware behaviour it assumes, each with a source
  (`docs/kb/...`, datasheet section) or `[unverified]`.
- **Fresh device per test.** `main.cpp` keeps state in file statics, so each test runs in a
  `fork()`ed child: `setup()` sees clean statics like a power-on. The parent turns the child's
  exit status into the Unity result. The child exits normally so gcov writes its counters.
- `main.cpp` and `bringup/main.cpp` both define `setup()`/`loop()` → separate test binaries
  (envs `sim` and `sim_bringup`).

## SimDeck API (sketch)

```cpp
SimDeck sim;
sim.nvs().loadPreset("presets/mac-daily.json");  // real preset file
sim.resetReason(ESP_RST_BROWNOUT);
sim.boot();                       // setup()
sim.press(k); sim.release(k);     // physical key position 1..12
sim.knobPress(n); sim.knobRelease(n);
sim.turn(n, detents);             // +CW / -CCW, real quadrature sequence on the A/B pins
sim.run(ms);                      // loop() once per simulated ms
sim.reboot();                     // forks again: the rest of the test continues in a fresh
                                  // child (clean statics, setup() rerun); the file-backed NVS
                                  // carries over, host logs start empty
sim.host();   sim.lcd();   sim.leds();   sim.wifi();   sim.serial();
sim.fault().mcpAbsent(); .mcpNackAfterIdle(ms); .stuckLow(pin); .swapAB(n); .deadPhase(n);
sim.fault().nvsCorrupt(); .wifiJoinFails(); .portalFails(); .linkDrops();
```

## Time and data flow

- Single sim clock. `run(ms)` calls `loop()` and advances 1 ms per call, plus any `delay()` the
  firmware does. Timing logic only compares `millis()` values, so the loop rate does not change
  results. Long timers (5 s chord hold, 10 min Wi-Fi off, 240 s dimmer) run in milliseconds.
- Input: `press(k)` pulls the MCP pin for physical position k low (map from
  `lib/deck_core/src/mcp23017.cpp`); the firmware reads it over fake Wire. `turn()` drives the
  quadrature sequence on GPIO3/4 or GPIO1/2 (pins from `include/pins.h`); the PCNT model counts it
  per the configured actions, so a wrong config gives a wrong count or direction.
- Output: tests assert what the host receives: 8-byte keyboard reports (`keyDown(usage)`,
  `allKeysUp()`), consumer usages, decoded helper frames (real `FrameReader`), LCD text/framebuffer,
  LED pixel values and brightness.

## Fault injection

| Fault | Origin | Expected response |
|---|---|---|
| MCP absent / NACK | bench 2026-10-18 | `mcpBegin` retried, `onReadFailed`, no stuck HID key |
| I2C fails after idle gap | bench 2026-10-18 | bringup `d` diag reports it |
| Stuck pin / short | bringup judging | idle-level check names the pin |
| Encoder A/B swapped, dead phase | bringup fail texts | guided step fails with that cause |
| NVS blob corrupt | keymap CRC | defaults + keymap-reset banner |
| Brownout reset | `esp_reset_reason` | brownout banner for 8 s |
| Wi-Fi join fail / AP fail / link drop | wifi_portal | matching banners, `portalStop`, 10 min auto-off |
| Bad portal field | portal save | invalid-key banner, map unchanged |
| Helper absent / junk serial | helper_link | helper-missing banner, frame resync |

## Scenario suites

- `test_sim_keys`: 12 keys with default and preset maps, multi-key hold, modifier order,
  release-all on bus loss.
- `test_sim_knobs`: volume, tracks, knob chord → Wi-Fi after 5 s, knob 1 confirm / mute,
  knob 2 mic gesture.
- `test_sim_portal`: chord → portal → form submit (including preset JSON values) → knob 1 →
  NVS → reboot → map persists; invalid field; discard timeout.
- `test_sim_display_leds`: banner priority, dimmer 120/240 s, LED brightness cap.
- `test_sim_helper`: HELLO / VOLUME / MIC in, ACTION / MIC frames out, junk resync.
- `test_sim_bringup`: every serial command (`i p m M l w o s b r d e h`) and the guided run `g`
  with typed y/n answers, pass and fail paths.

Existing `test/native/test_*` suites stay; `native` env stays the fast logic-only run.

## Coverage tooling

- `tools/coverage_flags.py`: PlatformIO extra script adding `--coverage` to CCFLAGS **and**
  LINKFLAGS (PlatformIO alone does not pass it to the linker; seen 2026-10-24).
- Envs `sim` and `sim_bringup` use it. Includes: `test/sim/fakes` first, then the usual paths.
- `tools/coverage.sh`:
  1. runs every suite (native + sim + sim_bringup);
  2. after each suite saves a gcovr JSON tracefile (PlatformIO rebuilds between suites);
  3. merges them; prints a summary; writes HTML to `.pio/coverage/index.html`;
  4. gcovr options: `--filter lib/deck_core/src --filter src --exclude 'src/spike_.*'
     --exclude-unreachable-branches --exclude-throw-branches`;
  5. exits non-zero if lines < 95 %, branches < 95 %, or more than 10 `GCOVR_EXCL` markers in
     `src/` + `lib/`.
- Exclusion markers: only for branches no test can reach, each with
  `// GCOVR_EXCL_BR_LINE: <reason>`. Cap: 10 in total.

## CI

New step "Coverage gate" in `.github/workflows/ci.yml` runs `tools/coverage.sh` (Ubuntu, gcc) and
uploads `.pio/coverage/` as an artifact. CI's gcc number is the authoritative one; local macOS
(clang) numbers are close but not identical. Existing steps unchanged.

## Process

- TDD as in CLAUDE.md. For existing code a new sim test may pass on first run; each such test is
  checked by breaking the code it covers once (mutation spot-check) and seeing it fail.
- CLAUDE.md "Done" gains: `tools/coverage.sh` passes (≥ 95 % line + branch).

## Order of work

1. Coverage env + script; baseline for `deck_core` only.
2. Fakes + sim core (clock, gpio, mcp23017, host, nvs) → `test_sim_keys`.
3. pcnt, display and LED models → knobs + display/LED suites.
4. Wi-Fi/portal and helper suites.
5. Bringup: serial-driven commands, bit-level I2C slave, ST7789 decoder.
6. Close gaps, add justified exclusions, enable CI gate, update CLAUDE.md.

Each step is committed with tests green and both firmware envs building.

## Limits

The sim proves the firmware matches the model. Model assumptions are listed per fake; anything
`[unverified]` there is a candidate for a bench check, not a fact.
