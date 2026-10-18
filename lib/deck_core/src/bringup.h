#pragma once
#include <stddef.h>
#include <stdint.h>
#include "button_diff.h"

// Guided hardware bring-up: operator is asked to actuate one control per step,
// incoming events are judged against the expected wiring.

enum class StepKind : uint8_t { Button, EncoderTurn };

struct BringupStep {
  StepKind kind;
  uint16_t mask;  // Button: deck bit(s) (see mcp23017.h) that count as the expected press
  uint8_t enc;    // EncoderTurn: encoder index (0-based)
  int8_t dir;     // EncoderTurn: +1 CW, -1 CCW
  const char *label;
};

enum class Verdict : uint8_t { Pending, Pass, Fail, Skipped };

struct StepResult {
  Verdict verdict;
  uint16_t gotBits;
  uint8_t gotEnc;
  int8_t gotDir;
};

class BringupSequencer {
 public:
  BringupSequencer(const BringupStep *steps, size_t count) : steps_(steps), count_(count) {}

  const BringupStep *current() const { return done() ? nullptr : &steps_[index_]; }
  size_t index() const { return index_; }
  size_t count() const { return count_; }
  bool done() const { return index_ >= count_; }

  StepResult onButtons(const ButtonEvents &ev);
  StepResult onEncoder(uint8_t enc, int dir);
  void skip();

  size_t passed() const { return passed_; }
  size_t failed() const { return failed_; }
  size_t skipped() const { return skipped_; }

 private:
  StepResult finish(StepResult r);

  const BringupStep *steps_;
  size_t count_;
  size_t index_ = 0;
  size_t passed_ = 0, failed_ = 0, skipped_ = 0;
};

// Bench-readable verdict with a probable wiring cause.
void describeResult(const BringupStep &step, const StepResult &r, char *buf, size_t len);
