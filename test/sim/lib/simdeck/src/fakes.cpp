// Bodies of the host fakes. Each forwards to a model in sim_board.h.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "Adafruit_NeoPixel.h"
#include "Adafruit_ST7789.h"
#include "Arduino.h"
#include "ESPmDNS.h"
#include "Preferences.h"
#include "SPI.h"
#include "USB.h"
#include "USBHIDConsumerControl.h"
#include "USBHIDKeyboard.h"
#include "WiFi.h"
#include "WiFiManager.h"
#include "Wire.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_random.h"
#include "sim_board.h"

HWCDC Serial;
TwoWire Wire;
SPIClass SPI;
ESPUSB USB;
WiFiClass WiFi;
MDNSResponder MDNS;

// --- time, GPIO, LEDC ---

uint32_t millis() { return board().clock.millis(); }
uint32_t micros() { return board().clock.micros(); }
void delay(uint32_t ms) { board().clock.advanceMs(ms); }
void delayMicroseconds(uint32_t us) { board().clock.advanceUs(us); }

void pinMode(uint8_t pin, uint8_t mode) {
  if (pin >= SimGpio::kPins) return;
  SimGpio &g = board().gpio;
  g.mode[pin] = mode == OUTPUT ? SimGpio::Out : (mode == INPUT_PULLUP ? SimGpio::InPullup : SimGpio::In);
}
int digitalRead(uint8_t pin) { return board().gpio.read(pin); }
void digitalWrite(uint8_t pin, uint8_t val) {
  if (pin < SimGpio::kPins) board().gpio.out[pin] = val != LOW;
}
bool ledcAttach(uint8_t pin, uint32_t, uint8_t) { return pin < SimGpio::kPins; }
bool ledcWrite(uint8_t pin, uint32_t duty) {
  if (pin >= SimGpio::kPins) return false;
  board().ledcDuty[pin] = duty;
  return true;
}

esp_err_t gpio_set_drive_capability(gpio_num_t pin, gpio_drive_cap_t cap) {
  if (pin < 0 || pin >= SimGpio::kPins) return ESP_FAIL;
  board().driveCap[pin] = cap;
  return ESP_OK;
}
esp_err_t gpio_pullup_en(gpio_num_t pin) {
  if (pin < 0 || pin >= SimGpio::kPins) return ESP_FAIL;
  board().gpio.pullup[pin] = true;
  return ESP_OK;
}

esp_reset_reason_t esp_reset_reason() { return board().resetReason; }

uint32_t esp_random() {
  uint32_t &x = board().rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

// --- USB CDC serial ---

void HWCDC::begin(uint32_t baud) { board().serial.baud = baud; }
void HWCDC::setTxTimeoutMs(uint32_t) {}
int HWCDC::available() { return static_cast<int>(board().serial.rx.size()); }
int HWCDC::read() {
  SimSerialPort &s = board().serial;
  if (s.rx.empty()) return -1;
  int b = s.rx.front();
  s.rx.pop_front();
  return b;
}
size_t HWCDC::write(uint8_t b) {
  board().serial.tx.push_back(b);
  return 1;
}
size_t HWCDC::write(const uint8_t *buf, size_t n) {
  board().serial.tx.insert(board().serial.tx.end(), buf, buf + n);
  return n;
}
size_t HWCDC::print(const char *s) { return write(reinterpret_cast<const uint8_t *>(s), strlen(s)); }
size_t HWCDC::println(const char *s) { return print(s) + print("\r\n"); }
size_t HWCDC::printf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n < 0) return 0;
  return print(buf);
}

// --- I2C ---

bool TwoWire::begin(int sdaPin, int sclPin, uint32_t freq) {
  sda = sdaPin;
  scl = sclPin;
  frequency = freq;
  return true;
}
void TwoWire::beginTransmission(uint8_t address) {
  txAddr_ = address;
  tx_.clear();
}
size_t TwoWire::write(uint8_t b) {
  tx_.push_back(b);
  return 1;
}
uint8_t TwoWire::endTransmission(bool) { return board().i2cWrite(txAddr_, tx_.data(), tx_.size()); }
uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity) {
  rx_.assign(quantity, 0);
  rxPos_ = 0;
  size_t n = board().i2cRead(address, rx_.data(), quantity);
  rx_.resize(n);
  return static_cast<uint8_t>(n);
}
int TwoWire::available() { return static_cast<int>(rx_.size() - rxPos_); }
int TwoWire::read() { return rxPos_ < rx_.size() ? rx_[rxPos_++] : -1; }

