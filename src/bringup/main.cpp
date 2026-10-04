// SAUCEDECK hardware bring-up: guided wiring test over USB serial.
// Flash with `pio run -e bringup -t upload`, then `pio device monitor`.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "bringup.h"
#include "pins.h"
#include "quad_decoder.h"

static const uint32_t kStepTimeoutMs = 20000;
static const uint32_t kSettleMs = 400;

static const BringupStep kSteps[] = {
    {StepKind::Button, 1u << 0, 0, 0, "Press deck button 1"},
    {StepKind::Button, 1u << 1, 0, 0, "Press deck button 2"},
    {StepKind::Button, 1u << 2, 0, 0, "Press deck button 3"},
    {StepKind::Button, 1u << 3, 0, 0, "Press deck button 4"},
    {StepKind::Button, 1u << 4, 0, 0, "Press deck button 5"},
    {StepKind::Button, 1u << 5, 0, 0, "Press deck button 6"},
    {StepKind::Button, 1u << 6, 0, 0, "Press deck button 7"},
    {StepKind::Button, 1u << 7, 0, 0, "Press deck button 8"},
    {StepKind::Button, 1u << 8, 0, 0, "Press deck button 9"},
    {StepKind::Button, 1u << 9, 0, 0, "Press deck button 10"},
    {StepKind::Button, 1u << 10, 0, 0, "Press deck button 11"},
    {StepKind::Button, 1u << 11, 0, 0, "Press deck button 12"},
    {StepKind::Button, PCF_MASK_ENC1_SW, 0, 0, "Push encoder 1 knob"},
    {StepKind::Button, PCF_MASK_ENC2_SW, 0, 0, "Push encoder 2 knob"},
    {StepKind::EncoderTurn, 0, 0, +1, "Turn encoder 1 ONE click clockwise"},
    {StepKind::EncoderTurn, 0, 0, -1, "Turn encoder 1 ONE click counter-clockwise"},
    {StepKind::EncoderTurn, 0, 1, +1, "Turn encoder 2 ONE click clockwise"},
    {StepKind::EncoderTurn, 0, 1, -1, "Turn encoder 2 ONE click counter-clockwise"},
};
static const size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

static const uint8_t kEncPins[2][2] = {
    {ENC1_PHASE_A_PIN, ENC1_PHASE_B_PIN},
    {ENC2_PHASE_A_PIN, ENC2_PHASE_B_PIN},
};

// ---------- serial helpers ----------

static void drainSerial() {
  while (Serial.available()) Serial.read();
}

static char waitKey() {
  drainSerial();
  while (!Serial.available()) delay(10);
  char c = Serial.read();
  drainSerial();
  return c;
}

static bool askYesNo(const char *q) {
  Serial.printf("%s [y/n] ", q);
  for (;;) {
    char c = waitKey();
    if (c == 'y' || c == 'Y') { Serial.println("y"); return true; }
    if (c == 'n' || c == 'N') { Serial.println("n"); return false; }
  }
}

// ---------- PCF8575 ----------

static bool pcfRead(uint16_t &out) {
  if (Wire.requestFrom((uint8_t)PCF_I2C_ADDR, (uint8_t)2) != 2) return false;
  uint16_t lo = Wire.read();
  uint16_t hi = Wire.read();
  out = (hi << 8) | lo;
  return true;
}

static bool pcfAllInputs() {
  Wire.beginTransmission(PCF_I2C_ADDR);
  Wire.write(0xFF);
  Wire.write(0xFF);
  return Wire.endTransmission() == 0;
}

// ---------- static checks ----------

static bool checkI2C() {
  Serial.println("\n== I2C / PCF8575 ==");
  Wire.beginTransmission(PCF_I2C_ADDR);
  if (Wire.endTransmission() == 0 && pcfAllInputs()) {
    Serial.printf("PASS: PCF8575 ACK at 0x%02X\n", PCF_I2C_ADDR);
    return true;
  }
  Serial.printf("FAIL: no ACK at 0x%02X. Scanning bus...\n", PCF_I2C_ADDR);
  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X\n", a);
      found++;
    }
  }
  if (found)
    Serial.println("  -> PCF on other address: check A0-A2 straps or PCF_I2C_ADDR in pins.h");
  else
    Serial.println("  -> nothing on bus: check SDA=GPIO1, SCL=GPIO6 (swapped?), 3V3, GND, pull-ups");
  return false;
}

