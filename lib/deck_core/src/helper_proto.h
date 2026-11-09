#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "keymap.h"

// Deck <-> Mac helper messages (63-byte messages, framed on USB CDC by frame.h).
// [ver, type, len, payload...], zero padded. Host->deck messages can only update
// displayed state; actions only flow deck->host, from a physical key press.
constexpr size_t kReportSize = 63;
constexpr size_t kPayloadMax = kReportSize - 3;
constexpr uint8_t kProtoVersion = 1;
enum : uint8_t {
  MSG_HELLO = 0x01,
  MSG_VOLUME = 0x02,
  MSG_MIC = 0x03,
  MSG_TIME = 0x04,  // status display: wall clock
  MSG_CARD = 0x05,  // status display: one text card
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

// Status display (spec 2026-10-31 §4.4). Display-only like VOLUME/MIC: nothing here can
// trigger a key or an action.
constexpr size_t kCardTextMax = 48;
constexpr uint8_t kCardSlots = 8;  // 0 = top bar, 1..7 rotate on the strip
constexpr uint8_t kCardIcons = 8;  // 0 none, 1 music, 2 calendar, 3 cpu, 4 weather, 5 mail, 6 chat, 7 battery

struct TimeMsg {
  uint32_t epoch;       // UTC seconds
  int16_t tzOffsetMin;  // local = UTC + offset, -720..+840
};

struct CardMsg {
  uint8_t slot;
  uint8_t icon;
  uint16_t ttlS;  // 0 = until replaced
  char text[kCardTextMax + 1];  // printable ASCII, empty = clear the slot
};

inline bool decodeTime(const uint8_t *buf, size_t n, TimeMsg &out) {
  if (n < 9 || buf[0] != kProtoVersion || buf[1] != MSG_TIME || buf[2] != 6) return false;
  const uint8_t *p = buf + 3;
  uint32_t e = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
  int16_t tz = static_cast<int16_t>(static_cast<uint16_t>(p[4] | (p[5] << 8)));
  if (tz < -720 || tz > 840) return false;
  out = {e, tz};
  return true;
}

inline bool decodeCard(const uint8_t *buf, size_t n, CardMsg &out) {
  if (n < 8 || buf[0] != kProtoVersion || buf[1] != MSG_CARD) return false;
  uint8_t len = buf[2];
  const uint8_t *p = buf + 3;
  if (len < 5 || 3u + len > n) return false;
  uint8_t tlen = p[4];
  if (p[0] >= kCardSlots || p[1] >= kCardIcons || tlen > kCardTextMax || len != 5 + tlen) return false;
  for (uint8_t i = 0; i < tlen; i++)
    if (p[5 + i] < 0x20 || p[5 + i] > 0x7E) return false;
  out.slot = p[0];
  out.icon = p[1];
  out.ttlS = static_cast<uint16_t>(p[2] | (p[3] << 8));
  memcpy(out.text, p + 5, tlen);
  out.text[tlen] = 0;
  return true;
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