void SPIClass::begin(int8_t sckPin, int8_t misoPin, int8_t mosiPin, int8_t ssPin) {
  sck = sckPin;
  miso = misoPin;
  mosi = mosiPin;
  ss = ssPin;
}

// --- USB device ---

bool ESPUSB::begin() { return true; }
void ESPUSB::onEvent(arduino_usb_event_t event, sim_usb_handler_t handler) {
  if (event == ARDUINO_USB_STOPPED_EVENT) board().usbStoppedHandler = handler;
}

// US-layout ASCII -> HID usage, from the core's _asciimap: letters, digits, space, enter.
// 0x80 flag = needs shift. Other characters map to 0 (not sent), as in the core for unmapped.
static uint8_t asciiUsage(uint8_t c) {
  if (c >= 'a' && c <= 'z') return static_cast<uint8_t>(0x04 + (c - 'a'));
  if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(0x80 | (0x04 + (c - 'A')));
  if (c >= '1' && c <= '9') return static_cast<uint8_t>(0x1E + (c - '1'));
  if (c == '0') return 0x27;
  if (c == ' ') return 0x2C;
  if (c == '\n') return 0x28;
  return 0;
}

void USBHIDKeyboard::begin() {}
void USBHIDKeyboard::send() { board().host.kb.push_back(report_); }

size_t USBHIDKeyboard::pressRaw(uint8_t k) {
  if (k >= 0xE0 && k < 0xE8) {
    report_.mods |= static_cast<uint8_t>(1u << (k - 0xE0));
  } else if (k && k < 0xA5) {
    bool present = false;
    for (uint8_t s : report_.keys)
      if (s == k) present = true;
    if (!present) {
      int i = 0;
      for (; i < 6; i++)
        if (report_.keys[i] == 0) {
          report_.keys[i] = k;
          break;
        }
      if (i == 6) return 0;
    }
  } else if (report_.mods == 0) {
    return 0;
  }
  send();
  return 1;
}

size_t USBHIDKeyboard::releaseRaw(uint8_t k) {
  if (k >= 0xE0 && k < 0xE8) {
    report_.mods &= static_cast<uint8_t>(~(1u << (k - 0xE0)));
  } else if (k && k < 0xA5) {
    for (uint8_t &s : report_.keys)
      if (s == k) s = 0;
  } else {
    return 0;
  }
  send();
  return 1;
}

size_t USBHIDKeyboard::press(uint8_t k) {
  if (k >= 0x88) {
    k = static_cast<uint8_t>(k - 0x88);
  } else if (k >= 0x80) {
    report_.mods |= static_cast<uint8_t>(1u << (k - 0x80));
    k = 0;
  } else {
    k = asciiUsage(k);
    if (!k) return 0;
    if (k & 0x80) {
      report_.mods |= 0x02;
      k &= 0x7F;
    }
  }
  return pressRaw(k);
}

size_t USBHIDKeyboard::release(uint8_t k) {
  if (k >= 0x88) {
    k = static_cast<uint8_t>(k - 0x88);
  } else if (k >= 0x80) {
    report_.mods &= static_cast<uint8_t>(~(1u << (k - 0x80)));
    k = 0;
  } else {
    k = asciiUsage(k);
    if (!k) return 0;
    if (k & 0x80) {
      report_.mods &= static_cast<uint8_t>(~0x02);
      k &= 0x7F;
    }
  }
  return releaseRaw(k);
}

void USBHIDKeyboard::releaseAll() {
  report_ = KbReport{0, {0, 0, 0, 0, 0, 0}};
  send();
}

void USBHIDConsumerControl::begin() {}
size_t USBHIDConsumerControl::press(uint16_t k) {
  board().host.consumer.push_back(k);
  return 1;
}
size_t USBHIDConsumerControl::release() {
  board().host.consumer.push_back(0);
  return 1;
}

// --- NVS ---