static void checkIdle(bool pcfOk) {
  Serial.println("\n== Idle levels (touch nothing) ==");
  delay(200);
  if (pcfOk) {
    uint16_t v;
    if (!pcfRead(v)) {
      Serial.println("FAIL: PCF read error");
    } else if (v == 0xFFFF) {
      Serial.println("PASS: all 16 PCF inputs HIGH");
    } else {
      for (uint8_t i = 0; i < 16; i++)
        if (!(v & (1u << i)))
          Serial.printf("FAIL: %s stuck LOW - short to GND, wrong switch wiring, or held\n", pcfPinName(i));
    }
    delay(5);
    Serial.println(digitalRead(PCF_INT_PIN) ? "PASS: INT (GPIO13) idle HIGH"
                                            : "FAIL: INT (GPIO13) stuck LOW - short to GND?");
  }
  for (int e = 0; e < 2; e++)
    Serial.printf("INFO: encoder %d idle A=%d B=%d (GPIO%u/GPIO%u)\n", e + 1,
                  digitalRead(kEncPins[e][0]), digitalRead(kEncPins[e][1]), kEncPins[e][0],
                  kEncPins[e][1]);
}

// ---------- ST7789 (minimal driver, write-only SPI) ----------

// Runtime-tunable so the bench can try SPI mode / pin variants.
static uint8_t tftCs = TFT_CS_PIN, tftDc = TFT_DC_PIN;
static uint8_t tftSck = TFT_SCL_PIN, tftMosi = TFT_SDA_PIN;
static SPISettings tftSpi(20000000, MSBFIRST, SPI_MODE0);

static void tftCmd(uint8_t cmd, const uint8_t *data = nullptr, size_t n = 0) {
  SPI.beginTransaction(tftSpi);
  digitalWrite(tftCs, LOW);
  digitalWrite(tftDc, LOW);
  SPI.write(cmd);
  if (n) {
    digitalWrite(tftDc, HIGH);
    SPI.writeBytes(data, n);
  }
  digitalWrite(tftCs, HIGH);
  SPI.endTransaction();
}

static void tftInit() {
  pinMode(tftCs, OUTPUT);
  pinMode(tftDc, OUTPUT);
  pinMode(TFT_RST_PIN, OUTPUT);
  digitalWrite(tftCs, HIGH);
  SPI.end();  // begin() is a no-op once initialized; re-init so pin variants apply
  SPI.begin(tftSck, -1, tftMosi, -1);  // no MISO: GPIO13 belongs to PCF INT

  digitalWrite(TFT_RST_PIN, HIGH); delay(10);
  digitalWrite(TFT_RST_PIN, LOW);  delay(20);
  digitalWrite(TFT_RST_PIN, HIGH); delay(150);

  static const uint8_t colmod = 0x55, madctl = 0x00;
  tftCmd(0x01); delay(150);          // SWRESET
  tftCmd(0x11); delay(120);          // SLPOUT
  tftCmd(0x3A, &colmod, 1);          // COLMOD 16-bit
  tftCmd(0x36, &madctl, 1);          // MADCTL
  tftCmd(0x21);                      // INVON (IPS panels)
  tftCmd(0x13);                      // NORON
  tftCmd(0x29); delay(20);           // DISPON
}

