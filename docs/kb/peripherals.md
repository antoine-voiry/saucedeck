# Peripherals

## PCF8575 (16-bit I2C I/O expander)
- **Default address 0x20 (A0–A2 low).** `[verified: TI PCF8575 datasheet]`
- **Quasi-bidirectional I/O: write 1 to use pin as input (weak pull-up). Read = 2 bytes, P0x first then P1x.** `[verified: TI PCF8575 datasheet]`
- **I2C max 400 kHz.** `[verified: TI PCF8575 datasheet]`
- **INT is open-drain, active-low; cleared by a read. Needs pull-up.** `[verified: TI PCF8575 datasheet]`
- **Breakout module has onboard I2C pull-ups.** `[unverified — check module]`

## EC11 rotary encoder
- **Common variant: 4 quadrature transitions per detent (20 detents/rev, 20 pulses).** Some variants = 2 per detent. `[unverified for this part — bench check]`

## ST7789 LCD (GMT020-02-8P, 2", 240×320)
- **Write-only SPI in this design (no MISO).** `[verified: wiring.md]`
- **Onboard 6206A LDO → accepts 5V VCC.** `[unverified — from wiring.md notes]`

## WS2812B
- **VIH min = 0.7 × VDD → 3.5V at 5V supply.** 3.3V data marginal. `[verified: WS2812B datasheet]`
- **SAUCEDECK build: strip powered from 5V VBUS, 70 µF electrolytic across 5V/GND, 470 Ω series resistor on DIN. No level shifter.** `[verified: user report, 2026-10-04; corrects earlier "470 nF" entry]`
- **70 µF is below the usual 100 µF floor. Fine for short strips at capped brightness; a long strip at high brightness may need 470–1000 µF.** `[unverified — depends on LED count, bench check for brownout]`
- **Bulk cap (typ. 100–1000 µF) absorbs inrush; series resistor (typ. 220–470 Ω) damps ringing on DIN. Neither fixes 3.3V→VIH margin.** `[verified: Adafruit NeoPixel Überguide, best practices]`
- **Cheap margin fix without shifter: power first LED from 5V via 1N4148/1N4001 drop (~4.3V → VIH ≈ 3.0V), it re-drives clean 5V data to rest.** `[unverified — bench check]`
- **~60 mA per LED full white.** `[verified: common datasheet figure, approx]`