bool Preferences::begin(const char *name, bool readOnly) {
  if (readOnly && !board().nvs.hasNamespace(name)) return false;
  ns_ = name;
  readOnly_ = readOnly;
  open_ = true;
  if (!readOnly) board().nvs.ns[ns_];  // creating the namespace, as nvs_open(READWRITE) does
  return true;
}
void Preferences::end() { open_ = false; }
bool Preferences::isKey(const char *key) {
  if (!open_) return false;
  return board().nvs.ns[ns_].count(key) != 0;
}
size_t Preferences::getBytes(const char *key, void *buf, size_t maxLen) {
  if (!open_) return 0;
  auto &m = board().nvs.ns[ns_];
  auto it = m.find(key);
  if (it == m.end() || it->second.size() > maxLen) return 0;
  memcpy(buf, it->second.data(), it->second.size());
  return it->second.size();
}
size_t Preferences::putBytes(const char *key, const void *value, size_t len) {
  if (!open_ || readOnly_) return 0;
  const uint8_t *p = static_cast<const uint8_t *>(value);
  board().nvs.ns[ns_][key].assign(p, p + len);
  return len;
}

// --- Wi-Fi ---

String IPAddress::toString() const {
  char b[16];
  snprintf(b, sizeof b, "%u.%u.%u.%u", o_[0], o_[1], o_[2], o_[3]);
  return String(b);
}
bool WiFiClass::mode(wifi_mode_t m) {
  board().wifi.mode = m;
  return true;
}
bool WiFiClass::setTxPower(wifi_power_t p) {
  board().wifi.txPower = p;
  return true;
}
bool WiFiClass::disconnect(bool) {
  board().wifi.connected = false;
  return true;
}
wl_status_t WiFiClass::status() { return board().wifi.connected ? WL_CONNECTED : WL_DISCONNECTED; }
IPAddress WiFiClass::localIP() { return IPAddress(board().wifi.ip); }
int8_t WiFiClass::RSSI() { return board().wifi.rssi; }

WiFiManagerParameter::WiFiManagerParameter(const char *custom) : custom_(custom) {}
WiFiManagerParameter::WiFiManagerParameter(const char *id, const char *label, const char *defaultValue,
                                           int length)
    : id_(id), label_(label) {
  setValue(defaultValue, length);
}
void WiFiManagerParameter::setValue(const char *v, int length) {
  if (!id_) return;  // custom HTML parameters have no value (as in the library)
  length_ = length;
  value_.assign(v ? v : "");
  if (static_cast<int>(value_.size()) > length) value_.resize(static_cast<size_t>(length));
}

WiFiManager::WiFiManager() { board().wifi.manager = this; }
bool WiFiManager::addParameter(WiFiManagerParameter *p) {
  params.push_back(p);
  return true;
}
bool WiFiManager::autoConnect(const char *ssid, const char *pass) {
  SimWifi &w = board().wifi;
  w.apSsid = ssid;
  w.apPass = pass;
  if (w.joinOk) {
    w.connected = true;
    return true;
  }
  w.portalActive = w.portalOk;
  w.serverUp = w.portalOk;  // startConfigPortal -> setupConfigPortal allocates the server
  return false;
}
bool WiFiManager::getConfigPortalActive() { return board().wifi.portalActive; }

// WiFiManager 2.0.17 shutdownConfigPortal() (WiFiManager.cpp:957-985): returns early only if
// webPortalActive, then calls server->handleClient() unconditionally. With the server already
// freed (or never allocated) that is a null dereference: LoadProhibited at 0x0 on the chip
// (core dump 2026-11-08). The sim aborts the device the same way.
static bool shutdownConfigPortal() {
  SimWifi &w = board().wifi;
  if (w.webPortal) return false;
  if (!w.serverUp) {
    fprintf(stderr, "SIM CRASH: WiFiManager::shutdownConfigPortal server->handleClient() on null server\n");
    abort();
  }
  w.serverUp = false;  // server->stop(); server.reset();
  if (!w.portalActive) return false;
  w.portalActive = false;  // dnsServer stop, soft AP off
  return true;
}
void WiFiManager::stopWebPortal() {  // WiFiManager.cpp:569
  SimWifi &w = board().wifi;
  if (!w.portalActive && !w.webPortal) return;
  w.webPortal = false;
  shutdownConfigPortal();
}
bool WiFiManager::stopConfigPortal() {  // non-blocking mode: straight to shutdownConfigPortal
  if (blocking_) return true;
  return shutdownConfigPortal();
}
bool WiFiManager::process() { return true; }
void WiFiManager::startWebPortal() {  // WiFiManager.cpp startWebPortal
  SimWifi &w = board().wifi;
  if (w.portalActive || w.webPortal) return;
  w.serverUp = true;
  w.webPortal = true;
}

