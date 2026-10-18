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
