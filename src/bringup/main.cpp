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

static const SPISettings kTftSpi(20000000, MSBFIRST, SPI_MODE0);

static void tftCmd(uint8_t cmd, const uint8_t *data = nullptr, size_t n = 0) {
  SPI.beginTransaction(kTftSpi);
  digitalWrite(TFT_CS_PIN, LOW);
  digitalWrite(TFT_DC_PIN, LOW);
  SPI.write(cmd);
  if (n) {
    digitalWrite(TFT_DC_PIN, HIGH);
    SPI.writeBytes(data, n);
  }
  digitalWrite(TFT_CS_PIN, HIGH);
  SPI.endTransaction();
}

static void tftInit() {
  pinMode(TFT_CS_PIN, OUTPUT);
  pinMode(TFT_DC_PIN, OUTPUT);
  pinMode(TFT_RST_PIN, OUTPUT);
  digitalWrite(TFT_CS_PIN, HIGH);
  SPI.begin(TFT_SCL_PIN, -1, TFT_SDA_PIN, -1);  // no MISO: GPIO13 belongs to PCF INT

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
  SPI.beginTransaction(kTftSpi);
  digitalWrite(TFT_CS_PIN, LOW);
  digitalWrite(TFT_DC_PIN, LOW);
  SPI.write(0x2C);                   // RAMWR
  digitalWrite(TFT_DC_PIN, HIGH);
  for (int y = 0; y < 320; y++) SPI.writeBytes(row, sizeof row);
  digitalWrite(TFT_CS_PIN, HIGH);
  SPI.endTransaction();
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
  Serial.println("  backlight on, no image -> RST=GPIO8, CS=GPIO9, DC=GPIO10, SDA=GPIO11, SCL=GPIO12");
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
  Serial.println("\nDone. Copy this log into docs/bringup/. Press any key to rerun.");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);  // wait for monitor (USB CDC DTR)

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  pinMode(PCF_INT_PIN, INPUT_PULLUP);
  for (auto &pins : kEncPins)
    for (uint8_t pin : pins) pinMode(pin, INPUT_PULLUP);

  Serial.println("SAUCEDECK bring-up ready. Press any key to start.");
  waitKey();
}

void loop() {
  runAll();
  waitKey();
}