bool MDNSResponder::begin(const char *hostName) {
  board().wifi.mdnsName = hostName;
  return true;
}
void MDNSResponder::end() { board().wifi.mdnsName.clear(); }

// --- LCD ---
// Rasterizers below are ports of Adafruit GFX 1.12.x (Adafruit_GFX.cpp, BSD) writing to
// board().lcd.fb with clipping to the 320x240 panel.

#include "sim/glcdfont.h"

static uint8_t font5x7ColumnForSim(unsigned char c, int col) { return font[c * 5 + col]; }

static void putPx(int16_t x, int16_t y, uint16_t c) {
  if (x < 0 || y < 0 || x >= SimLcd::kW || y >= SimLcd::kH) return;
  board().lcd.fb[y][x] = c;
}
static void hLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
  for (int16_t i = 0; i < w; i++) putPx(static_cast<int16_t>(x + i), y, c);
}
static void vLine(int16_t x, int16_t y, int16_t h, uint16_t c) {
  for (int16_t i = 0; i < h; i++) putPx(x, static_cast<int16_t>(y + i), c);
}
static void rectFill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  for (int16_t j = 0; j < h; j++) hLine(x, static_cast<int16_t>(y + j), w, c);
}
static void swap16(int16_t &a, int16_t &b) {
  int16_t t = a;
  a = b;
  b = t;
}

static void lineDraw(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
  const bool steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) {
    swap16(x0, y0);
    swap16(x1, y1);
  }
  if (x0 > x1) {
    swap16(x0, x1);
    swap16(y0, y1);
  }
  const int16_t dx = x1 - x0, dy = abs(y1 - y0);
  int16_t err = dx / 2;
  const int16_t ystep = y0 < y1 ? 1 : -1;
  for (; x0 <= x1; x0++) {
    if (steep) putPx(y0, x0, c);
    else putPx(x0, y0, c);
    err -= dy;
    if (err < 0) {
      y0 += ystep;
      err += dx;
    }
  }
}

static void circleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corner, uint16_t c) {
  int16_t f = 1 - r, ddFx = 1, ddFy = -2 * r, x = 0, y = r;
  while (x < y) {
    if (f >= 0) {
      y--;
      ddFy += 2;
      f += ddFy;
    }
    x++;
    ddFx += 2;
    f += ddFx;
    if (corner & 0x4) {
      putPx(x0 + x, y0 + y, c);
      putPx(x0 + y, y0 + x, c);
    }
    if (corner & 0x2) {
      putPx(x0 + x, y0 - y, c);
      putPx(x0 + y, y0 - x, c);
    }
    if (corner & 0x8) {
      putPx(x0 - y, y0 + x, c);
      putPx(x0 - x, y0 + y, c);
    }
    if (corner & 0x1) {
      putPx(x0 - y, y0 - x, c);
      putPx(x0 - x, y0 - y, c);
    }
  }
}

static void circleFillHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta, uint16_t c) {
  int16_t f = 1 - r, ddFx = 1, ddFy = -2 * r, x = 0, y = r, pxx = x, pyy = y;
  delta++;
  while (x < y) {
    if (f >= 0) {
      y--;
      ddFy += 2;
      f += ddFy;
    }
    x++;
    ddFx += 2;
    f += ddFx;
    if (x < (y + 1)) {
      if (corners & 1) vLine(x0 + x, y0 - y, 2 * y + delta, c);
      if (corners & 2) vLine(x0 - x, y0 - y, 2 * y + delta, c);
    }
    if (y != pyy) {
      if (corners & 1) vLine(x0 + pyy, y0 - pxx, 2 * pxx + delta, c);
      if (corners & 2) vLine(x0 - pyy, y0 - pxx, 2 * pxx + delta, c);
      pyy = y;
    }
    pxx = x;
  }
}