static void tftFill(uint16_t color) {
  static const uint8_t caset[] = {0, 0, 0, 239}, raset[] = {0, 0, 0x01, 0x3F};
  static uint8_t row[240 * 2];
  for (size_t i = 0; i < sizeof row; i += 2) { row[i] = color >> 8; row[i + 1] = color & 0xFF; }
  tftCmd(0x2A, caset, 4);
  tftCmd(0x2B, raset, 4);
  SPI.beginTransaction(tftSpi);
  digitalWrite(tftCs, LOW);
  digitalWrite(tftDc, LOW);
  SPI.write(0x2C);                   // RAMWR
  digitalWrite(tftDc, HIGH);
  for (int y = 0; y < 320; y++) SPI.writeBytes(row, sizeof row);
  digitalWrite(tftCs, HIGH);
  SPI.endTransaction();
}

// Try SPI mode x CS/DC order at 4 MHz; each variant paints its own color for 3 s.
static void cmdLcdVariants(bool swapClkData) {
  tftSck = swapClkData ? TFT_SDA_PIN : TFT_SCL_PIN;
  tftMosi = swapClkData ? TFT_SCL_PIN : TFT_SDA_PIN;
  Serial.printf("SCK=GPIO%u MOSI=GPIO%u\n", tftSck, tftMosi);
  struct Variant { uint8_t mode; bool swap; uint16_t color; const char *name; };
  static const Variant kVariants[] = {
      {SPI_MODE0, false, 0xF800, "1 mode0 CS=pins.h      RED (inverted: cyan)"},
      {SPI_MODE3, false, 0x07E0, "2 mode3 CS=pins.h      GREEN (inverted: magenta)"},
      {SPI_MODE0, true, 0x001F, "3 mode0 CS/DC swapped  BLUE (inverted: yellow)"},
      {SPI_MODE3, true, 0xFFFF, "4 mode3 CS/DC swapped  WHITE (inverted: black)"},
  };
  for (const Variant &v : kVariants) {
    tftCs = v.swap ? TFT_DC_PIN : TFT_CS_PIN;
    tftDc = v.swap ? TFT_CS_PIN : TFT_DC_PIN;
    tftSpi = SPISettings(4000000, MSBFIRST, v.mode);
    Serial.printf("variant %s\n", v.name);
    tftInit();
    tftFill(v.color);
    delay(3000);
  }
  tftCs = TFT_CS_PIN;
  tftDc = TFT_DC_PIN;
  tftSck = TFT_SCL_PIN;
  tftMosi = TFT_SDA_PIN;
  tftSpi = SPISettings(20000000, MSBFIRST, SPI_MODE0);
}

static bool checkLcd() {
  Serial.println("\n== LCD (ST7789) ==");
  Serial.println("Watch screen: RED, GREEN, BLUE, WHITE, BLACK (1 s each)");
  tftInit();
  for (uint16_t c : {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000}) {
    tftFill(c);
    delay(1000);
  }
  if (askYesNo("Saw 5 full-screen colors?")) { Serial.println("PASS"); return true; }
  Serial.println("FAIL hints:");
  Serial.println("  backlight off -> VCC/GND");
  Serial.println("  backlight on, no image -> RST=GPIO8, DC=GPIO9, CS=GPIO10, SDA=GPIO11, SCL=GPIO12");
  Serial.println("  image but wrong colors -> wiring OK, inversion/RGB order fix in firmware");
  return false;
}

// ---------- WS2812B first LED ----------

static bool checkLed() {
  Serial.println("\n== LED strip (first LED only) ==");
  Serial.println("Watch first LED: RED, GREEN, BLUE (1 s each, low brightness)");
  const uint8_t k = 32;
  rgbLedWrite(LED_DATA_PIN, k, 0, 0); delay(1000);
  rgbLedWrite(LED_DATA_PIN, 0, k, 0); delay(1000);
  rgbLedWrite(LED_DATA_PIN, 0, 0, k); delay(1000);
  rgbLedWrite(LED_DATA_PIN, 0, 0, 0);
  if (askYesNo("First LED showed red, green, blue in that order?")) { Serial.println("PASS"); return true; }
  Serial.println("FAIL hints:");
  Serial.println("  dark -> DIN on GPIO7 via 470R, strip arrow direction (DIN not DOUT), 5V, GND");
  Serial.println("  wrong order -> wiring OK, color order fix in firmware");
  Serial.println("  flicker/random -> 3.3V data margin, see docs/kb/peripherals.md");
  return false;
}

