# Peripherals

## PCF8575 (16-bit I2C I/O expander) — LEGACY, removed from build 2026-10-18
> Kept for reference: old module was a PCF8575C (no pull-ups). Replaced by MCP23017, see below.

- **Default address 0x20 (A0–A2 low).** `[verified: TI PCF8575 datasheet]`
- **Quasi-bidirectional I/O: write 1 to use pin as input (weak pull-up). Read = 2 bytes, P0x first then P1x.** `[verified: TI PCF8575 datasheet]`
- **I2C max 400 kHz.** `[verified: TI PCF8575 datasheet]`
- **INT is open-drain, active-low; cleared by a read. Needs pull-up.** `[verified: TI PCF8575 datasheet]`
- **SAUCEDECK module = Open-Smart "PCF8575" board, chip marked PCF8575CTS → PCF8575C.** `[verified: operator read marking, 2026-10-04]`
- **PCF8575C: P-ports open-drain, NO internal pull-up, need external pull-up to VCC. VCC spec 4.5–5.5V.** `[verified: TI PCF8575C datasheet https://ti.com/document-viewer/PCF8575C/datasheet]`
- **Open-Smart module: inputs need external pull-up (author uses 100 kΩ to VCC), button to GND.** `[verified: https://michiel.vanderwulp.be/domotica/Modules/IO-expander-PCF8575/]`
- **Open-Smart module power: VCC → onboard 3.3V regulator → chip VDD (VCC-VDD pad open). Pad soldered = VDD tied to VCC.** `[verified: seller listing + vanderwulp page]`
- **Regulator = AMS1117-3.3 (~1.1V dropout) → 3.3V on VCC gives only ~2.2V at chip. SDA/SCL header side pulled up 10 kΩ to VCC → never put 5V on VCC with an ESP32.** `[unverified — generic schematic text from operator, check regulator marking]`
- **Workaround without pull-ups: column scan. PCF8575C sinks LOW strongly; drive one P pin LOW at a time, buttons' common to one MCU GPIO with internal pull-up. Floating released pins can't pull the sense line down.** `[unverified — bench pending, 2026-10-05]`
- **Refresh-read (write 0xFFFF then read immediately) does NOT work on PCF8575C: pins fall LOW within ms untouched.** `[verified: bench, 2026-10-04]`
- **Firmware rule: never drive a P pin LOW if it can be tied to VCC (damages chip). Buttons-to-GND + write HIGH = safe.** `[verified: vanderwulp page]`
- **Possible symptom of PCF8575 with VCC/GND open (hypothesis REJECTED on this unit, VCC=3.3V measured): still ACKs on I2C and sinks LOW (parasitic power via SDA/SCL clamp diodes), but inputs float.** `[unverified]`
- **Observed on SAUCEDECK PCF (cause: PCF8575C open-drain, no pull-ups, + low VDD): inputs toggle at 50 Hz untouched, driven-high bits sag, INT never fires.** `[verified: bench, docs/bringup/2026-10-04.log]`
- **Firmware power check: drive 0x00FF, read 5× over 100 ms. Healthy = stable. Decay / ERR = power starved OR external load on P lines.** `[verified: bench, 2026-10-04 — on this unit VCC measured 3.3V yet bits still sag, so not power alone]`

## MCP23017 (16-bit I2C I/O expander) — current
### Datasheet (Microchip DS20001952D)
- **Address = 0b0100 A2 A1 A0 → 0x20–0x27. RESET "must be externally biased".** `[verified: DS20001952D, 2026-10-18]`
- **Registers (BANK=0): IODIRA 00, IODIRB 01, IOCON 0A, GPPUA 0C, GPPUB 0D, GPIOA 12, GPIOB 13, OLATA 14, OLATB 15. POR: IODIR=0xFF (all inputs), GPPU=0, IOCON=0.** `[verified: DS20001952D Table 3-1, 3-5]`
- **IOCON.SEQOP=0 (default) → address auto-increments: 2-byte read from GPIOA = GPIOA then GPIOB.** `[verified: DS20001952D §3.2.1, 3.2.2.1]`
- **Internal weak pull-up per pin via GPPU: IPU 40/75/115 µA min/typ/max at VDD=5V (≈44–125 kΩ). Weaker at 3V3. Enough for keys straight to GND.** `[verified: DS20001952D D070; keys bench-verified 2026-10-18]`
- **GPA7 and GPB7 are OUTPUT-ONLY on MCP23017 (Rev D 2022+, still in Rev E; Rev C 2016 lacked the note). Never wire an input there. Firmware drives them LOW.** `[verified: DS20001952D pin table "Output only (MCP23017)"]`
- **INT outputs push-pull, active-low by default (IOCON ODR=0, INTPOL=0) → idle INT drives HIGH. Never share a net with a driven MCU output (e.g. U0TXD).** `[verified: DS20001952D Register 3-5]`

### SAUCEDECK module
- **Waveshare MCP23017 board, 6-pin header (VCC GND SDA SCL INTA INTB), no RESET pin on header. A0–A2 pads open → 0x27.** `[verified: operator + bench scan, 2026-10-18]`
- **INTA/INTB unconnected; firmware polls. (First solder had INTA→GPIO5 = old ENC2 B, INTB→U0TXD; both removed.)** `[verified: operator, 2026-10-18]`
- **Idle levels with init applied: raw GPIOA|GPIOB<<8 = 0x7F7F (bit 7s are LOW outputs).** `[verified: bench 2026-10-18]`
- **Floating MCP inputs (GPPU off) pick up 50 Hz mains hum: toggle every 20 ms untouched.** `[verified: bench 2026-10-18]`

