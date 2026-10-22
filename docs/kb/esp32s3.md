# ESP32-S3 / ESP32-S3 SuperMini

## SoC
- **Strapping pins: GPIO0, GPIO3, GPIO45, GPIO46.** GPIO0 = boot mode (BOOT button). GPIO3 = JTAG signal source, only sampled when eFuse `STRAP_JTAG_SEL` burned; default eFuses → GPIO3 free after reset. `[verified: ESP32-S3 datasheet §2.6 Strapping Pins]`
- **GPIO19 = USB D-, GPIO20 = USB D+.** `[verified: ESP32-S3 datasheet, pin list]`
- **GPIO26–32 wired to in-package SPI flash/PSRAM.** Do not use. `[verified: ESP32-S3 datasheet, pin list]`
- **FSPI IO_MUX pins: GPIO10 = FSPICS0, GPIO11 = FSPID (MOSI), GPIO12 = FSPICLK, GPIO13 = FSPIQ (MISO).** IO_MUX path allows highest SPI clock; other pins route via GPIO matrix. `[verified: ESP32-S3 TRM, IO MUX table]`
- **GPIO-matrix-routed SPI tops out ~40 MHz (IO_MUX up to 80 MHz).** Old build (until 2026-10-18) had LCD SCK=11/MOSI=12 matrix-routed; rebuild uses SCK=12/MOSI=11 = IO_MUX native. `[unverified — check TRM SPI timing section]`
- **GPIO is 3.3V, not 5V tolerant.** `[verified: ESP32-S3 datasheet, electrical characteristics]`
- **Internal pull-up ≈ 45 kΩ.** `[unverified — check datasheet DC characteristics]`
- **ADC1 = GPIO1–10, ADC2 = GPIO11–20. ADC2 unusable while Wi-Fi active.** `[verified: ESP32-S3 datasheet]`

## Board actually used: ESP32-S3 SuperMini
- **SAUCEDECK runs on an ESP32-S3 SuperMini, not a Waveshare S3-Zero (earlier docs wrong).** `[verified: operator, 2026-10-05]`
- **SuperMini free pins after 2026-10-18 rebuild: GPIO5 and U0TXD GPIO43. GPIO44 = LCD RST (correction: was button sense in old build).** GPIO43 prints ROM boot log at reset → avoid for inputs that touch other circuits. `[verified: operator + ESP32-S3 datasheet UART0 default pins]`
- **SuperMini chip = ESP32-S3 (QFN56) rev v0.2, embedded 4 MB flash (XMC, mfr 0x46 dev 0x4016), embedded 2 MB PSRAM (AP_3v3) = ESP32-S3FH4R2.** `[verified: esptool flash_id after 1200 bps touch, 2026-10-22]` (Correction: was unverified.) Onboard LED pin still `[unverified]`.
- **esptool over TinyUSB CDC can't reset the chip itself ("No serial data received"): do the 1200 bps touch first, then `--before no_reset`.** `[verified: bench 2026-10-22]`

## Waveshare ESP32-S3-Zero (NOT this project's board, kept for reference)
- **Chip: ESP32-S3FH4R2 — 4 MB in-package flash, 2 MB QSPI PSRAM.** `[verified: Waveshare wiki]`
- **Onboard WS2812 RGB LED on GPIO21.** `[verified: Waveshare wiki]`
- **USB-C wired direct to native USB (GPIO19/20), no USB-UART bridge.** If firmware breaks USB → hold BOOT while plugging in to reach ROM download mode. `[verified: Waveshare wiki]`
- **3.3V LDO max current.** `[unverified — check S3-Zero schematic]`
- **PCNT: 1 group, 4 units, 2 channels/unit → 2 EC11 knobs use 2 units (x4 quadrature, edge+level per channel).** `[verified: framework soc_caps.h:275-277 (SOC_PCNT_*), 2026-10-19]`
- **IDF 5 PCNT driver `pcnt_unit_config_t.flags.accum_count` + watch points at ±limit → `pcnt_unit_get_count()` returns an accumulated count across overflow.** `[verified: driver/pulse_cnt.h:67, 2026-10-19]` Glitch filter range for S3 `[unverified — TRM PCNT chapter]`.
