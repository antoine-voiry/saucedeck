# ESP32-S3 SuperMini Macro Deck Pinout Mapping

Rebuild of 2026-10-18 (perfboard). Silkscreen number on the SuperMini = GPIO number.
Mirrored in `include/pins.h` — change both or neither.

---

## 1. ESP32-S3 SuperMini Board Pinout

| Pin / Pad | Destination | Bus / Protocol | Notes |
| :--- | :--- | :--- | :--- |
| **5V / VBUS** | LED board `5V` **only** | Power (5V) | Direct 5V from USB host |
| **3V3** | MCP23017 `VCC`, LCD `VCC` | Power (3.3V) | Never 5V on MCP VCC (its I2C pull-ups would put 5V on GPIO7/8) |
| **GND** | Common system GND | Ground | Encoders C+S2, key chain, MCP, LCD, LED |
| **GPIO 1** | EC11 #2 — `A` | Direct GPIO | `INPUT_PULLUP` |
| **GPIO 2** | EC11 #2 — `B` | Direct GPIO | `INPUT_PULLUP` |
| **GPIO 3** | EC11 #1 — `A` | Direct GPIO | Strapping pin (JTAG source). Input only. Safe with default eFuses |
| **GPIO 4** | EC11 #1 — `B` | Direct GPIO | `INPUT_PULLUP` |
| **GPIO 5** | — | — | Must stay empty |
| **GPIO 6** | LED board — `DIN` | Single-wire | WS2812, DIN end |
| **GPIO 7** | MCP23017 — `SCL` | I2C clock | |
| **GPIO 8** | MCP23017 — `SDA` | I2C data | |
| **GPIO 9** | LCD — `BL` | Output | Measure BL current first: > 20 mA → BL to 3V3 instead |
| **GPIO 10** | LCD — `CS` | SPI | |
| **GPIO 11** | LCD — `SDA` (MOSI) | SPI | FSPID on IO_MUX |
| **GPIO 12** | LCD — `SCL` (SCK) | SPI | FSPICLK on IO_MUX |
| **GPIO 13** | LCD — `DC` | SPI control | |
| **GPIO 43 (TX)** | — | — | Must stay empty: ROM boot log drives it |
| **GPIO 44 (RX)** | LCD — `RST` | Output | Free because Serial runs over USB CDC. Never `Serial0.begin()` |

---

## 2. MCP23017 (Waveshare board)

Chip: MCP23017. Address 0x27 (A0–A2 pads open, bench-verified 2026-10-18). RESET handled on the board.
Keys and encoder switches go **straight to GND**; firmware enables the internal pull-ups (GPPU).
**GPA7 and GPB7 are output-only on the MCP23017** (Microchip DS20001952D) → unused, driven LOW by firmware.

Key **positions** are the physical order on the deck (bench-verified 2026-10-18); they differ
from the K labels used while soldering. Firmware sends position 1–12 as F13–F24.

| MCP23017 Pin | Connected To | Deck bit |
| :--- | :--- | :--- |
| **VCC** | ESP32 `3V3` | |
| **GND** | System `GND` | |
| **SDA** | ESP32 `GPIO 8` | |
| **SCL** | ESP32 `GPIO 7` | |
| **INTA, INTB** | nothing | firmware polls |
| **GPB0–GPB3** | Key positions 1–4 | 0–3 |
| **GPA2, GPA1, GPA0** | Key positions 5, 6, 7 | 4–6 |
| **GPB6** | Key position 8 | 7 |
| **GPA3, GPA6, GPA5, GPA4** | Key positions 9, 10, 11, 12 | 8–11 |
| **GPA7** | unused (output-only) | |
| **GPB4** | EC11 #1 `S1` (knob 1) | 12 |
| **GPB5** | EC11 #2 `S1` (knob 2) | 13 |
| **GPB7** | unused (output-only) | |

Positions 5–7 and 9–12 (the 7 bottom keys) follow the 2026-10-20 rewire, bench-verified: all 12 keys read on distinct pins (docs/bringup/2026-10-20.log).

Key leg 2 of all 12 keys chained to GND. EC11 `S2` jumpered to its own `C` (GND).

---

## 3. EC11 encoders

| Pin | Encoder 1 | Encoder 2 |
| :--- | :--- | :--- |
| **A** | GPIO 3 | GPIO 1 |
| **B** | GPIO 4 | GPIO 2 |
| **C** | GND | GND |
| **S1** | MCP GPB4 | MCP GPB5 |
| **S2** | jumper to C | jumper to C |

---

## 4. LED board (WS2812)

16 × WS2812 (owner, 2026-10-19). Full white ≈ 960 mA → firmware caps frames at 250 mA estimated.

| Pin | Connected To | Notes |
| :--- | :--- | :--- |
| **5V** | ESP32 `5V` | Resistor and cap already on the board |
| **GND** | System `GND` | |
| **DIN** | ESP32 `GPIO 6` | DIN end, DOUT free |

> Data level: 3.3V GPIO vs 3.5V VIH min at 5V supply — marginal. Fallback: 1N4001 in LED 5V wire. See `docs/kb/peripherals.md`.

---

## 5. LCD (GMT020-02-8P, ST7789 2.0" 240×320)

| Screen Pin | Connected To | Function |
| :--- | :--- | :--- |
| **GND** | System `GND` | |
| **VCC** | ESP32 `3V3` | |
| **SCL** | ESP32 `GPIO 12` | SPI clock |
| **SDA** | ESP32 `GPIO 11` | SPI MOSI |
| **RST** | ESP32 `GPIO 44` (RX) | Reset, active low |
| **DC** | ESP32 `GPIO 13` | Data / command |
| **CS** | ESP32 `GPIO 10` | Chip select |
| **BL** | ESP32 `GPIO 9` | Or 3V3 if BL draws > 20 mA |

> Bench-verified 2026-10-18: R/G/B/W fill correct, BL driven from GPIO9 (BL current not measured).
