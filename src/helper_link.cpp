// Moves bytes only: framing in deck_core/frame.h, message rules in deck_core/helper_proto.h.
// Host->deck messages can only update displayed state (HostLink, DeckClock, StatusBoard);
// nothing here runs commands.
#include "helper_link.h"
#include <Arduino.h>
#include "USB.h"
#include "frame.h"
#include "helper_proto.h"

static HostLink hostLink;
static FrameReader reader;
static DeckClock deckClock;
static StatusBoard statusBoard;
static uint32_t rejected = 0;  // frames with a valid CRC but an invalid message

void helperBegin() {
  Serial.setTxTimeoutMs(20);  // never stall the loop when no helper is reading
  USB.onEvent(ARDUINO_USB_STOPPED_EVENT, [](void *, esp_event_base_t, int32_t, void *) { hostLink.onUsbReset(); });
}

void helperPoll(uint32_t nowMs) {
  uint8_t msg[kReportSize];
  int budget = 512;  // bound the work per loop pass
  while (Serial.available() > 0 && budget-- > 0) {
    if (!reader.push(static_cast<uint8_t>(Serial.read()), msg)) continue;
    HostMsg m;
    TimeMsg tm;
    CardMsg cm;
    if (decodeHost(msg, sizeof msg, m)) hostLink.onMessage(m, nowMs);
    else if (decodeTime(msg, sizeof msg, tm)) deckClock.onTime(tm.epoch, tm.tzOffsetMin, nowMs);
    else if (decodeCard(msg, sizeof msg, cm)) statusBoard.onCard(cm, nowMs);
    else rejected++;
  }
}

const HostLink &helperLink() { return hostLink; }
const DeckClock &helperClock() { return deckClock; }
StatusBoard &helperStatus() { return statusBoard; }

static void send(const uint8_t *msg, size_t n) {
  if (n != kReportSize) return;
  uint8_t f[kFrameSize];
  frameMessage(msg, f);
  Serial.write(f, sizeof f);
}

void helperSendAction(uint8_t key, const KeyEntry &e) {
  uint8_t m[kReportSize];
  send(m, encodeAction(key, e.type, e.value, m, sizeof m));
}

void helperSendMicDelta(int8_t steps) {
  uint8_t m[kReportSize];
  send(m, encodeMicDelta(steps, m, sizeof m));
}

void helperSendMicMute() {
  uint8_t m[kReportSize];
  send(m, encodeMicMute(m, sizeof m));
}
