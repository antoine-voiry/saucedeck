# ESP32-S3 / ESP32-S3 SuperMini

## SoC
- **Strapping pins: GPIO0, GPIO3, GPIO45, GPIO46.** GPIO0 = boot mode (BOOT button). GPIO3 = JTAG signal source, only sampled when eFuse `STRAP_JTAG_SEL` burned; default eFuses → GPIO3 free after reset. `[verified: ESP32-S3 datasheet §2.6 Strapping Pins]`
- **GPIO19 = USB D-, GPIO20 = USB D+.** `[verified: ESP32-S3 datasheet, pin list]`
- **GPIO26–32 wired to in-package SPI flash/PSRAM.** Do not use. `[verified: ESP32-S3 datasheet, pin list]`
- **FSPI IO_MUX pins: GPIO10 = FSPICS0, GPIO11 = FSPID (MOSI), GPIO12 = FSPICLK, GPIO13 = FSPIQ (MISO).** IO_MUX path allows highest SPI clock; other pins route via GPIO matrix. `[verified: ESP32-S3 TRM, IO MUX table]`
- **GPIO-matrix-routed SPI tops out ~40 MHz (IO_MUX up to 80 MHz).** SAUCEDECK LCD SCK=11/MOSI=12 is matrix-routed. `[unverified — check TRM SPI timing section]`
- **GPIO is 3.3V, not 5V tolerant.** `[verified: ESP32-S3 datasheet, electrical characteristics]`
- **Internal pull-up ≈ 45 kΩ.** `[unverified — check datasheet DC characteristics]`
- **ADC1 = GPIO1–10, ADC2 = GPIO11–20. ADC2 unusable while Wi-Fi active.** `[verified: ESP32-S3 datasheet]`

## Board actually used: ESP32-S3 SuperMini
- **SAUCEDECK runs on an ESP32-S3 SuperMini, not a Waveshare S3-Zero (earlier docs wrong).** `[verified: operator, 2026-10-05]`
- **SuperMini free pins after SAUCEDECK wiring: only U0RXD GPIO44 and U0TXD GPIO43.** GPIO44 now = button sense. GPIO43 prints ROM boot log at reset → avoid for inputs that touch other circuits. `[verified: operator + ESP32-S3 datasheet UART0 default pins]`
- **SuperMini chip/flash/PSRAM variant and onboard LED pin.** `[unverified — read with esptool chip_id/flash_id]`

## Waveshare ESP32-S3-Zero (NOT this project's board, kept for reference)
- **Chip: ESP32-S3FH4R2 — 4 MB in-package flash, 2 MB QSPI PSRAM.** `[verified: Waveshare wiki]`
- **Onboard WS2812 RGB LED on GPIO21.** `[verified: Waveshare wiki]`
- **USB-C wired direct to native USB (GPIO19/20), no USB-UART bridge.** If firmware breaks USB → hold BOOT while plugging in to reach ROM download mode. `[verified: Waveshare wiki]`
- **3.3V LDO max current.** `[unverified — check S3-Zero schematic]`
