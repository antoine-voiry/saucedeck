# Peripherals

## PCF8575 (16-bit I2C I/O expander)
- **Default address 0x20 (A0–A2 low).** `[verified: TI PCF8575 datasheet]`
- **Quasi-bidirectional I/O: write 1 to use pin as input (weak pull-up). Read = 2 bytes, P0x first then P1x.** `[verified: TI PCF8575 datasheet]`
- **I2C max 400 kHz.** `[verified: TI PCF8575 datasheet]`
- **INT is open-drain, active-low; cleared by a read. Needs pull-up.** `[verified: TI PCF8575 datasheet]`
- **SAUCEDECK module = Open-Smart "PCF8575" board, chip marked PCF8575CTS → PCF8575C.** `[verified: operator read marking, 2026-10-04]`
- **PCF8575C: P-ports open-drain, NO internal pull-up, need external pull-up to VCC. VCC spec 4.5–5.5V.** `[verified: TI PCF8575C datasheet https://ti.com/document-viewer/PCF8575C/datasheet]`
- **Open-Smart module: inputs need external pull-up (author uses 100 kΩ to VCC), button to GND.** `[verified: https://michiel.vanderwulp.be/domotica/Modules/IO-expander-PCF8575/]`
- **Open-Smart module power: VCC → onboard 3.3V regulator → chip VDD (VCC-VDD pad open). Pad soldered = VDD tied to VCC.** `[verified: seller listing + vanderwulp page]`
- **Regulator = AMS1117-3.3 (~1.1V dropout) → 3.3V on VCC gives only ~2.2V at chip. SDA/SCL header side pulled up 10 kΩ to VCC → never put 5V on VCC with an ESP32.** `[unverified — generic schematic text from operator, check regulator marking]`
- **Firmware rule: never drive a P pin LOW if it can be tied to VCC (damages chip). Buttons-to-GND + write HIGH = safe.** `[verified: vanderwulp page]`
- **Possible symptom of PCF8575 with VCC/GND open (hypothesis REJECTED on this unit, VCC=3.3V measured): still ACKs on I2C and sinks LOW (parasitic power via SDA/SCL clamp diodes), but inputs float.** `[unverified]`
- **Observed on SAUCEDECK PCF (cause: PCF8575C open-drain, no pull-ups, + low VDD): inputs toggle at 50 Hz untouched, driven-high bits sag, INT never fires.** `[verified: bench, docs/bringup/2026-10-04.log]`
- **Firmware power check: drive 0x00FF, read 5× over 100 ms. Healthy = stable. Decay / ERR = power starved OR external load on P lines.** `[verified: bench, 2026-10-04 — on this unit VCC measured 3.3V yet bits still sag, so not power alone]`

## EC11 rotary encoder
- **Common variant: 4 quadrature transitions per detent (20 detents/rev, 20 pulses).** Some variants = 2 per detent. `[verified-ish: bench 2026-10-04, ~19 toggles per ~90° twist ≈ 5 detents × 4; exact 1-click count still pending]`
- **SAUCEDECK encoders: ENC1 (GPIO2/3) and ENC2 (GPIO4/5) both decode correct direction, no idle noise with INPUT_PULLUP.** `[verified: bench, 2026-10-04]`

## ST7789 LCD (GMT020-02-8P, 2", 240×320)
- **Write-only SPI in this design (no MISO).** `[verified: wiring.md]`
- **Onboard 6206A LDO → accepts 5V VCC.** `[unverified — from wiring.md notes]`
- **GMT020-02-8P: BL floating = backlight OFF. BL→3V3 = on.** Contradicts "onboard circuit keeps backlight on". `[verified: bench 2026-10-04]`
- **GMT020-02-8P init: SWRESET, SLPOUT, COLMOD 0x55, MADCTL 0x00, INVON, NORON, DISPON → correct RGB colors at 20 MHz, SPI mode 0 (mode 3 also OK).** `[verified: bench 2026-10-04]`
- **Debug trick: no image with backlight on → paint a distinct color per SPI variant (mode × CS/DC × SCK/MOSI swap), ask which color shows. Found swapped wires in one run.** `[verified: bench 2026-10-04]`

## WS2812B
- **VIH min = 0.7 × VDD → 3.5V at 5V supply.** 3.3V data marginal. `[verified: WS2812B datasheet]`
- **SAUCEDECK strip: 3.3V GPIO7 → 470 Ω → DIN works, correct order with `rgbLedWrite` (first LED).** Margin still thin; full-strip test pending. `[verified: bench, 2026-10-04]`
- **SAUCEDECK build: strip powered from 5V VBUS, 70 µF electrolytic across 5V/GND, 470 Ω series resistor on DIN. No level shifter.** `[verified: user report, 2026-10-04; corrects earlier "470 nF" entry]`
- **70 µF is below the usual 100 µF floor. Fine for short strips at capped brightness; a long strip at high brightness may need 470–1000 µF.** `[unverified — depends on LED count, bench check for brownout]`
- **Bulk cap (typ. 100–1000 µF) absorbs inrush; series resistor (typ. 220–470 Ω) damps ringing on DIN. Neither fixes 3.3V→VIH margin.** `[verified: Adafruit NeoPixel Überguide, best practices]`
- **Cheap margin fix without shifter: power first LED from 5V via 1N4148/1N4001 drop (~4.3V → VIH ≈ 3.0V), it re-drives clean 5V data to rest.** `[unverified — bench check]`
- **~60 mA per LED full white.** `[verified: common datasheet figure, approx]`
