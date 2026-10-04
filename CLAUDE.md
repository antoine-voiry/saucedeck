# SAUCEDECK

USB HID macro deck. ESP32-S3-Zero (Waveshare, ESP32-S3FH4R2). Enumerates on Mac/PC as `SAUCEDECK`.
12 keys + 2 EC11 encoders (via PCF8575 + direct GPIO), ST7789 2" LCD, WS2812B strip.
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
- `test/native/<suite>/` — Unity tests for `deck_core`, run on host. One suite per module (`test_core`, `test_bringup`).
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
- Auto checks: PCF8575 ACK (+ bus scan), idle levels (stuck pins), INT idle, encoder idle A/B.
- Operator checks (y/n): LCD 5-color fill (minimal ST7789 driver, MISO -1), first LED R/G/B.
- Guided controls: 12 keys, 2 knob switches, each encoder CW + CCW. Judged by `BringupSequencer` (tested). Fail text names likely cause (crossed wire, short, A/B swapped, dead phase, INT dead).
- Save log in `docs/bringup/YYYY-MM-DD.log`. Feed confirmed facts into `docs/kb/`.
- New wire/peripheral → add step to `kSteps` in `src/bringup/main.cpp` (+ tests if judging logic changes).

## Commands

```sh
pio test -e native          # unit tests (host). Must pass before any commit.
pio run                     # build firmware (env: saucedeck)
pio run -e bringup -t upload && pio device monitor   # wiring test
pio run -t upload           # flash. If no port: hold BOOT, plug USB, release.
pio device monitor          # serial over USB CDC, 115200
```

Done = `pio test -e native` green AND `pio run -e saucedeck -e bringup` SUCCESS with zero new warnings. Show output, don't claim.

## Platform

- pioarduino platform (Arduino core 3.x / ESP-IDF 5.x), board `esp32-s3-devkitc-1` + overrides in [platformio.ini](platformio.ini).
- `ARDUINO_USB_MODE=0` → TinyUSB OTG (needed for HID). `ARDUINO_USB_CDC_ON_BOOT=1` → Serial on same port.
- Set USB descriptors (`USB.productName`, etc.) BEFORE `USB.begin()`.
- HID: `USBHIDKeyboard` (F13–F24 for keys) + `USBHIDConsumerControl` (media).

## Hardware gotchas (detail + sources in docs/kb/esp32s3.md)

- GPIO3 = strapping pin (JTAG source). Used by ENC1 phase B. Safe with default eFuses. Never burn `STRAP_JTAG_SEL`.
- GPIO13 = FSPIQ (MISO) on IO_MUX. Used by PCF INT. Display driver must set MISO = -1.
- GPIO19/20 = USB D-/D+. GPIO26–32 = in-package flash/PSRAM. Never touch.
- GPIO21 = onboard WS2812 on S3-Zero.
- WS2812B strip at 5V: VIH min 0.7×VDD = 3.5V > 3.3V GPIO out. Marginal. Recommend 74AHCT1G125 level shifter.
- USB power budget 500 mA. Cap LED brightness in firmware.
- All GPIO 3.3V only, not 5V tolerant.
