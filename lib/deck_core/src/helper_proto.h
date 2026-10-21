#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "keymap.h"

// Deck <-> Mac helper messages on the vendor HID interface (63-byte reports).
// [ver, type, len, payload...], zero padded. Host->deck messages can only update
// displayed state; actions only flow deck->host, from a physical key press.
constexpr size_t kReportSize = 63;
constexpr size_t kPayloadMax = kReportSize - 3;
constexpr uint8_t kProtoVersion = 1;
enum : uint8_t {
  MSG_HELLO = 0x01,
  MSG_VOLUME = 0x02,
  MSG_MIC = 0x03,
  MSG_ACTION = 0x81,
  MSG_MIC_DELTA = 0x82,
  MSG_MIC_MUTE = 0x83,
};

struct HostMsg {
  uint8_t type;
  uint8_t a;  // HELLO: helper version; VOLUME/MIC: percent 0..100
  uint8_t b;  // VOLUME/MIC: muted 0/1
};

inline bool decodeHost(const uint8_t *buf, size_t n, HostMsg &out) {
  if (n < 3 || buf[0] != kProtoVersion) return false;
  uint8_t type = buf[1], len = buf[2];
  if (len > kPayloadMax || 3u + len > n) return false;
  const uint8_t *p = buf + 3;
  switch (type) {
    case MSG_HELLO:
      if (len != 1) return false;
      out = {type, p[0], 0};
      return true;
    case MSG_VOLUME:
    case MSG_MIC:
      if (len != 2 || p[0] > 100 || p[1] > 1) return false;
      out = {type, p[0], p[1]};
      return true;
    default:
      return false;
  }
}

namespace proto_detail {
inline size_t frame(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t cap) {
  if (cap < kReportSize || len > kPayloadMax) return 0;
  memset(out, 0, kReportSize);
  out[0] = kProtoVersion;
  out[1] = type;
  out[2] = static_cast<uint8_t>(len);
  if (len) memcpy(out + 3, payload, len);
  return kReportSize;
}
}  // namespace proto_detail

inline size_t encodeAction(uint8_t key, KeyType t, const char *value, uint8_t *out, size_t cap) {
  if (key >= kKeyCount) return 0;
  if (t != KeyType::App && t != KeyType::Url && t != KeyType::Shortcut) return 0;
  size_t vlen = strnlen(value, kKeyValueMax + 1);
  if (vlen == 0 || vlen > kKeyValueMax) return 0;
  uint8_t p[kPayloadMax];
  p[0] = key;
  p[1] = static_cast<uint8_t>(t);
  p[2] = static_cast<uint8_t>(vlen);
  memcpy(p + 3, value, vlen);
  return proto_detail::frame(MSG_ACTION, p, 3 + vlen, out, cap);
}

inline size_t encodeMicDelta(int8_t steps, uint8_t *out, size_t cap) {
  if (steps < -8 || steps > 8 || steps == 0) return 0;
  uint8_t p[1] = {static_cast<uint8_t>(steps)};
  return proto_detail::frame(MSG_MIC_DELTA, p, 1, out, cap);
}

inline size_t encodeMicMute(uint8_t *out, size_t cap) {
  return proto_detail::frame(MSG_MIC_MUTE, nullptr, 0, out, cap);
}