static void triangleFill(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c) {
  int16_t a, b, y, last;
  if (y0 > y1) {
    swap16(y0, y1);
    swap16(x0, x1);
  }
  if (y1 > y2) {
    swap16(y2, y1);
    swap16(x2, x1);
  }
  if (y0 > y1) {
    swap16(y0, y1);
    swap16(x0, x1);
  }
  if (y0 == y2) {
    a = b = x0;
    if (x1 < a) a = x1;
    else if (x1 > b) b = x1;
    if (x2 < a) a = x2;
    else if (x2 > b) b = x2;
    hLine(a, y0, b - a + 1, c);
    return;
  }
  const int16_t dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1;
  int32_t sa = 0, sb = 0;
  last = (y1 == y2) ? y1 : static_cast<int16_t>(y1 - 1);
  for (y = y0; y <= last; y++) {
    a = x0 + sa / dy01;
    b = x0 + sb / dy02;
    sa += dx01;
    sb += dx02;
    if (a > b) swap16(a, b);
    hLine(a, y, b - a + 1, c);
  }
  sa = static_cast<int32_t>(dx12) * (y - y1);
  sb = static_cast<int32_t>(dx02) * (y - y0);
  for (; y <= y2; y++) {
    a = x1 + sa / dy12;
    b = x0 + sb / dy02;
    sa += dx12;
    sb += dx02;
    if (a > b) swap16(a, b);
    hLine(a, y, b - a + 1, c);
  }
}

// drawChar: classic 5x7 (opaque when bg != fg) or custom GFX glyph (transparent, y = baseline).
static void drawGlyph(const GFXfont *font, int16_t x, int16_t y, unsigned char ch, uint16_t fg, uint16_t bg,
                      uint8_t sz) {
  if (!font) {
    if (ch >= 176) ch++;  // Adafruit "classic" charset behaviour (!_cp437)
    for (int8_t i = 0; i < 5; i++) {
      uint8_t line = font5x7ColumnForSim(ch, i);
      for (int8_t j = 0; j < 8; j++, line >>= 1) {
        if (line & 1) rectFill(x + i * sz, y + j * sz, sz, sz, fg);
        else if (bg != fg) rectFill(x + i * sz, y + j * sz, sz, sz, bg);
      }
    }
    if (bg != fg) rectFill(x + 5 * sz, y, sz, 8 * sz, bg);
    return;
  }
  const GFXglyph &g = font->glyph[ch - font->first];
  uint16_t bo = g.bitmapOffset;
  uint8_t bits = 0, bit = 0;
  for (uint8_t yy = 0; yy < g.height; yy++)
    for (uint8_t xx = 0; xx < g.width; xx++) {
      if (!(bit++ & 7)) bits = font->bitmap[bo++];
      if (bits & 0x80) rectFill(x + (g.xOffset + xx) * sz, y + (g.yOffset + yy) * sz, sz, sz, fg);
      bits <<= 1;
    }
}

bool SimLcd::saveBmp(const char *path) const {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  const uint32_t dataBytes = static_cast<uint32_t>(kW) * kH * 3;  // 960 B/row: already 4-byte aligned
  const uint32_t fileBytes = 54 + dataBytes;
  uint8_t h[54] = {'B', 'M'};
  auto put32 = [&h](int off, uint32_t v) {
    for (int i = 0; i < 4; i++) h[off + i] = static_cast<uint8_t>(v >> (8 * i));
  };
  put32(2, fileBytes);
  put32(10, 54);       // pixel data offset
  put32(14, 40);       // BITMAPINFOHEADER
  put32(18, kW);
  put32(22, kH);       // positive = bottom-up rows
  h[26] = 1;           // planes
  h[28] = 24;          // bits per pixel
  put32(34, dataBytes);
  put32(38, 2835);
  put32(42, 2835);
  bool ok = fwrite(h, 1, sizeof h, f) == sizeof h;
  uint8_t row[kW * 3];
  for (int y = kH - 1; ok && y >= 0; y--) {
    for (int x = 0; x < kW; x++) {
      const uint16_t c = fb[y][x];
      const uint8_t r5 = c >> 11, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
      row[x * 3 + 0] = static_cast<uint8_t>(b5 << 3 | b5 >> 2);
      row[x * 3 + 1] = static_cast<uint8_t>(g6 << 2 | g6 >> 4);
      row[x * 3 + 2] = static_cast<uint8_t>(r5 << 3 | r5 >> 2);
    }
    ok = fwrite(row, 1, sizeof row, f) == sizeof row;
  }
  return fclose(f) == 0 && ok;
}

