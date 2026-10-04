#pragma once
// Pin map — see wiring.md

// PCF8575 / I2C
#define I2C_SDA_PIN         1
#define I2C_SCL_PIN         6
#define PCF_INT_PIN        13
#define PCF_I2C_ADDR     0x20

// ST7789 LCD (SPI)
#define TFT_SCL_PIN        12
#define TFT_SDA_PIN        11
#define TFT_CS_PIN          9
#define TFT_DC_PIN         10
#define TFT_RST_PIN         8

// Addressable LED strip
#define LED_DATA_PIN        7

// EC11 quadrature encoders
#define ENC1_PHASE_A_PIN    2
#define ENC1_PHASE_B_PIN    3
#define ENC2_PHASE_A_PIN    4
#define ENC2_PHASE_B_PIN    5

// PCF8575 bitmasks (active-low inputs)
#define PCF_MASK_KEYS_LOW   0x00FF  // P00-P07: deck buttons 1-8
#define PCF_MASK_KEYS_HIGH  0x0F00  // P10-P13: deck buttons 9-12
#define PCF_MASK_ENC1_SW    0x3000  // P14-P15: EC11 #1 switch
#define PCF_MASK_ENC2_SW    0xC000  // P16-P17: EC11 #2 switch
