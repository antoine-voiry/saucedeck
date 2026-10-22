#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "helper_proto.h"

// HelperProto messages over the USB CDC serial port (spike B: vendor HID needs macOS
// Input Monitoring). Frame = A5 5A | 63-byte message | CRC16-CCITT-FALSE (LE) over the message.
// The reader re-syncs on any junk (debug text, partial frames, fake sync bytes in a payload).
constexpr size_t kFrameSize = 2 + kReportSize + 2;
constexpr uint8_t kSync0 = 0xA5, kSync1 = 0x5A;

inline uint16_t crc16ccitt(const uint8_t *d, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= static_cast<uint16_t>(d[i]) << 8;
    for (int k = 0; k < 8; k++) c = (c & 0x8000) ? static_cast<uint16_t>((c << 1) ^ 0x1021) : static_cast<uint16_t>(c << 1);
  }
  return c;
}

inline void frameMessage(const uint8_t msg[kReportSize], uint8_t out[kFrameSize]) {
  out[0] = kSync0;
  out[1] = kSync1;
  memcpy(out + 2, msg, kReportSize);
  uint16_t c = crc16ccitt(msg, kReportSize);
  out[kFrameSize - 2] = static_cast<uint8_t>(c);
  out[kFrameSize - 1] = static_cast<uint8_t>(c >> 8);
}

class FrameReader {
 public:
  // Feed one byte; returns true and fills msg when a frame with a valid CRC completes.
  bool push(uint8_t b, uint8_t msg[kReportSize]) {
    buf_[len_++] = b;
    for (;;) {
      if (len_ >= 1 && buf_[0] != kSync0) { drop1(); continue; }
      if (len_ >= 2 && buf_[1] != kSync1) { drop1(); continue; }
      if (len_ < kFrameSize) return false;
      uint16_t got = static_cast<uint16_t>(buf_[kFrameSize - 2] | (buf_[kFrameSize - 1] << 8));
      if (crc16ccitt(buf_ + 2, kReportSize) == got) {
        memcpy(msg, buf_ + 2, kReportSize);
        len_ = 0;
        return true;
      }
      drop1();  // bad CRC: the real frame may start inside this one, rescan
    }
  }

 private:
  void drop1() {
    memmove(buf_, buf_ + 1, len_ - 1);
    len_--;
  }

  uint8_t buf_[kFrameSize] = {};
  size_t len_ = 0;
};