// ---------- guided controls test ----------

struct PhaseStats {
  uint32_t toggles[2][2];
  uint8_t level[2][2];
};

static void snapshotLevels(PhaseStats &p) {
  for (int e = 0; e < 2; e++)
    for (int ph = 0; ph < 2; ph++) p.level[e][ph] = digitalRead(kEncPins[e][ph]);
}

// Wait until PCF inputs and encoder phases stop changing, so bounce/overturn
// from one step can't be judged against the next. Stuck pins don't block it.
static void settle(uint16_t &lastPcf) {
  uint32_t quietSince = millis();
  PhaseStats p = {};
  snapshotLevels(p);
  while (millis() - quietSince < kSettleMs) {
    uint16_t v;
    if (pcfRead(v) && v != lastPcf) { lastPcf = v; quietSince = millis(); }
    PhaseStats now = {};
    snapshotLevels(now);
    if (memcmp(now.level, p.level, sizeof p.level)) { p = now; quietSince = millis(); }
    delay(2);
  }
}

static void printPhaseHint(const BringupStep &s, const PhaseStats &p) {
  uint32_t a = p.toggles[s.enc][0], b = p.toggles[s.enc][1];
  Serial.printf("  phase toggles: A=%lu B=%lu\n", (unsigned long)a, (unsigned long)b);
  if (!a && !b)
    Serial.println("  -> no movement seen: encoder common pin (C) to GND? both phase wires?");
  else if (!a)
    Serial.printf("  -> phase A dead: check GPIO%u wire\n", kEncPins[s.enc][0]);
  else if (!b)
    Serial.printf("  -> phase B dead: check GPIO%u wire\n", kEncPins[s.enc][1]);
  else
    Serial.println("  -> both phases move but no full detent: half-step encoder? tell firmware dev");
}

static void runControls(bool pcfOk) {
  Serial.println("\n== Controls (s = skip step) ==");
  BringupSequencer seq(kSteps, kStepCount);
  uint16_t lastPcf = 0xFFFF;
  char msg[128];

  while (!seq.done()) {
    const BringupStep &step = *seq.current();
    if (step.kind == StepKind::Button && !pcfOk) {
      Serial.printf("[%u/%u] %s -> SKIP (no PCF8575)\n", seq.index() + 1, seq.count(), step.label);
      seq.skip();
      continue;
    }
    settle(lastPcf);
    Serial.printf("[%u/%u] %s ... ", seq.index() + 1, seq.count(), step.label);

    QuadDecoder dec[2] = {
        QuadDecoder(digitalRead(ENC1_PHASE_A_PIN), digitalRead(ENC1_PHASE_B_PIN)),
        QuadDecoder(digitalRead(ENC2_PHASE_A_PIN), digitalRead(ENC2_PHASE_B_PIN)),
    };
    PhaseStats ph = {};
    snapshotLevels(ph);
    bool intSeen = false;
    uint32_t start = millis();
    StepResult r = {Verdict::Pending, 0, 0, 0};

    while (r.verdict == Verdict::Pending) {
      if (Serial.available() && Serial.read() == 's') {
        seq.skip();
        r.verdict = Verdict::Skipped;
        break;
      }
      if (millis() - start > kStepTimeoutMs) {
        Serial.print("(timeout) ");
        seq.skip();
        r.verdict = Verdict::Skipped;
        break;
      }

      for (int e = 0; e < 2 && r.verdict == Verdict::Pending; e++) {
        uint8_t a = digitalRead(kEncPins[e][0]), b = digitalRead(kEncPins[e][1]);
        if (a != ph.level[e][0]) { ph.toggles[e][0]++; ph.level[e][0] = a; }
        if (b != ph.level[e][1]) { ph.toggles[e][1]++; ph.level[e][1] = b; }
        int d = dec[e].step(a, b);
        if (d) r = seq.onEncoder(e, d);
      }

      if (pcfOk && r.verdict == Verdict::Pending) {
        if (!digitalRead(PCF_INT_PIN)) intSeen = true;
        uint16_t v;
        if (pcfRead(v) && v != lastPcf) {
          r = seq.onButtons(diffButtons(lastPcf, v));
          lastPcf = v;
        }
      }
      delayMicroseconds(500);
    }

    describeResult(step, r, msg, sizeof msg);
    Serial.println(msg);
    if (step.kind == StepKind::EncoderTurn && r.verdict == Verdict::Skipped) printPhaseHint(step, ph);
    if (step.kind == StepKind::Button && r.verdict == Verdict::Skipped && step.mask & (step.mask - 1))
      Serial.println("  -> knob switch: one terminal must go to GND. If wired PCF pin<->PCF pin, nothing pulls low");
    if (step.kind == StepKind::Button && r.verdict != Verdict::Skipped && !intSeen)
      Serial.println("  WARN: INT (GPIO13) never went LOW - check INT wire");
  }

  Serial.printf("\nControls: %u pass, %u fail, %u skip\n", seq.passed(), seq.failed(), seq.skipped());
}

