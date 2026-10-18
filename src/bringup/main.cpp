// SAUCEDECK hardware bring-up: guided wiring test over USB serial.
// Flash with `pio run -e bringup -t upload`, then `pio device monitor`.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "bringup.h"
#include "pins.h"
#include "mcp_io.h"
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
    {StepKind::Button, DECK_ENC1_SW, 0, 0, "Push encoder 1 knob"},
    {StepKind::Button, DECK_ENC2_SW, 0, 0, "Push encoder 2 knob"},
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

// ---------- MCP23017 ----------

// Deck word, active-low like a raw port read: bit = 0 while that control is pressed.
static bool deckRead(uint16_t &out) {
  uint16_t raw;
  if (!mcpReadRaw(raw)) return false;
  out = ~deckPressed(raw);
  return true;
}

// ---------- static checks ----------

static bool checkI2C() {
  Serial.println("\n== I2C / MCP23017 ==");
  if (mcpBegin()) {
    Serial.printf("PASS: MCP23017 ACK at 0x%02X, init written\n", MCP_I2C_ADDR);
    return true;
  }
  Serial.printf("FAIL: no ACK at 0x%02X. Scanning bus...\n", MCP_I2C_ADDR);
  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X\n", a);
      found++;
    }
  }
  if (found)
    Serial.println("  -> MCP on other address (0x20-0x27): check A0-A2 pads or MCP_I2C_ADDR in pins.h");
  else
    Serial.println("  -> nothing on bus: check SDA=GPIO8, SCL=GPIO7 (swapped?), 3V3, GND");
  return false;
}

