#include "bringup.h"
#include <stdio.h>
#include "mcp23017.h"

static const StepResult kPending = {Verdict::Pending, 0, 0, 0};

StepResult BringupSequencer::finish(StepResult r) {
  if (r.verdict == Verdict::Pass) passed_++;
  if (r.verdict == Verdict::Fail) failed_++;
  if (r.verdict == Verdict::Skipped) skipped_++;
  index_++;
  return r;
}

StepResult BringupSequencer::onButtons(const ButtonEvents &ev) {
  const BringupStep *s = current();
  if (!s || s->kind != StepKind::Button || !ev.pressed) return kPending;
  bool ok = (ev.pressed & s->mask) && !(ev.pressed & ~s->mask);
  return finish({ok ? Verdict::Pass : Verdict::Fail, ev.pressed, 0, 0});
}

StepResult BringupSequencer::onEncoder(uint8_t enc, int dir) {
  const BringupStep *s = current();
  if (!s || s->kind != StepKind::EncoderTurn || !dir) return kPending;
  int8_t d = dir > 0 ? 1 : -1;
  bool ok = enc == s->enc && d == s->dir;
  return finish({ok ? Verdict::Pass : Verdict::Fail, 0, enc, d});
}

void BringupSequencer::skip() {
  if (!done()) finish({Verdict::Skipped, 0, 0, 0});
}

static int lowestBit(uint16_t v) {
  for (int i = 0; i < 16; i++)
    if (v & (1u << i)) return i;
  return -1;
}

static bool singleBit(uint16_t v) { return v && !(v & (v - 1)); }

void describeResult(const BringupStep &step, const StepResult &r, char *buf, size_t len) {
  switch (r.verdict) {
    case Verdict::Pending:
      snprintf(buf, len, "PENDING");
      return;
    case Verdict::Pass:
      snprintf(buf, len, "PASS");
      return;
    case Verdict::Skipped:
      snprintf(buf, len, "SKIP: no signal - check wire, solder joint, GND");
      return;
    case Verdict::Fail:
      break;
  }

  if (step.kind == StepKind::EncoderTurn) {
    if (r.gotEnc != step.enc)
      snprintf(buf, len, "FAIL: got encoder %u - encoder cables crossed", r.gotEnc + 1u);
    else
      snprintf(buf, len, "FAIL: direction reversed - swap A/B wires (or A/B pins in pins.h)");
    return;
  }

  if (!singleBit(r.gotBits)) {
    snprintf(buf, len, "FAIL: got 0x%04X (several pins) - short between pins?", r.gotBits);
  } else if (singleBit(step.mask)) {
    snprintf(buf, len, "FAIL: got %s, expected %s - wires crossed", deckPinName(lowestBit(r.gotBits)),
             deckPinName(lowestBit(step.mask)));
  } else {
    snprintf(buf, len, "FAIL: got %s, expected mask 0x%04X - wires crossed",
             deckPinName(lowestBit(r.gotBits)), step.mask);
  }
}