// ---------- entry ----------

static void runAll() {
  Serial.println("\n######## SAUCEDECK BRING-UP ########");
  bool pcfOk = checkI2C();
  checkIdle(pcfOk);
  checkLcd();
  checkLed();
  runControls(pcfOk);
  Serial.println("\nDone. Copy this log into docs/bringup/.");
}

// ---------- single-shot commands (host-driven bench sessions) ----------

static void cmdSnapshot() {
  uint16_t v;
  if (!pcfRead(v)) {
    Serial.println("PCF: read error (no ACK)");
  } else {
    Serial.printf("PCF: 0x%04X low:", v);
    bool any = false;
    for (uint8_t i = 0; i < 16; i++)
      if (!(v & (1u << i))) { Serial.printf(" %s", pcfPinName(i)); any = true; }
    Serial.println(any ? "" : " none");
  }
  Serial.printf("INT: %d\n", digitalRead(PCF_INT_PIN));
  for (int e = 0; e < 2; e++)
    Serial.printf("ENC%d: A=%d B=%d\n", e + 1, digitalRead(kEncPins[e][0]), digitalRead(kEncPins[e][1]));
}

static void cmdMonitor(uint32_t ms) {
  Serial.printf("Monitoring %lu s...\n", (unsigned long)(ms / 1000));
  uint16_t last = 0xFFFF;
  pcfRead(last);
  QuadDecoder dec[2] = {
      QuadDecoder(digitalRead(ENC1_PHASE_A_PIN), digitalRead(ENC1_PHASE_B_PIN)),
      QuadDecoder(digitalRead(ENC2_PHASE_A_PIN), digitalRead(ENC2_PHASE_B_PIN)),
  };
  PhaseStats ph = {};
  snapshotLevels(ph);
  uint32_t intLows = 0;
  bool intPrev = true;
  uint32_t start = millis();
  while (millis() - start < ms) {
    bool intNow = digitalRead(PCF_INT_PIN);
    if (intPrev && !intNow) intLows++;
    intPrev = intNow;
    uint16_t v;
    if (pcfRead(v) && v != last) {
      ButtonEvents ev = diffButtons(last, v);
      for (uint8_t i = 0; i < 16; i++) {
        if (ev.pressed & (1u << i)) Serial.printf("%6lu ms  %s DOWN\n", (unsigned long)(millis() - start), pcfPinName(i));
        if (ev.released & (1u << i)) Serial.printf("%6lu ms  %s UP\n", (unsigned long)(millis() - start), pcfPinName(i));
      }
      last = v;
    }
    for (int e = 0; e < 2; e++) {
      uint8_t a = digitalRead(kEncPins[e][0]), b = digitalRead(kEncPins[e][1]);
      if (a != ph.level[e][0]) { ph.toggles[e][0]++; ph.level[e][0] = a; }
      if (b != ph.level[e][1]) { ph.toggles[e][1]++; ph.level[e][1] = b; }
      int d = dec[e].step(a, b);
      if (d) Serial.printf("%6lu ms  ENC%d %s\n", (unsigned long)(millis() - start), e + 1, d > 0 ? "CW" : "CCW");
    }
    delayMicroseconds(500);
  }
  Serial.printf("INT low edges: %lu\n", (unsigned long)intLows);
  for (int e = 0; e < 2; e++)
    Serial.printf("ENC%d toggles: A=%lu B=%lu\n", e + 1, (unsigned long)ph.toggles[e][0],
                  (unsigned long)ph.toggles[e][1]);
}

