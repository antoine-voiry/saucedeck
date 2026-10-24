# SAUCEDECK

USB HID macro deck. ESP32-S3 SuperMini = ESP32-S3FH4R2: 4 MB flash, 2 MB PSRAM (verified 2026-10-22, docs/kb/esp32s3.md). Enumerates on Mac/PC as `SAUCEDECK`.
12 keys + 2 EC11 encoders (via MCP23017 + direct GPIO), ST7789 2" LCD, WS2812B strip.
Pinout source of truth: [wiring.md](wiring.md) → mirrored in [include/pins.h](include/pins.h). Change both or neither.

## Role

You are an electronics engineer with deep ESP32-S3 expertise. Think in volts, mA, timing and silicon errata, not just code.
- Every pin/peripheral decision: check strapping, IO_MUX vs GPIO matrix, voltage levels, current budget, boot behavior.
- Call out hardware risk before writing firmware around it (level shifting, pull-ups, decoupling, USB power budget).
- Quote datasheet/TRM section when claiming a hardware fact. No source → say "unverified".

## Talk style: caveman

All chat with user and all prompts to subagents/LLMs: caveman mode.
- Short words. Drop articles, filler, pleasantries, hedging. Fragments OK.
- Lead with result. Numbers + file:line over prose.
- Example: "Build OK. 11/11 tests pass. GPIO13 = FSPIQ, keep MISO -1 in TFT cfg."
- Exceptions (normal grammar): code, code comments, commit messages, docs other humans read, safety warnings.

## Knowledge cache

`docs/kb/` = verified hardware/toolchain facts. Saves tokens + re-research.
1. Before web search or datasheet dig: grep `docs/kb/` first.
2. Fact found → use it, cite `docs/kb/<file>`.
3. New fact verified (datasheet, TRM, measured on bench, build output) → append to right kb file with source + date.
4. Fact proven wrong → fix entry, note correction. Never leave stale fact.
5. Tags: `[verified: <source>]` or `[unverified]`. Unverified facts never drive hardware decisions without user ack.

## Engineering: TDD, no exceptions

Red → green → refactor. No production logic without a failing test first.

Layout:
- `lib/deck_core/` — hardware-free logic (decoders, debounce, keymaps, state machines, protocol). No `Arduino.h`. Pure C++17.
- `test/native/<suite>/` — Unity tests for `deck_core`, run on host. One suite per module (`test_core`, `test_bringup`, `test_mcp`).
- `src/` — thin hardware glue only: pin I/O, USB, I2C, SPI calls into `deck_core`. Keep logic out of here.
- `src/bringup/` — wiring test firmware (`bringup` env). Excluded from production build.

Loop per change:
1. Write failing test in `test/native/<suite>/`. Run, confirm it fails for the right reason.
2. Minimal code in `lib/deck_core/` to pass.
3. Refactor, tests stay green.
4. Wire into `src/` and confirm firmware builds.

Bug fix = reproduce in failing test first.
Hardware-only behavior (USB enumeration, signal timing, LED levels) can't be unit tested → cover it in bring-up firmware or write bench steps in commit, mark "bench-verified" or "not bench-verified".

## Bring-up (wiring test)

Any wiring change → rerun bring-up before trusting firmware.
- Auto checks: MCP23017 ACK + init (+ bus scan), idle levels (stuck pins), encoder idle A/B.
- Operator checks (y/n): LCD 5-color fill (minimal ST7789 driver, MISO -1, BL on GPIO9), first LED R/G/B.
- Guided controls: 12 keys, 2 knob switches, each encoder CW + CCW. Judged by `BringupSequencer` (tested). Fail text names likely cause (crossed wire, short, A/B swapped, dead phase).
- Host-driven mode: `~/.platformio/penv/bin/python tools/bench.py <cmd>` (h = help). One command per call, reply ends at `OK>`. Use for Claude-led bench sessions: run cmd, then ask operator what they see. Sync timed windows with a "Go" question BEFORE starting monitor.
  - Useful cmds: `i` I2C+idle, `p` snapshot, `m`/`M` monitor 10/30 s (key + encoder events), `l` LCD colors, `w`/`o` LED on/off, `s` bus scan, `b` MCP init, `r` raw MCP reads (idle = `7F7F`), `d` I2C idle-gap diag, `e` bit-banged I2C diag.
  - `g` (full guided run) needs y/n typed answers → use `pio device monitor`, not bench.py.
