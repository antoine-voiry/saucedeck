# Toolchain

- **pioarduino platform URL:** `https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip` → Arduino core 3.x. `[verified: pio run SUCCESS, 2026-10-04]`
- **`board_build.extra_flags` override replaces board JSON flags** → `-DARDUINO_USB_MODE=0` lands once, no redefinition warning. `[verified: pio run -v, 77/77 compile units, 2026-10-04]`
- **`ARDUINO_USB_MODE=0` = TinyUSB OTG stack (HID capable). `=1` = HW USB-Serial-JTAG (no HID).** `[verified: arduino-esp32 USB docs]`
- **USB descriptors (`USB.productName/manufacturerName`) must be set before `USB.begin()`.** `[verified: arduino-esp32 USB API docs]`
- **`KEY_F13`..`KEY_F24` defined in `USBHIDKeyboard.h`.** `[verified: pio run SUCCESS, 2026-10-04]`
- **Native tests: suites in `test/native/<suite>/`, `[env:native]` has `test_filter = native/*`, ESP32 envs `test_ignore = native/*`.** Host compiler = Apple clang. `[verified: pio test -e native 32/32, 2026-10-04]`
- **Several firmwares in one project: shared `[esp32s3]` section + `extends`, split via `build_src_filter`.** `[verified: pio run saucedeck + bringup SUCCESS, 2026-10-04]`
- **`SPI.begin(sck, -1, mosi, -1)` leaves MISO unclaimed: GPIO13 works as LCD DC.** `[verified: bench LCD fill, 2026-10-18]`
- **`rgbLedWrite(pin, r, g, b)` (core 3.x) drives one WS2812 via RMT — enough to test strip DIN.** `[verified: bench first LED R/G/B, 2026-10-04 + 2026-10-18]`
- **Upload via 1200bps touch fails with "Couldn't find a board on the selected port": the SuperMini comes back on the SAME port name, so `wait_for_upload_port` never sees a new port. Workaround: `pio run -e <env> -t upload --upload-port /dev/cu.usbmodem<id>` (works, no BOOT button needed).** `[verified: bench 2026-10-18]`
- **Arduino-ESP32 3.x `Wire.endTransmission(false)` returns 0 and the following `requestFrom` does the repeated-start read (MCP23017 register read OK).** `[verified: bench 2026-10-18, Wire 3.3.5]`
- **Native tests 2026-10-18: suites `test_core`, `test_bringup`, `test_mcp` → 39/39.** `[verified: pio test -e native]`
- **Platform pinned to pioarduino 55.03.35 (release 2025-12-19). Latest on 2026-10-18 = 55.03.312-1, untested here.** `[verified: ~/.platformio/platforms/espressif32/.piopm + GitHub releases API, 2026-10-18]`
- **`-Wextra` on firmware sources warns in framework header `tinyusb/src/common/tusb_types.h:557` (enum/non-enum in conditional) → firmware `build_src_flags` = `-Wall` only.** `[verified: build output, 2026-10-18]`
- **`pio check` (cppcheck) default filter skips `lib/` → need `check_src_filters` incl. `lib/deck_core/src/`. `unusedFunction` = false positives (setup/loop, cross-file).** `[verified: pio check output, 2026-10-18]`
- **`ARDUINO_USB_CDC_ON_BOOT=1` → core `app_main` calls `USB.begin()` before `setup()` (`ARDUINO_USB_ON_BOOT` = CDC|MSC|DFU on boot). `USB.productName()` / `manufacturerName()` in `setup()` then return false and do nothing → Mac saw "Espressif ESP32-S3-DevKitC-1-N8". Fix: `-DUSB_PRODUCT="SAUCEDECK"` + `-DUSB_MANUFACTURER="SAUCEDECK"` build flags → ioreg shows SAUCEDECK on all HID interfaces.** `[verified: core main.cpp:108, USB.h:26, USB.cpp:38/315; ioreg on Mac, 2026-10-18]`
- **Upload after 1200 bps touch fails "Couldn't find a board"; explicit `--upload-port` works. Port name changes C1 ↔ C2 between bootloader/app.** `[verified: bench 2026-10-18]`
- **Adafruit ST7789 lib 1.11.0 → pulls seesaw 1.7.9 → pulls registry `arduino-libraries/SD`. Unpinned it is downloaded but not linked; pinning it in `lib_deps` forces it into the build and it fails (`Sd2PinMap.h: Architecture or board not supported`).** `[verified: build output, 2026-10-18]`
- **Upload via 1200 bps touch: ROM download mode can appear as a different port (`/dev/cu.usbmodem21101`) than the app (`…A0C1`/`…A0C2`); esptool followed it and flashed OK. Only one Espressif device on USB at the time. First attempt timed out (FAILED, 15 s), retry OK.** `[verified: bench 2026-10-19]`
- **Spike: WiFiManager 2.0.17 + WiFi on pioarduino 55.03.35 builds clean (0 warnings); spike alone = 1,093,783 B = 83.4 % of the default 1,310,720 B app partition.** `[verified: build output 2026-10-21]`
- **Arduino USBHID = ONE HID interface for keyboard (report ID 1) + consumer (ID 4) + USBHIDVendor (usage page 0xFF00, ID 6, 63 B in/out/feature). macOS sees one IOHIDDevice with PrimaryUsage = keyboard: match on DeviceUsagePage (not PrimaryUsagePage). Opening it from an unprivileged process: IOHIDManagerOpen = 0xe00002e2 (kIOReturnNotPermitted), SetReport = 0xe00002cd (not open) -> needs Input Monitoring.** `[verified: ioreg ReportDescriptor + tools/spike/hidprobe.swift, 2026-10-21]`
- **Production firmware with WiFiManager + Wi-Fi = 1,211,351 B: does not fit the default 1.25 MB app slot (92 %) → huge_app.csv (3 MB, 38.5 %). Partition table rewritten at 0x8000 by a normal pio upload.** `[verified: build + upload 2026-10-22]`
- **WiFiManager 2.0.17 on pioarduino 55.03.35: custom params served in STA mode at /param after startWebPortal(); mDNS saucedeck.local resolves.** `[verified: bench curl 2026-10-22]`

