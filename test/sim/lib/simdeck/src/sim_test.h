#pragma once
#include <unity.h>

// Runs body in a fork()ed child: the firmware's statics start fresh, like a power-on.
// The child's Unity output is captured through a pipe; a failing assertion or a crash in the
// child becomes a FAIL of the test in the parent, with the child's message.
void simRunForked(void (*body)());

#define SIM_TEST(name)                     \
  static void name##_body();               \
  void name() { simRunForked(name##_body); } \
  static void name##_body()