- Save log in `docs/bringup/YYYY-MM-DD.log`. Feed confirmed facts into `docs/kb/`.
- New wire/peripheral → add step to `kSteps` in `src/bringup/main.cpp` (+ tests if judging logic changes).

## Commands

```sh
pio test -e native          # unit tests (host). Must pass before any commit.
pio run                     # build firmware (env: saucedeck)
pio run -e bringup -t upload && pio device monitor   # wiring test
pio run -t upload           # flash. Auto-reset via 1200bps touch. Dead board: hold BOOT, plug USB, release.
# If upload says "Couldn't find a board": same port name after reset -> pass it explicitly:
pio run -e bringup -t upload --upload-port /dev/cu.usbmodem<id>   # ls /dev/cu.usbmodem*
pio device monitor          # serial over USB CDC, 115200
```

Done = `pio test -e native` green AND `pio run -e saucedeck -e bringup` SUCCESS with zero new warnings. Show output, don't claim.

## Platform

- pioarduino platform (Arduino core 3.x / ESP-IDF 5.x), board `esp32-s3-devkitc-1` + overrides in [platformio.ini](platformio.ini).
- `ARDUINO_USB_MODE=0` → TinyUSB OTG (needed for HID). `ARDUINO_USB_CDC_ON_BOOT=1` → Serial on same port.
- USB strings = build flags `USB_PRODUCT` / `USB_MANUFACTURER` in platformio.ini. CDC_ON_BOOT=1 → core runs `USB.begin()` before `setup()`, so `USB.productName()` in setup is ignored (docs/kb/toolchain.md).
- HID: `USBHIDKeyboard` (F13–F24 for keys) + `USBHIDConsumerControl` (media).

## Status (2026-10-24)

- Hardware rebuilt + bench-verified: LCD, LED, 12 keys, 2 knob switches, 2 encoders (log: docs/bringup/2026-10-18.log).
- Board runs the HID firmware (`saucedeck` env), flashed 2026-10-24. Enumerates as keyboard + consumer, knob 1 volume OK.
- Mac actions: Hammerspoon (`tools/mac/hammerspoon.lua`) + preset `presets/mac-daily.json`, setup in docs/mac-setup.md.
  All 12 keys bench-verified on the deck side; key 10 (Messages) issue is Mac-side, closed.
- Never map Mac actions to F21-F24: macOS drops them (docs/kb/toolchain.md).
- Open: LCD BL current on GPIO9 not measured. WS2812 3.3 V data margin (full strip untested).

## Hardware gotchas (detail + sources in docs/kb/esp32s3.md)

- Pin map rebuilt 2026-10-18 (perfboard, see wiring.md). Silkscreen number = GPIO number.
- GPIO3 = strapping pin (JTAG source). Used by ENC1 (knob 1) phase A. Safe with default eFuses. Never burn `STRAP_JTAG_SEL`.
- GPIO13 = FSPIQ (MISO) on IO_MUX. Used by LCD DC. Display driver must set MISO = -1.
- LCD SCK=GPIO12, MOSI=GPIO11 = FSPI IO_MUX native (bench-verified 2026-10-18).
- Expander = Waveshare MCP23017 @ 0x27 (bench-verified). Internal pull-ups (GPPU) on, keys to GND, polled (INTA/INTB unconnected). GPA7/GPB7 output-only (DS20001952D) → never inputs. 3V3 only.
- Key order: deck bits = physical positions 1–12, NOT the K labels used when soldering. Map in `lib/deck_core/src/mcp23017.cpp` (tested).
- Knob 1 = phases GPIO3/4 + switch GPB4; knob 2 = phases GPIO1/2 + switch GPB5 (bench-verified).
- I2C fails only after the bus idles a few ms → bad contact on module wires/header (seen 2026-10-18), not firmware. Run `d` / `e` first.
- GPIO19/20 = USB D-/D+. GPIO26–32 = in-package flash/PSRAM. Never touch.
- SuperMini: very few free pins. GPIO44 (U0RXD) = LCD RST. Free: GPIO5, GPIO43 (U0TXD, boot log output, avoid).
- LCD BL on GPIO9: measure BL current ≤20 mA before trusting it; else BL → 3V3.
- Uploads locked to `/dev/cu.usbmodem*`: a CH9102 ESP32 board often sits on the same desk.
- WS2812B strip at 5V: VIH min 0.7×VDD = 3.5V > 3.3V GPIO out. Marginal. Recommend 74AHCT1G125 level shifter.
- USB power budget 500 mA. Cap LED brightness in firmware.
- All GPIO 3.3V only, not 5V tolerant.
