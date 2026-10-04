# Toolchain

- **pioarduino platform URL:** `https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip` → Arduino core 3.x. `[verified: pio run SUCCESS, 2026-10-04]`
- **`board_build.extra_flags` override replaces board JSON flags** → `-DARDUINO_USB_MODE=0` lands once, no redefinition warning. `[verified: pio run -v, 77/77 compile units, 2026-10-04]`
- **`ARDUINO_USB_MODE=0` = TinyUSB OTG stack (HID capable). `=1` = HW USB-Serial-JTAG (no HID).** `[verified: arduino-esp32 USB docs]`
- **USB descriptors (`USB.productName/manufacturerName`) must be set before `USB.begin()`.** `[verified: arduino-esp32 USB API docs]`
- **`KEY_F13`..`KEY_F24` defined in `USBHIDKeyboard.h`.** `[verified: pio run SUCCESS, 2026-10-04]`
- **Native tests: `[env:native]` + `test_filter = test_native`; firmware env `test_ignore = test_native`.** Host compiler = Apple clang. `[verified: pio test -e native 11/11, 2026-10-04]`