Adafruit_ST7789::Adafruit_ST7789(SPIClass *, int8_t, int8_t, int8_t) {}
void Adafruit_ST7789::init(uint16_t, uint16_t, uint8_t) { board().lcd.inited = true; }
void Adafruit_ST7789::setSPISpeed(uint32_t hz) { board().lcd.spiHz = hz; }
void Adafruit_ST7789::setRotation(uint8_t r) { board().lcd.rotation = r; }
void Adafruit_ST7789::setTextWrap(bool) {}
void Adafruit_ST7789::enableSleep(bool on) { board().lcd.asleep = on; }
void Adafruit_ST7789::fillScreen(uint16_t c) {
  board().lcd.fills++;
  rectFill(0, 0, SimLcd::kW, SimLcd::kH, c);
}
void Adafruit_ST7789::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  board().lcd.fills++;
  board().lcd.rects.push_back({x, y, w, h, c});
  rectFill(x, y, w, h, c);
}
void Adafruit_ST7789::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  hLine(x, y, w, c);
  hLine(x, y + h - 1, w, c);
  vLine(x, y, h, c);
  vLine(x + w - 1, y, h, c);
}
void Adafruit_ST7789::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
  board().lcd.rounds.push_back({x, y, w, h, c});
  const int16_t maxR = (w < h ? w : h) / 2;
  if (r > maxR) r = maxR;
  rectFill(x + r, y, w - 2 * r, h, c);
  circleFillHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, c);
  circleFillHelper(x + r, y + r, r, 2, h - 2 * r - 1, c);
}
void Adafruit_ST7789::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                                   uint16_t c) {
  triangleFill(x0, y0, x1, y1, x2, y2, c);
}
void Adafruit_ST7789::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
  board().lcd.hlines.push_back({x, y, w, 1, c});
  hLine(x, y, w, c);
}
void Adafruit_ST7789::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
  lineDraw(x0, y0, x1, y1, c);
}
void Adafruit_ST7789::drawCircleHelper(int16_t x, int16_t y, int16_t r, uint8_t corner, uint16_t c) {
  circleHelper(x, y, r, corner, c);
}
void Adafruit_ST7789::fillCircle(int16_t x, int16_t y, int16_t r, uint16_t c) {
  board().lcd.circles.push_back({x, y, r, r, c});
  vLine(x, y - r, 2 * r + 1, c);
  circleFillHelper(x, y, r, 3, 0, c);
}
size_t Adafruit_ST7789::print(const char *s) {
  board().lcd.texts.push_back({cx_, cy_, textSize_, fg_, s, font_ != nullptr, font_});
  // Adafruit_GFX::write(): cursor update and glyph placement (setTextWrap is not modelled).
  for (const char *p = s; *p; p++) {
    const unsigned char ch = static_cast<unsigned char>(*p);
    if (!font_) {
      if (ch == '\n') {
        cx_ = 0;
        cy_ += textSize_ * 8;
      } else if (ch != '\r') {
        drawGlyph(nullptr, cx_, cy_, ch, fg_, bg_, textSize_);
        cx_ += textSize_ * 6;
      }
    } else if (ch == '\n') {
      cx_ = 0;
      cy_ += textSize_ * font_->yAdvance;
    } else if (ch != '\r' && ch >= font_->first && ch <= font_->last) {
      const GFXglyph &g = font_->glyph[ch - font_->first];
      if (g.width > 0 && g.height > 0) drawGlyph(font_, cx_, cy_, ch, fg_, bg_, textSize_);
      cx_ += g.xAdvance * textSize_;
    }
  }
  return strlen(s);
}

// --- LEDs ---

Adafruit_NeoPixel::Adafruit_NeoPixel(uint16_t n, int16_t pin, neoPixelType) {
  SimLeds &l = board().leds;
  l.count = n;
  l.pin = pin;
  l.pending.assign(n, SimRgb{0, 0, 0});
}
void Adafruit_NeoPixel::begin() {}
void Adafruit_NeoPixel::setBrightness(uint8_t b) { board().leds.brightness = b; }
void Adafruit_NeoPixel::clear() {
  SimLeds &l = board().leds;
  l.pending.assign(l.count, SimRgb{0, 0, 0});
}
void Adafruit_NeoPixel::show() {
  SimLeds &l = board().leds;
  l.shown = l.pending;
  l.shows++;
}
void Adafruit_NeoPixel::setPixelColor(uint16_t i, uint8_t r, uint8_t g, uint8_t b) {
  SimLeds &l = board().leds;
  if (i < l.pending.size()) l.pending[i] = SimRgb{r, g, b};
}

