# ESP32-S3-Zero Macro Deck Pinout Mapping

---

## 1. ESP32-S3-Zero Board Pinout

| Pin / Pad | Destination Signal / Device | Bus / Protocol | Notes |
| :--- | :--- | :--- | :--- |
| **5V / VBUS** | • LCD `VCC`<br>• LED Strip `5V Rail` | Power (5V) | Direct 5V from USB host |
| **3V3** | PCF8575 `VCC` + `VDD`, LCD `BL` | Power (3.3V) | VDD jumper bypasses module AMS1117 (else chip gets ~2.2V) |
| **GND** | Common System GND | Ground | Tied across all modules, switches, and LED strip |
| **GPIO 1** | PCF8575 — `SDA` | I2C Data | Hardware I2C data line |
| **GPIO 2** | EC11 #1 — `Phase A` | Direct GPIO | Hardware interrupt line |
| **GPIO 3** | EC11 #1 — `Phase B` | Direct GPIO | Hardware interrupt line |
| **GPIO 4** | EC11 #2 — `Phase A` | Direct GPIO | Hardware interrupt line |
| **GPIO 5** | EC11 #2 — `Phase B` | Direct GPIO | Hardware interrupt line |
| **GPIO 6** | PCF8575 — `SCL` | I2C Clock | Hardware I2C clock line |
| **GPIO 7** | LED Strip — `DIN` | Single-wire | WS2812B/NeoPixel data signal |
| **GPIO 8** | LCD — `RST` | SPI Control | Display hardware reset line |
| **GPIO 9** | LCD — `CS` | SPI Control | Chip Select |
| **GPIO 10** | LCD — `DC` | SPI Control | ST7789 Data / Command select |
| **GPIO 11** | LCD — `SCL` (SCK) | SPI Clock | Master Clock (GPIO matrix, ≤40 MHz) |
| **GPIO 12** | LCD — `SDA` (MOSI) | SPI Master Out | Master Out Slave In |
| **GPIO 13** | PCF8575 — `INT` | Direct GPIO | Active-Low interrupt (`INPUT_PULLUP`) |

---

## 2. PCF8575 I/O Expander Mapping

All switch terminals short to **GND** when pressed.

| PCF8575 Pin | Connected Switch / Function | Mapping Detail |
| :--- | :--- | :--- |
| **VCC** | ESP32 `3V3` | Power rail |
| **GND** | System Common `GND` | Ground rail |
| **SDA** | ESP32 `GPIO 1` | I2C Data |
| **SCL** | ESP32 `GPIO 6` | I2C Clock |
| **INT** | ESP32 `GPIO 13` | Interrupt trigger line |
| **P00** | Deck Button 1 | Side 1 |
| **P01** | Deck Button 2 | Side 1 |
| **P02** | Deck Button 3 | Side 1 |
| **P03** | Deck Button 4 | Side 1 |
| **P04** | Deck Button 5 | Side 1 |
| **P05** | Deck Button 6 | Side 1 |
| **P06** | Deck Button 7 | Side 1 |
| **P07** | Deck Button 8 | Side 1 |
| **P10** | Deck Button 9 | Opposite Side |
| **P11** | Deck Button 10 | Opposite Side |
| **P12** | Deck Button 11 | Opposite Side |
| **P13** | Deck Button 12 | Opposite Side |
| **P14** | EC11 #1 Switch Line 1 | Rotary switch terminal |
| **P15** | EC11 #1 Switch Line 2 | Rotary switch terminal |
| **P16** | EC11 #2 Switch Line 1 | Rotary switch terminal |
| **P17** | EC11 #2 Switch Line 2 | Rotary switch terminal |

---

## 2b. LED Strip (WS2812B)

| Strip Pin | Connected To | Notes |
| :--- | :--- | :--- |
| **5V** | ESP32 `5V / VBUS` | 70 µF electrolytic across 5V/GND at strip input (watch polarity) |
| **GND** | System Common `GND` | |
| **DIN** | ESP32 `GPIO 7` via series resistor | 470 Ω series resistor; place close to strip DIN |

> Data level: 3.3V GPIO vs 3.5V VIH min at 5V supply — marginal, no level shifter fitted. See `docs/kb/peripherals.md`.

---

## 3. LCD Screen Mapping (GMT020-02-8P)

| Screen Pin | Connected ESP32 Pin / Rail | Function |
| :--- | :--- | :--- |
| **GND** | System Common `GND` | Power Ground |
| **VCC** | ESP32 `5V` (or `3V3`) | Onboard 6206A 3.3V LDO handles 5V input |
| **SCL** | ESP32 `GPIO 11` | SPI Clock |
| **SDA** | ESP32 `GPIO 12` | SPI MOSI |
| **RST** | ESP32 `GPIO 8` | Hardware Reset |
| **DC** | ESP32 `GPIO 10` | Data/Command Register Select |
| **CS** | ESP32 `GPIO 9` | SPI Chip Select |
| **BL** | ESP32 `3V3` | Backlight always on. Floating BL = backlight OFF (bench 2026-10-04). Future: GPIO14 PWM after current check |

> Pin map bench-verified 2026-10-04 (color cycle correct, SPI mode 0/3 both OK, no inversion/BGR fix needed beyond INVON).

---

## 4. C++ Firmware Definitions

```cpp
// PCF8575 / I2C Configuration
#define I2C_SDA_PIN         1
#define I2C_SCL_PIN         6
#define PCF_INT_PIN        13

// ST7789 LCD SPI Configuration
#define TFT_SCL_PIN        11
#define TFT_SDA_PIN        12
#define TFT_CS_PIN          9
#define TFT_DC_PIN         10
#define TFT_RST_PIN         8

// Addressable LED Strip
#define LED_DATA_PIN        7

// EC11 Quadrature Encoders (Direct Interrupts)
#define ENC1_PHASE_A_PIN    2
#define ENC1_PHASE_B_PIN    3
#define ENC2_PHASE_A_PIN    4
#define ENC2_PHASE_B_PIN    5

// PCF8575 Bitmask Definitions
#define PCF_MASK_KEYS_LOW   0x00FF // P00 - P07 (Deck Buttons 1 to 8)
#define PCF_MASK_KEYS_HIGH  0x0F00 // P10 - P13 (Deck Buttons 9 to 12)
#define PCF_MASK_ENC1_SW    0x3000 // P14 - P15 (EC11 #1 Switch Lines)
#define PCF_MASK_ENC2_SW    0xC000 // P16 - P17 (EC11 #2 Switch Lines)