static void checkIdle(bool mcpOk) {
  Serial.println("\n== Idle levels (touch nothing) ==");
  delay(200);
  if (mcpOk) {
    uint16_t raw;
    if (!mcpReadRaw(raw)) {
      Serial.println("FAIL: MCP read error");
    } else {
      uint16_t held = deckPressed(raw);
      if (!held) Serial.printf("PASS: all 14 deck inputs HIGH (raw 0x%04X)\n", raw);
      for (uint8_t i = 0; i < 14; i++)
        if (held & (1u << i))
          Serial.printf("FAIL: %s stuck LOW - short to GND, wrong switch wiring, or held\n", deckPinName(i));
    }
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
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);
  digitalWrite(tftCs, HIGH);
  SPI.end();  // begin() is a no-op once initialized; re-init so pin variants apply
  SPI.begin(tftSck, -1, tftMosi, -1);  // write-only: no MISO, GPIO13 is DC

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
  Serial.println("  backlight off -> VCC/GND, BL=GPIO9");
  Serial.println("  backlight on, no image -> RST=GPIO44, DC=GPIO13, CS=GPIO10, SDA=GPIO11, SCL=GPIO12");
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
  Serial.println("  dark -> DIN on GPIO6, DIN end (not DOUT), 5V, GND");
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

// Wait until MCP inputs and encoder phases stop changing, so bounce/overturn
// from one step can't be judged against the next. Stuck pins don't block it.
static void settle(uint16_t &lastDeck) {
  uint32_t quietSince = millis();
  PhaseStats p = {};
  snapshotLevels(p);
  while (millis() - quietSince < kSettleMs) {
    uint16_t v;
    if (deckRead(v) && v != lastDeck) { lastDeck = v; quietSince = millis(); }
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

static void runControls(bool mcpOk) {
  Serial.println("\n== Controls (s = skip step) ==");
  BringupSequencer seq(kSteps, kStepCount);
  uint16_t lastDeck = 0xFFFF;
  char msg[128];

  while (!seq.done()) {
    const BringupStep &step = *seq.current();
    if (step.kind == StepKind::Button && !mcpOk) {
      Serial.printf("[%u/%u] %s -> SKIP (no MCP23017)\n", seq.index() + 1, seq.count(), step.label);
      seq.skip();
      continue;
    }
    settle(lastDeck);
    Serial.printf("[%u/%u] %s ... ", seq.index() + 1, seq.count(), step.label);

    QuadDecoder dec[2] = {
        QuadDecoder(digitalRead(ENC1_PHASE_A_PIN), digitalRead(ENC1_PHASE_B_PIN)),
        QuadDecoder(digitalRead(ENC2_PHASE_A_PIN), digitalRead(ENC2_PHASE_B_PIN)),
    };
    PhaseStats ph = {};
    snapshotLevels(ph);
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

      if (mcpOk && r.verdict == Verdict::Pending) {
        uint16_t v;
        if (deckRead(v) && v != lastDeck) {
          r = seq.onButtons(diffButtons(lastDeck, v));
          lastDeck = v;
        }
      }
      delayMicroseconds(500);
    }

    describeResult(step, r, msg, sizeof msg);
    Serial.println(msg);
    if (step.kind == StepKind::EncoderTurn && r.verdict == Verdict::Skipped) printPhaseHint(step, ph);
    if (step.kind == StepKind::Button && r.verdict == Verdict::Skipped && (step.mask & ~DECK_KEYS))
      Serial.println("  -> knob switch: S2 must be jumpered to C (GND), S1 to the MCP pin");
  }

  Serial.printf("\nControls: %u pass, %u fail, %u skip\n", seq.passed(), seq.failed(), seq.skipped());
}

// ---------- entry ----------

static void runAll() {
  Serial.println("\n######## SAUCEDECK BRING-UP ########");
  bool mcpOk = checkI2C();
  checkIdle(mcpOk);
  checkLcd();
  checkLed();
  runControls(mcpOk);
  Serial.println("\nDone. Copy this log into docs/bringup/.");
}

// ---------- single-shot commands (host-driven bench sessions) ----------

static void cmdSnapshot() {
  uint16_t raw;
  if (!mcpReadRaw(raw)) {
    Serial.println("MCP: read error (no ACK)");
  } else {
    uint16_t held = deckPressed(raw);
    Serial.printf("MCP: raw 0x%04X low:", raw);
    for (uint8_t i = 0; i < 14; i++)
      if (held & (1u << i)) Serial.printf(" %s", deckPinName(i));
    Serial.println(held ? "" : " none");
  }
  for (int e = 0; e < 2; e++)
    Serial.printf("ENC%d: A=%d B=%d\n", e + 1, digitalRead(kEncPins[e][0]), digitalRead(kEncPins[e][1]));
}

static void cmdMonitor(uint32_t ms) {
  Serial.printf("Monitoring %lu s...\n", (unsigned long)(ms / 1000));
  uint16_t last = 0xFFFF;
  deckRead(last);
  QuadDecoder dec[2] = {
      QuadDecoder(digitalRead(ENC1_PHASE_A_PIN), digitalRead(ENC1_PHASE_B_PIN)),
      QuadDecoder(digitalRead(ENC2_PHASE_A_PIN), digitalRead(ENC2_PHASE_B_PIN)),
  };
  PhaseStats ph = {};
  snapshotLevels(ph);
  uint32_t start = millis();
  while (millis() - start < ms) {
    uint16_t v;
    if (deckRead(v) && v != last) {
      ButtonEvents ev = diffButtons(last, v);
      for (uint8_t i = 0; i < 14; i++) {
        if (ev.pressed & (1u << i)) Serial.printf("%6lu ms  %s DOWN\n", (unsigned long)(millis() - start), deckPinName(i));
        if (ev.released & (1u << i)) Serial.printf("%6lu ms  %s UP\n", (unsigned long)(millis() - start), deckPinName(i));
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

// Raw MCP port read, 10x over 200 ms: GPA in low byte, GPB in high byte. Idle = 0x7F7F
// (GPA7/GPB7 are outputs driven LOW).
static void cmdReadBurst() {
  for (int i = 0; i < 10; i++) {
    uint16_t raw = 0;
    Serial.printf(mcpReadRaw(raw) ? " %04X" : " ERR", raw);
    delay(20);
  }
  Serial.println();
}

// Wire return codes for one register write and one register read (with and without
// repeated start). 0 = OK, 2 = NACK on address, 3 = NACK on data, 4/5 = bus error/timeout.
static void cmdMcpDiag() {
  // 1) State left by the previous 'd': GPPUA was written 0x7F. 00 here = chip was reset.
  for (int t = 0; t < 5; t++) {
    Wire.beginTransmission(MCP_I2C_ADDR);
    Wire.write(MCP_GPPUA);
    uint8_t e = Wire.endTransmission(false);
    uint8_t n = Wire.requestFrom((uint8_t)MCP_I2C_ADDR, (uint8_t)1);
    if (e == 0 && n == 1) { Serial.printf("GPPUA at entry = %02X (try %d)\n", Wire.read(), t); break; }
    Serial.printf("GPPUA at entry: ERR e=%u n=%u\n", e, n);
  }
  Wire.beginTransmission(MCP_I2C_ADDR);
  Wire.write(MCP_GPPUA);
  Wire.write(0x7F);
  Serial.printf("write GPPUA=7F -> %u\n", Wire.endTransmission());

  // 2) Probe 0x20-0x27, 40 rounds, sweep the idle gap between probes (us).
  for (int gapUs : {0, 100, 300, 1000, 3000, 10000, 20000}) {
    uint8_t hits[8] = {};
    for (int r = 0; r < 40; r++)
      for (uint8_t a = 0; a < 8; a++) {
        Wire.beginTransmission(0x20 + a);
        if (Wire.endTransmission() == 0) hits[a]++;
        if (gapUs) delayMicroseconds(gapUs);
      }
    Serial.printf("gap %5d us, ACKs/40:", gapUs);
    for (uint8_t a = 0; a < 8; a++) Serial.printf(" %02X:%u", 0x20 + a, hits[a]);
    Serial.println();
  }
  Serial.printf("SDA=%d SCL=%d\n", digitalRead(I2C_SDA_PIN), digitalRead(I2C_SCL_PIN));

  // 3) After one good probe, sample the idle bus every ~5 us for 10 ms: any LOW = glitch.
  Wire.beginTransmission(MCP_I2C_ADDR);
  Wire.endTransmission();
  uint32_t t0 = micros(), firstSda = 0, firstScl = 0, lowSda = 0, lowScl = 0, n = 0;
  while (micros() - t0 < 10000) {
    uint32_t t = micros() - t0;
    if (!digitalRead(I2C_SDA_PIN)) { if (!lowSda) firstSda = t; lowSda++; }
    if (!digitalRead(I2C_SCL_PIN)) { if (!lowScl) firstScl = t; lowScl++; }
    n++;
  }
  Serial.printf("idle 10 ms, %lu samples: SDA low %lu (first @%lu us), SCL low %lu (first @%lu us)\n",
                (unsigned long)n, (unsigned long)lowSda, (unsigned long)firstSda, (unsigned long)lowScl,
                (unsigned long)firstScl);
}

// ---------- bit-banged I2C (driver-independent check), ~25 kHz ----------

static void bbDelay() { delayMicroseconds(20); }
static void bbRelease(uint8_t pin) { pinMode(pin, INPUT_PULLUP); }
static void bbLow(uint8_t pin) { digitalWrite(pin, LOW); pinMode(pin, OUTPUT); }

static void bbStart() {
  bbRelease(I2C_SDA_PIN); bbRelease(I2C_SCL_PIN); bbDelay();
  bbLow(I2C_SDA_PIN); bbDelay(); bbLow(I2C_SCL_PIN); bbDelay();
}
static void bbStop() {
  bbLow(I2C_SDA_PIN); bbDelay(); bbRelease(I2C_SCL_PIN); bbDelay(); bbRelease(I2C_SDA_PIN); bbDelay();
}
// Returns true on ACK.
static bool bbWrite(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    if (b & (1 << i)) bbRelease(I2C_SDA_PIN); else bbLow(I2C_SDA_PIN);
    bbDelay(); bbRelease(I2C_SCL_PIN); bbDelay(); bbLow(I2C_SCL_PIN);
  }
  bbRelease(I2C_SDA_PIN); bbDelay(); bbRelease(I2C_SCL_PIN); bbDelay();
  bool ack = !digitalRead(I2C_SDA_PIN);
  bbLow(I2C_SCL_PIN); bbDelay();
  return ack;
}

// Wire released, same pins bit-banged: addr probe with idle gaps, then GPPUB write.
static void cmdBitBang() {
  Wire.end();
  for (int gapMs : {0, 5, 20, 100}) {
    int ok = 0;
    for (int r = 0; r < 20; r++) {
      bbStart();
      if (bbWrite(MCP_I2C_ADDR << 1)) ok++;
      bbStop();
      if (gapMs) delay(gapMs);
    }
    Serial.printf("bit-bang gap %3d ms: ACK %d/20\n", gapMs, ok);
  }
  delay(50);
  bbStart();
  bool a = bbWrite(MCP_I2C_ADDR << 1), r = bbWrite(MCP_GPPUB), v = bbWrite(0x7F);
  bbStop();
  Serial.printf("bit-bang write GPPUB=7F after 50 ms idle: addr %d reg %d val %d\n", a, r, v);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
}

static void cmdToggleSpeed() {
  i2cHz = i2cHz == 400000 ? 100000 : 400000;
  Wire.setClock(i2cHz);
  Serial.printf("I2C clock now %lu Hz\n", (unsigned long)i2cHz);
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
  Serial.println("          s=I2C scan  b=MCP init  d=MCP I2C diag  e=bit-bang I2C test  r=MCP raw 10 reads  f=toggle I2C 400k/100k  v/V=LCD SPI variants (V: SCK/MOSI swapped)");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);  // wait for monitor (USB CDC DTR)

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  for (auto &pins : kEncPins)
    for (uint8_t pin : pins) pinMode(pin, INPUT_PULLUP);
  mcpBegin();

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
    case 'v': cmdLcdVariants(false); break;
    case 'V': cmdLcdVariants(true); break;
    case 'k': tftInit(); tftFill(0x0000); break;
    case 'w': cmdLed(); break;
    case 'o': rgbLedWrite(LED_DATA_PIN, 0, 0, 0); break;
    case 'g': runAll(); break;
    case 's': cmdScan(); break;
    case 'r': cmdReadBurst(); break;
    case 'd': cmdMcpDiag(); break;
    case 'e': cmdBitBang(); break;
    case 'b': Serial.println(mcpBegin() ? "MCP init OK" : "MCP init FAIL (no ACK)"); break;
    case 'f': cmdToggleSpeed(); break;
    case 'h': case '?': printHelp(); break;
    default: return;  // ignore CR/LF and noise
  }
  Serial.println("OK>");
}