static uint32_t i2cHz = 400000;

static void cmdScan() {
  Serial.printf("I2C scan @ %lu Hz, SDA=%d SCL=%d (idle levels)\n", (unsigned long)i2cHz,
                digitalRead(I2C_SDA_PIN), digitalRead(I2C_SCL_PIN));
  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; }
  }
  Serial.printf("\n%d device(s) ACK%s\n", found, found > 3 ? "  <- bus stuck low / bad pull-ups" : "");
}

static void cmdReadBurst() {
  Wire.beginTransmission(PCF_I2C_ADDR);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Serial.printf("write 0xFFFF -> err %d\n", Wire.endTransmission());
  for (int i = 0; i < 10; i++) {
    uint16_t v = 0;
    bool ok = pcfRead(v);
    Serial.printf(ok ? " %04X" : " ERR", v);
    delay(20);
  }
  Serial.println();
}

// Drive pattern, read back 5x, then release to inputs. Powered PCF: low bits read 0 solid.
static void cmdPattern(uint16_t pattern) {
  Wire.beginTransmission(PCF_I2C_ADDR);
  Wire.write(pattern & 0xFF);
  Wire.write(pattern >> 8);
  Serial.printf("drive 0x%04X -> err %d, read:", pattern, Wire.endTransmission());
  for (int i = 0; i < 5; i++) {
    uint16_t v = 0;
    Serial.printf(pcfRead(v) ? " %04X" : " ERR", v);
    delay(20);
  }
  Serial.println();
  pcfAllInputs();
}

// Drive one pin low at a time; any other pin reading low is coupled (short/bridge/load).
static void cmdShortMatrix() {
  auto writePcf = [](uint16_t v) {
    Wire.beginTransmission(PCF_I2C_ADDR);
    Wire.write(v & 0xFF);
    Wire.write(v >> 8);
    return Wire.endTransmission();
  };
  writePcf(0xFFFF);
  delay(50);
  uint16_t base = 0;
  pcfRead(base);
  Serial.printf("baseline all-high: %04X\n", base);
  for (uint8_t i = 0; i < 16; i++) {
    writePcf(0xFFFF & ~(1u << i));
    delay(5);
    uint16_t a = 0, b = 0;
    bool ok = pcfRead(a);
    delay(20);
    ok = ok && pcfRead(b);
    uint16_t followers = ~(a | b) & ~(1u << i) & base;  // low now, high at baseline
    Serial.printf("%s low -> read %04X/%04X", pcfPinName(i), a, b);
    if (!ok) Serial.print("  ERR");
    for (uint8_t j = 0; j < 16; j++)
      if (followers & (1u << j)) Serial.printf("  +%s", pcfPinName(j));
    Serial.println();
  }
  writePcf(0xFFFF);
}

static void cmdToggleSpeed() {
  i2cHz = i2cHz == 400000 ? 100000 : 400000;
  Wire.setClock(i2cHz);
  Serial.printf("I2C clock now %lu Hz\n", (unsigned long)i2cHz);
}

// Candidate config found on bench: CS/DC and SCK/MOSI both reversed vs pins.h, SPI mode 3.
static void cmdLcdCandidate() {
  tftCs = TFT_DC_PIN;
  tftDc = TFT_CS_PIN;
  tftSck = TFT_SDA_PIN;
  tftMosi = TFT_SCL_PIN;
  tftSpi = SPISettings(4000000, MSBFIRST, SPI_MODE3);
  Serial.printf("CS=GPIO%u DC=GPIO%u SCK=GPIO%u MOSI=GPIO%u mode3: RED GREEN BLUE WHITE 2 s each\n",
                tftCs, tftDc, tftSck, tftMosi);
  tftInit();
  for (uint16_t c : {0xF800, 0x07E0, 0x001F, 0xFFFF}) {
    tftFill(c);
    delay(2000);
  }
}