## macOS host keys (2026-10-24, macOS 26.7.1)

- F13-F20 from the deck arrive as macOS keyDown events (keycodes 105/107/113/106/64/79/80/90).
  F21-F24 (HID 0x70-0x73) produce no event at all: macOS has no virtual keycode for them.
  Never map Mac actions to F21-F24. [verified: CGEventTap logger on bench, 2026-10-24]
- Shortcuts app "Add Keyboard Shortcut" recorded no key (not even Ctrl+Opt+Cmd+T) on this Mac.
  Hammerspoon hs.hotkey on F13-F20 works. [verified: bench, 2026-10-24]
- Deck enumerates as keyboard (page 1 usage 6) + consumer (page 12 usage 1), VID 0x303a PID 0x1001.
  [verified: hidutil list, 2026-10-24]

## PlatformIO native tests (2026-10-24)

- PIO 6.2 reports any failing native suite as ERRORED with "Program received signal SIGHUP" and
  counts one extra test case (e.g. "22 test cases: 1 failed, 20 succeeded" for 21 tests). Unity's
  own line ("N Tests M Failures") is the true count. Not caused by the sim fork harness: same in
  env native. [verified: bench run, 2026-10-24]
- One `pio run -e saucedeck -e bringup` failed once (saucedeck, 3 s) and left a generated
  `src/idf_component.yml`; not reproducible (incremental, clean, without the file). Watch for it.
  [unverified cause]
- **Firmware with Plan 1 (legend layout) + Plan 2 (status cards) + FreeSans9pt7b (2026-10-31) = 1218239 B = 38.7 % of the 3 MB huge_app slot.** `[verified: build output 2026-10-31]`
