#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_system.h"
#include "sim/clock.h"
#include "sim/gpio.h"
#include "sim/host.h"
#include "sim/lcd.h"
#include "sim/leds.h"
#include "sim/mcp23017.h"
#include "sim/nvs.h"
#include "sim/pcnt.h"
#include "sim/serial_port.h"
#include "sim/wifi.h"

typedef const char *esp_event_base_t;
typedef void (*sim_usb_handler_t)(void *arg, esp_event_base_t base, int32_t id, void *data);

// Everything on (and attached to) the simulated SAUCEDECK. One instance per process; each
// SIM_TEST runs in a fresh child process, so it always starts at power-on state.
struct SimBoard {
  SimClock clock;
  SimGpio gpio;
  SimMcp23017 mcp;
  SimHost host;
  SimNvs nvs;
  SimSerialPort serial;
  SimWifi wifi;
  SimLcd lcd;
  SimLeds leds;
  SimPcnt pcnt;
  uint32_t ledcDuty[SimGpio::kPins] = {};
  int driveCap[SimGpio::kPins] = {};
  esp_reset_reason_t resetReason = ESP_RST_POWERON;
  sim_usb_handler_t usbStoppedHandler = nullptr;
  uint32_t rng = 0x2545F491u;

  // I2C transfers as Wire sees them. Return values follow Arduino TwoWire::endTransmission:
  // 0 = ACK, 2 = address NACK.
  uint8_t i2cWrite(uint8_t addr, const uint8_t *data, size_t n);
  size_t i2cRead(uint8_t addr, uint8_t *out, size_t n);
};

SimBoard &board();