### Bench fault 2026-10-18 (resolved): I2C works only with back-to-back traffic
- **Symptom: ACK only at 0x27, but fails whenever the bus idles > ~1–3 ms (gap 0–300 µs → 40/40, 1 ms → 38/40, ≥3 ms → 0/40). Idle SDA/SCL HIGH, no glitches in 10 ms of sampling. Registers kept their values throughout.** `[verified: bench, bringup 'd', 2026-10-18]`
- **Bit-banged I2C on the same pins failed identically (0 ms → 19/20, 5 ms → 1/20, ≥20 ms → 0/20) → not the ESP I2C driver.** `[verified: bench, bringup 'e', 2026-10-18]`
- **Cause: bad contact in module wires/header. Operator reseated → 40/40 at all gaps up to 20 ms, bit-bang 20/20.** `[verified: bench 2026-10-18]`
- **Lesson: "works only with continuous traffic" on I2C = check connectors/joints before chasing firmware. A chip on a poor supply joint can keep registers (retention needs far less voltage than I2C operation).** `[verified: bench 2026-10-18; mechanism unverified]`
- **Side effect: failing I2C reads stall polled encoder decode (96 phase toggles → 5 detents counted).** `[verified: bench 2026-10-18]`

## EC11 rotary encoder
- **Common variant: 4 quadrature transitions per detent (20 detents/rev, 20 pulses).** Some variants = 2 per detent. `[verified-ish: bench 2026-10-04, ~19 toggles per ~90° twist ≈ 5 detents × 4; exact 1-click count still pending]`
- **Old build (2026-10-04): ENC1 (GPIO2/3) and ENC2 (GPIO4/5) both decode correct direction, no idle noise with INPUT_PULLUP.** `[verified: bench, 2026-10-04]`
- **Rebuild (2026-10-18): knob 1 = phases GPIO3/4 + switch MCP GPB4; knob 2 = phases GPIO1/2 + switch MCP GPB5 (A/B pairs crossed vs the soldering list, fixed in pins.h). Both directions correct, 4 transitions/detent → 1 count per click.** `[verified: bench monitor 2026-10-18]`

## ST7789 LCD (GMT020-02-8P, 2", 240×320)
- **Write-only SPI in this design (no MISO).** `[verified: wiring.md]`
- **Onboard 6206A LDO → accepts 5V VCC.** `[unverified — from wiring.md notes]`
- **GMT020-02-8P: BL floating = backlight OFF. BL→3V3 = on.** Contradicts "onboard circuit keeps backlight on". `[verified: bench 2026-10-04]`
- **GMT020-02-8P init: SWRESET, SLPOUT, COLMOD 0x55, MADCTL 0x00, INVON, NORON, DISPON → correct RGB colors at 20 MHz, SPI mode 0 (mode 3 also OK).** `[verified: bench 2026-10-04]`
- **Rebuild (2026-10-18): SCK=12, MOSI=11 (IO_MUX native), CS=10, DC=13, RST=GPIO44, BL=GPIO9 → R/G/B/W correct with same init at 20 MHz mode 0.** `[verified: bench 2026-10-18]`
- **BL current from GPIO9 not measured yet. GPIO should source ≤20 mA; if higher, BL → 3V3.** `[unverified]`
- **Debug trick: no image with backlight on → paint a distinct color per SPI variant (mode × CS/DC × SCK/MOSI swap), ask which color shows. Found swapped wires in one run.** `[verified: bench 2026-10-04]`

## WS2812B
- **VIH min = 0.7 × VDD → 3.5V at 5V supply.** 3.3V data marginal. `[verified: WS2812B datasheet]`
- **Old build: 3.3V GPIO7 → 470 Ω → DIN works, correct order with `rgbLedWrite` (first LED).** Margin still thin; full-strip test pending. `[verified: bench, 2026-10-04]`
- **Old build: strip powered from 5V VBUS, 70 µF electrolytic across 5V/GND, 470 Ω series resistor on DIN. No level shifter.** `[verified: user report, 2026-10-04; corrects earlier "470 nF" entry]`
- **70 µF is below the usual 100 µF floor. Fine for short strips at capped brightness; a long strip at high brightness may need 470–1000 µF.** `[unverified — depends on LED count, bench check for brownout]`
- **Bulk cap (typ. 100–1000 µF) absorbs inrush; series resistor (typ. 220–470 Ω) damps ringing on DIN. Neither fixes 3.3V→VIH margin.** `[verified: Adafruit NeoPixel Überguide, best practices]`
- **Cheap margin fix without shifter: power first LED from 5V via 1N4148/1N4001 drop (~4.3V → VIH ≈ 3.0V), it re-drives clean 5V data to rest.** `[unverified — bench check]`
- **Rebuild (2026-10-18): LED board (own resistor + cap) on GPIO6, 5V from VBUS. First LED R/G/B order correct, off works. One "off" was ignored while the MCP bus was flaky; fine after reseat — not reproduced.** `[verified: bench 2026-10-18]`
- **~60 mA per LED full white.** `[verified: common datasheet figure, approx]`