// --- PCNT (configuration only in plan 1) ---

struct sim_pcnt_unit_t {
  int index;
};
struct sim_pcnt_chan_t {
  int unit, index;
};
static sim_pcnt_unit_t gUnits[SimPcnt::kUnits] = {{0}, {1}, {2}, {3}};
static sim_pcnt_chan_t gChans[SimPcnt::kUnits][2] = {{{0, 0}, {0, 1}}, {{1, 0}, {1, 1}},
                                                     {{2, 0}, {2, 1}}, {{3, 0}, {3, 1}}};

static SimPcntUnit &unitOf(pcnt_unit_handle_t u) { return board().pcnt.unit[u->index]; }
static SimPcntChannel &chanOf(pcnt_channel_handle_t c) { return board().pcnt.unit[c->unit].ch[c->index]; }

esp_err_t pcnt_new_unit(const pcnt_unit_config_t *config, pcnt_unit_handle_t *ret_unit) {
  SimPcnt &p = board().pcnt;
  if (p.failNewUnit) return ESP_FAIL;
  for (int i = 0; i < SimPcnt::kUnits; i++) {
    if (p.unit[i].used) continue;
    p.unit[i] = SimPcntUnit{};
    p.unit[i].used = true;
    p.unit[i].low = config->low_limit;
    p.unit[i].high = config->high_limit;
    *ret_unit = &gUnits[i];
    return ESP_OK;
  }
  return ESP_FAIL;
}
esp_err_t pcnt_del_unit(pcnt_unit_handle_t unit) {
  if (!unit) return ESP_FAIL;
  unitOf(unit).used = false;
  return ESP_OK;
}
esp_err_t pcnt_unit_set_glitch_filter(pcnt_unit_handle_t unit, const pcnt_glitch_filter_config_t *config) {
  unitOf(unit).glitchNs = config->max_glitch_ns;
  return ESP_OK;
}
esp_err_t pcnt_new_channel(pcnt_unit_handle_t unit, const pcnt_chan_config_t *config,
                           pcnt_channel_handle_t *ret_chan) {
  SimPcntUnit &u = unitOf(unit);
  if (u.channels >= 2) return ESP_FAIL;
  int i = u.channels++;
  u.ch[i].edgePin = config->edge_gpio_num;
  u.ch[i].levelPin = config->level_gpio_num;
  *ret_chan = &gChans[unit->index][i];
  return ESP_OK;
}
esp_err_t pcnt_del_channel(pcnt_channel_handle_t) { return ESP_OK; }
esp_err_t pcnt_channel_set_edge_action(pcnt_channel_handle_t chan, pcnt_channel_edge_action_t pos,
                                       pcnt_channel_edge_action_t neg) {
  chanOf(chan).posEdge = pos;
  chanOf(chan).negEdge = neg;
  return ESP_OK;
}
esp_err_t pcnt_channel_set_level_action(pcnt_channel_handle_t chan, pcnt_channel_level_action_t high,
                                        pcnt_channel_level_action_t low) {
  chanOf(chan).highLevel = high;
  chanOf(chan).lowLevel = low;
  return ESP_OK;
}
esp_err_t pcnt_unit_add_watch_point(pcnt_unit_handle_t, int) { return ESP_OK; }
esp_err_t pcnt_unit_enable(pcnt_unit_handle_t unit) {
  if (!unit) return ESP_FAIL;
  unitOf(unit).enabled = true;
  return ESP_OK;
}
esp_err_t pcnt_unit_disable(pcnt_unit_handle_t unit) {
  if (!unit) return ESP_FAIL;
  unitOf(unit).enabled = false;
  return ESP_OK;
}
esp_err_t pcnt_unit_start(pcnt_unit_handle_t unit) {
  unitOf(unit).started = true;
  return ESP_OK;
}
esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t unit) {
  unitOf(unit).count = 0;
  return ESP_OK;
}
esp_err_t pcnt_unit_get_count(pcnt_unit_handle_t unit, int *value) {
  *value = unitOf(unit).count;
  return ESP_OK;
}