// Hold one solid color (candidate config, no re-init after first call): 1=red 2=green 3=blue
static void cmdLcdSolid(uint16_t color) {
  static bool ready = false;
  if (!ready || tftCs != TFT_DC_PIN) {
    tftCs = TFT_DC_PIN;
    tftDc = TFT_CS_PIN;
    tftSck = TFT_SDA_PIN;
    tftMosi = TFT_SCL_PIN;
    tftSpi = SPISettings(4000000, MSBFIRST, SPI_MODE3);
    tftInit();
    ready = true;
  }
  tftFill(color);
}

static void cmdLcd() {
  Serial.println("LCD: RED, GREEN, BLUE, WHITE 1.5 s each, then stays WHITE");
  tftInit();
  for (uint16_t c : {0xF800, 0x07E0, 0x001F, 0xFFFF}) {
    tftFill(c);
    delay(1500);
  }
}

static void cmdLed() {
  Serial.println("LED1: RED, GREEN, BLUE 1.5 s each, then stays dim WHITE");
  const uint8_t k = 32;
  rgbLedWrite(LED_DATA_PIN, k, 0, 0); delay(1500);
  rgbLedWrite(LED_DATA_PIN, 0, k, 0); delay(1500);
  rgbLedWrite(LED_DATA_PIN, 0, 0, k); delay(1500);
  rgbLedWrite(LED_DATA_PIN, k, k, k);
}

static void printHelp() {
  Serial.println("Commands: i=I2C+idle  p=snapshot  m=monitor 10s  M=monitor 30s");
  Serial.println("          l=LCD colors  w=LED1 colors  o=LED1 off  k=LCD black  g=guided full test");
  Serial.println("          s=I2C scan  r=PCF write+10 reads  f=toggle I2C 400k/100k  z=PCF drive patterns  x=PCF short matrix  v/V=LCD SPI variants (V: SCK/MOSI swapped)");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);  // wait for monitor (USB CDC DTR)

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  pinMode(PCF_INT_PIN, INPUT_PULLUP);
  for (auto &pins : kEncPins)
    for (uint8_t pin : pins) pinMode(pin, INPUT_PULLUP);
  pcfAllInputs();

  Serial.println("SAUCEDECK bring-up ready.");
  printHelp();
  Serial.println("OK>");
}

void loop() {
  if (!Serial.available()) { delay(10); return; }
  char c = Serial.read();
  switch (c) {
    case 'i': { bool ok = checkI2C(); checkIdle(ok); break; }
    case 'p': cmdSnapshot(); break;
    case 'm': cmdMonitor(10000); break;
    case 'M': cmdMonitor(30000); break;
    case 'l': cmdLcd(); break;
    case 'q': cmdLcdCandidate(); break;
    case '1': cmdLcdSolid(0xF800); break;
    case '2': cmdLcdSolid(0x07E0); break;
    case '3': cmdLcdSolid(0x001F); break;
    case 'v': cmdLcdVariants(false); break;
    case 'V': cmdLcdVariants(true); break;
    case 'k': tftInit(); tftFill(0x0000); break;
    case 'w': cmdLed(); break;
    case 'o': rgbLedWrite(LED_DATA_PIN, 0, 0, 0); break;
    case 'g': runAll(); break;
    case 's': cmdScan(); break;
    case 'r': cmdReadBurst(); break;
    case 'f': cmdToggleSpeed(); break;
    case 'x': cmdShortMatrix(); break;
    case 'z': cmdPattern(0x0000); cmdPattern(0xFF00); cmdPattern(0x00FF); cmdPattern(0xFFFF); break;
    case 'h': case '?': printHelp(); break;
    default: return;  // ignore CR/LF and noise
  }
  Serial.println("OK>");
}
