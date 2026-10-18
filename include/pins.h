#pragma once
// Pin map — see wiring.md. Silkscreen number on the SuperMini = GPIO number.

// MCP23017 on I2C. INTA/INTB not connected: firmware polls.
#define I2C_SDA_PIN         8
#define I2C_SCL_PIN         7
#define MCP_I2C_ADDR     0x27  // Waveshare board, A0-A2 pads open (unverified until bus scan)

// ST7789 LCD (SPI, write-only). GPIO12/11 = FSPICLK/FSPID on IO_MUX.
#define TFT_SCL_PIN        12  // SCK
#define TFT_SDA_PIN        11  // MOSI
#define TFT_CS_PIN         10
#define TFT_DC_PIN         13
#define TFT_RST_PIN        44  // U0RXD, free: Serial runs over USB CDC
#define TFT_BL_PIN          9  // check BL current <= 20 mA before driving from GPIO

// Addressable LED strip
#define LED_DATA_PIN        6

// EC11 quadrature encoders. Knob 1 = switch GPB4, knob 2 = switch GPB5.
// Bench-verified 2026-10-18: knob 1 rotation is on GPIO3/4, knob 2 on GPIO1/2.
#define ENC1_PHASE_A_PIN    3  // strapping pin (JTAG source), input only, safe with default eFuses
#define ENC1_PHASE_B_PIN    4
#define ENC2_PHASE_A_PIN    1
#define ENC2_PHASE_B_PIN    2
