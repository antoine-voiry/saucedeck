#pragma once
// Arduino-ESP32 core 3.x subset used by src/. Behaviour lives in sim_board.h models.
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#ifndef PROGMEM
#define PROGMEM  // sim: flash-resident data is plain memory
#endif
#include "esp_err.h"
#include "esp_system.h"

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05

typedef bool boolean;

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t val);
bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
bool ledcWrite(uint8_t pin, uint32_t duty);

// Minimal Arduino String (only what src/ uses: construction + c_str()).
class String {
 public:
  String(const char *s = "") : s_(s) {}
  explicit String(const std::string &s) : s_(s) {}
  const char *c_str() const { return s_.c_str(); }

 private:
  std::string s_;
};

// USB CDC serial (ARDUINO_USB_CDC_ON_BOOT=1). Bytes go to board().serial.
class HWCDC {
 public:
  void begin(uint32_t baud);
  void setTxTimeoutMs(uint32_t ms);
  int available();
  int read();
  size_t write(uint8_t b);
  size_t write(const uint8_t *buf, size_t n);
  size_t print(const char *s);
  size_t println(const char *s = "");
  size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};
extern HWCDC Serial;
