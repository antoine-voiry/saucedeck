#include <unity.h>
#include <string.h>
#include <stdlib.h>
#include "helper_proto.h"
#include "host_link.h"
#include "frame.h"

void setUp() {}
void tearDown() {}

static uint8_t rep[kReportSize];
static void msg(uint8_t type, uint8_t len, uint8_t p0 = 0, uint8_t p1 = 0) {
  memset(rep, 0, sizeof rep);
  rep[0] = kProtoVersion; rep[1] = type; rep[2] = len; rep[3] = p0; rep[4] = p1;
}

void test_decode_hello_volume_mic() {
  HostMsg m;
  msg(MSG_HELLO, 1, 3);
  TEST_ASSERT_TRUE(decodeHost(rep, sizeof rep, m));
  TEST_ASSERT_EQUAL_HEX8(MSG_HELLO, m.type);
  TEST_ASSERT_EQUAL_UINT8(3, m.a);
  msg(MSG_VOLUME, 2, 42, 1);
  TEST_ASSERT_TRUE(decodeHost(rep, sizeof rep, m));
  TEST_ASSERT_EQUAL_UINT8(42, m.a);
  TEST_ASSERT_EQUAL_UINT8(1, m.b);
  msg(MSG_MIC, 2, 100, 0);
  TEST_ASSERT_TRUE(decodeHost(rep, sizeof rep, m));
}

void test_decode_rejects_bad_fields() {
  HostMsg m;
  msg(MSG_VOLUME, 2, 101, 0); TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // pct > 100
  msg(MSG_VOLUME, 2, 50, 2);  TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // muted not 0/1
  msg(MSG_VOLUME, 3, 50, 0);  TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // wrong len
  msg(MSG_ACTION, 1, 0);      TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // deck->host type
  msg(0x7F, 0);               TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // unknown
  msg(MSG_HELLO, 61, 1);      TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));  // len > 60
  msg(MSG_HELLO, 1, 1); rep[0] = 2;
  TEST_ASSERT_FALSE(decodeHost(rep, sizeof rep, m));                               // version
  msg(MSG_HELLO, 1, 1);
  TEST_ASSERT_FALSE(decodeHost(rep, 2, m));                                        // short
}

void test_decode_garbage_never_crashes_or_accepts_out_of_range() {
  uint32_t x = 12345;
  HostMsg m;
  for (int i = 0; i < 20000; i++) {
    for (size_t k = 0; k < kReportSize; k++) {
      x = x * 1103515245u + 12345u;
      rep[k] = static_cast<uint8_t>(x >> 16);
    }
    if (i % 4 == 0) rep[0] = kProtoVersion;  // reach past the version check too
    size_t n = (x >> 8) % (kReportSize + 1);
    if (decodeHost(rep, n, m)) {
      TEST_ASSERT_TRUE(m.type == MSG_HELLO || m.type == MSG_VOLUME || m.type == MSG_MIC);
      if (m.type != MSG_HELLO) TEST_ASSERT_TRUE(m.a <= 100 && m.b <= 1);
    }
  }
}

void test_encode_action() {
  uint8_t out[kReportSize];
  TEST_ASSERT_EQUAL(kReportSize, encodeAction(4, KeyType::Url, "https://x.io", out, sizeof out));
  TEST_ASSERT_EQUAL_HEX8(kProtoVersion, out[0]);
  TEST_ASSERT_EQUAL_HEX8(MSG_ACTION, out[1]);
  TEST_ASSERT_EQUAL_UINT8(3 + 12, out[2]);
  TEST_ASSERT_EQUAL_UINT8(4, out[3]);
  TEST_ASSERT_EQUAL_UINT8(2, out[4]);   // KeyType::Url
  TEST_ASSERT_EQUAL_UINT8(12, out[5]);
  TEST_ASSERT_EQUAL_MEMORY("https://x.io", out + 6, 12);
  TEST_ASSERT_EQUAL_HEX8(0, out[18]);  // zero padded
}

void test_encode_action_rejects() {
  uint8_t out[kReportSize];
  char v[kKeyValueMax + 2];
  memset(v, 'a', kKeyValueMax + 1);
  v[kKeyValueMax + 1] = 0;
  TEST_ASSERT_EQUAL(0, encodeAction(0, KeyType::App, v, out, sizeof out));            // too long
  TEST_ASSERT_EQUAL(0, encodeAction(12, KeyType::App, "x", out, sizeof out));          // key idx
  TEST_ASSERT_EQUAL(0, encodeAction(0, KeyType::Keystroke, "a", out, sizeof out));     // not a helper type
  TEST_ASSERT_EQUAL(0, encodeAction(0, KeyType::App, "x", out, 10));                    // cap
  memset(v, 'a', kKeyValueMax);
  v[kKeyValueMax] = 0;
  TEST_ASSERT_EQUAL(kReportSize, encodeAction(0, KeyType::App, v, out, sizeof out));   // 56 fits
}

void test_encode_mic() {
  uint8_t out[kReportSize];
  TEST_ASSERT_EQUAL(kReportSize, encodeMicDelta(-2, out, sizeof out));
  TEST_ASSERT_EQUAL_HEX8(MSG_MIC_DELTA, out[1]);
  TEST_ASSERT_EQUAL_UINT8(1, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFE, out[3]);
  TEST_ASSERT_EQUAL(0, encodeMicDelta(9, out, sizeof out));  // |steps| <= 8
  TEST_ASSERT_EQUAL(kReportSize, encodeMicMute(out, sizeof out));
  TEST_ASSERT_EQUAL_HEX8(MSG_MIC_MUTE, out[1]);
  TEST_ASSERT_EQUAL_UINT8(0, out[2]);
}

// --- HostLink ---

void test_link_absent_until_hello() {
  HostLink l;
  TEST_ASSERT_FALSE(l.present(0));
  l.onMessage({MSG_VOLUME, 50, 0}, 10);  // state without HELLO does not mean present
  TEST_ASSERT_FALSE(l.present(10));
  l.onMessage({MSG_HELLO, 1, 0}, 20);
  TEST_ASSERT_TRUE(l.present(20));
}

void test_link_times_out_after_3s_silence() {
  HostLink l;
  l.onMessage({MSG_HELLO, 1, 0}, 1000);
  TEST_ASSERT_TRUE(l.present(1000 + HostLink::kTimeoutMs - 1));
  TEST_ASSERT_FALSE(l.present(1000 + HostLink::kTimeoutMs));
  l.onMessage({MSG_HELLO, 1, 0}, 5000);
  TEST_ASSERT_TRUE(l.present(5000));
}

void test_link_any_message_keeps_alive() {
  HostLink l;
  l.onMessage({MSG_HELLO, 1, 0}, 0);
  l.onMessage({MSG_MIC, 30, 1}, 2500);
  TEST_ASSERT_TRUE(l.present(5000));
}

void test_link_stores_volume_and_mic() {
  HostLink l;
  TEST_ASSERT_FALSE(l.hasVolume());
  l.onMessage({MSG_HELLO, 1, 0}, 0);
  l.onMessage({MSG_VOLUME, 42, 1}, 1);
  l.onMessage({MSG_MIC, 75, 0}, 2);
  TEST_ASSERT_TRUE(l.hasVolume() && l.hasMic());
  TEST_ASSERT_EQUAL_UINT8(42, l.volumePercent());
  TEST_ASSERT_TRUE(l.volumeMuted());
  TEST_ASSERT_EQUAL_UINT8(75, l.micPercent());
  TEST_ASSERT_FALSE(l.micMuted());
}

void test_link_usb_reset_clears_everything() {
  HostLink l;
  l.onMessage({MSG_HELLO, 1, 0}, 0);
  l.onMessage({MSG_VOLUME, 42, 0}, 1);
  l.onUsbReset();
  TEST_ASSERT_FALSE(l.present(2));
  TEST_ASSERT_FALSE(l.hasVolume());
}

void test_link_millis_wrap() {
  HostLink l;
  l.onMessage({MSG_HELLO, 1, 0}, 0xFFFFFF00u);
  TEST_ASSERT_TRUE(l.present(0x00000100u));  // 512 ms later across wrap
}

// --- serial framing (USB CDC transport, spike B outcome) ---

static size_t feed(FrameReader &r, const uint8_t *b, size_t n, uint8_t *msg) {
  size_t got = 0;
  for (size_t i = 0; i < n; i++) got += r.push(b[i], msg) ? 1 : 0;
  return got;
}

void test_crc16_check_value() {
  const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16ccitt(s, sizeof s));  // CRC-16/CCITT-FALSE
}

void test_frame_roundtrip() {
  uint8_t m[kReportSize], f[kFrameSize], out[kReportSize];
  encodeMicMute(m, sizeof m);
  frameMessage(m, f);
  TEST_ASSERT_EQUAL_HEX8(0xA5, f[0]);
  TEST_ASSERT_EQUAL_HEX8(0x5A, f[1]);
  FrameReader r;
  TEST_ASSERT_EQUAL(1, feed(r, f, sizeof f, out));
  TEST_ASSERT_EQUAL_MEMORY(m, out, kReportSize);
}

void test_frame_skips_text_and_junk_before_frame() {
  uint8_t m[kReportSize], f[kFrameSize], out[kReportSize];
  encodeMicDelta(3, m, sizeof m);
  frameMessage(m, f);
  const uint8_t junk[] = {'P', 'C', 'N', 'T', ' ', 'f', 'a', 'i', 'l', '\n', 0xA5, 0x00, 0xA5};
  FrameReader r;
  TEST_ASSERT_EQUAL(0, feed(r, junk, sizeof junk, out));
  TEST_ASSERT_EQUAL(1, feed(r, f, sizeof f, out));
  TEST_ASSERT_EQUAL_MEMORY(m, out, kReportSize);
}

void test_frame_bad_crc_dropped_then_next_frame_ok() {
  uint8_t m[kReportSize], f[kFrameSize], bad[kFrameSize], out[kReportSize];
  encodeMicMute(m, sizeof m);
  frameMessage(m, f);
  memcpy(bad, f, sizeof f);
  bad[10] ^= 0x40;
  FrameReader r;
  TEST_ASSERT_EQUAL(0, feed(r, bad, sizeof bad, out));
  TEST_ASSERT_EQUAL(1, feed(r, f, sizeof f, out));
  TEST_ASSERT_EQUAL_MEMORY(m, out, kReportSize);
}

void test_frame_fake_sync_in_corrupt_frame_does_not_swallow_next() {
  uint8_t m[kReportSize], f[kFrameSize], bad[kFrameSize], out[kReportSize];
  encodeMicDelta(-1, m, sizeof m);
  frameMessage(m, f);
  memcpy(bad, f, sizeof f);
  bad[20] = 0xA5;
  bad[21] = 0x5A;  // fake sync inside, CRC now wrong
  FrameReader r;
  size_t got = feed(r, bad, sizeof bad, out) + feed(r, f, sizeof f, out);
  TEST_ASSERT_EQUAL(1, got);
  TEST_ASSERT_EQUAL_MEMORY(m, out, kReportSize);
}

void test_frame_random_bytes_emit_nothing() {
  uint32_t x = 777;
  uint8_t out[kReportSize];
  FrameReader r;
  size_t got = 0;
  for (int i = 0; i < 200000; i++) {
    x = x * 1103515245u + 12345u;
    got += r.push(static_cast<uint8_t>(x >> 16), out) ? 1 : 0;
  }
  TEST_ASSERT_EQUAL(0, got);
}

void test_frame_back_to_back_frames() {
  uint8_t m1[kReportSize], m2[kReportSize], f[2 * kFrameSize], out[kReportSize];
  encodeMicMute(m1, sizeof m1);
  encodeMicDelta(2, m2, sizeof m2);
  frameMessage(m1, f);
  frameMessage(m2, f + kFrameSize);
  FrameReader r;
  TEST_ASSERT_EQUAL(1, feed(r, f, kFrameSize, out));
  TEST_ASSERT_EQUAL(1, feed(r, f + kFrameSize, kFrameSize, out));
  TEST_ASSERT_EQUAL_MEMORY(m2, out, kReportSize);
}

// --- TIME / CARD (status display, host -> deck) ---

static size_t timeMsg(uint8_t *b, uint32_t epoch, int16_t tz, uint8_t len = 6) {
  memset(b, 0, kReportSize);
  b[0] = kProtoVersion; b[1] = MSG_TIME; b[2] = len;
  for (int k = 0; k < 4; k++) b[3 + k] = static_cast<uint8_t>(epoch >> (8 * k));
  b[7] = static_cast<uint8_t>(tz); b[8] = static_cast<uint8_t>(static_cast<uint16_t>(tz) >> 8);
  return kReportSize;
}

static size_t cardMsg(uint8_t *b, uint8_t slot, uint8_t icon, uint16_t ttl, const char *text) {
  size_t tl = strlen(text);
  memset(b, 0, kReportSize);
  b[0] = kProtoVersion; b[1] = MSG_CARD; b[2] = static_cast<uint8_t>(5 + tl);
  b[3] = slot; b[4] = icon; b[5] = ttl & 0xFF; b[6] = ttl >> 8; b[7] = static_cast<uint8_t>(tl);
  memcpy(b + 8, text, tl);
  return kReportSize;
}

void test_decode_time() {
  uint8_t b[kReportSize];
  TimeMsg t;
  TEST_ASSERT_TRUE(decodeTime(b, timeMsg(b, 0x6A2B3C4Du, -60), t));
  TEST_ASSERT_EQUAL_HEX32(0x6A2B3C4Du, t.epoch);
  TEST_ASSERT_EQUAL_INT16(-60, t.tzOffsetMin);
  TEST_ASSERT_TRUE(decodeTime(b, timeMsg(b, 1, -720), t));
  TEST_ASSERT_TRUE(decodeTime(b, timeMsg(b, 1, 840), t));
}

void test_decode_time_rejects() {
  uint8_t b[kReportSize];
  TimeMsg t;
  TEST_ASSERT_FALSE(decodeTime(b, timeMsg(b, 1, 841), t));
  TEST_ASSERT_FALSE(decodeTime(b, timeMsg(b, 1, -721), t));
  TEST_ASSERT_FALSE(decodeTime(b, timeMsg(b, 1, 0, 5), t));  // wrong len
  timeMsg(b, 1, 0); b[0] = 2;
  TEST_ASSERT_FALSE(decodeTime(b, kReportSize, t));          // version
  timeMsg(b, 1, 0); b[1] = MSG_CARD;
  TEST_ASSERT_FALSE(decodeTime(b, kReportSize, t));          // other type
  TEST_ASSERT_FALSE(decodeTime(b, 8, t));                    // truncated buffer
}

void test_decode_card() {
  uint8_t b[kReportSize];
  CardMsg c;
  TEST_ASSERT_TRUE(decodeCard(b, cardMsg(b, 1, 1, 300, "Daft Punk - Around the World"), c));
  TEST_ASSERT_EQUAL_UINT8(1, c.slot);
  TEST_ASSERT_EQUAL_UINT8(1, c.icon);
  TEST_ASSERT_EQUAL_UINT16(300, c.ttlS);
  TEST_ASSERT_EQUAL_STRING("Daft Punk - Around the World", c.text);
  TEST_ASSERT_TRUE(decodeCard(b, cardMsg(b, 7, 7, 0, ""), c));  // empty = clear slot
  TEST_ASSERT_EQUAL_STRING("", c.text);
  char max[kCardTextMax + 1];
  memset(max, 'x', kCardTextMax);
  max[kCardTextMax] = 0;
  TEST_ASSERT_TRUE(decodeCard(b, cardMsg(b, 0, 0, 0, max), c));
  TEST_ASSERT_EQUAL_size_t(kCardTextMax, strlen(c.text));
}

void test_decode_card_rejects() {
  uint8_t b[kReportSize];
  CardMsg c;
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 8, 1, 0, "x"), c));      // slot
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 1, 8, 0, "x"), c));      // icon
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 1, 1, 0, "a\x7F"), c));  // DEL
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 1, 1, 0, "a\x1F"), c));  // control
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 1, 1, 0, "Caf\xC3\xA9"), c));  // UTF-8
  cardMsg(b, 1, 1, 0, "abc"); b[2] = 9;                               // len != 5 + tlen
  TEST_ASSERT_FALSE(decodeCard(b, kReportSize, c));
  char big[kCardTextMax + 2];
  memset(big, 'x', kCardTextMax + 1);
  big[kCardTextMax + 1] = 0;
  TEST_ASSERT_FALSE(decodeCard(b, cardMsg(b, 1, 1, 0, big), c));      // tlen 49
  cardMsg(b, 1, 1, 0, "x"); b[1] = MSG_TIME;
  TEST_ASSERT_FALSE(decodeCard(b, kReportSize, c));
  HostMsg h;
  TEST_ASSERT_FALSE(decodeHost(b, kReportSize, h));                   // decodeHost ignores 0x04
  cardMsg(b, 1, 1, 0, "x");
  TEST_ASSERT_FALSE(decodeHost(b, kReportSize, h));                   // ... and 0x05
}

void test_decode_status_fuzz_never_accepts_out_of_range() {
  srand(12345);
  uint8_t b[kReportSize];
  for (int it = 0; it < 20000; it++) {
    for (size_t i = 0; i < kReportSize; i++) b[i] = static_cast<uint8_t>(rand());
    b[0] = kProtoVersion;
    b[1] = (it & 1) ? MSG_CARD : MSG_TIME;
    if (it % 3 == 0) b[2] = static_cast<uint8_t>(5 + (b[7] % 50));  // make len plausible
    CardMsg c;
    if (decodeCard(b, kReportSize, c)) {
      TEST_ASSERT_TRUE(c.slot < kCardSlots && c.icon < kCardIcons);
      size_t n = strlen(c.text);
      TEST_ASSERT_TRUE(n <= kCardTextMax);
      for (size_t k = 0; k < n; k++) TEST_ASSERT_TRUE(c.text[k] >= 0x20 && c.text[k] <= 0x7E);
    }
    TimeMsg t;
    if (decodeTime(b, kReportSize, t)) TEST_ASSERT_TRUE(t.tzOffsetMin >= -720 && t.tzOffsetMin <= 840);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_decode_hello_volume_mic);
  RUN_TEST(test_decode_rejects_bad_fields);
  RUN_TEST(test_decode_garbage_never_crashes_or_accepts_out_of_range);
  RUN_TEST(test_encode_action);
  RUN_TEST(test_encode_action_rejects);
  RUN_TEST(test_encode_mic);
  RUN_TEST(test_link_absent_until_hello);
  RUN_TEST(test_link_times_out_after_3s_silence);
  RUN_TEST(test_link_any_message_keeps_alive);
  RUN_TEST(test_link_stores_volume_and_mic);
  RUN_TEST(test_link_usb_reset_clears_everything);
  RUN_TEST(test_link_millis_wrap);
  RUN_TEST(test_crc16_check_value);
  RUN_TEST(test_frame_roundtrip);
  RUN_TEST(test_frame_skips_text_and_junk_before_frame);
  RUN_TEST(test_frame_bad_crc_dropped_then_next_frame_ok);
  RUN_TEST(test_frame_fake_sync_in_corrupt_frame_does_not_swallow_next);
  RUN_TEST(test_frame_random_bytes_emit_nothing);
  RUN_TEST(test_frame_back_to_back_frames);
  RUN_TEST(test_decode_time);
  RUN_TEST(test_decode_time_rejects);
  RUN_TEST(test_decode_card);
  RUN_TEST(test_decode_card_rejects);
  RUN_TEST(test_decode_status_fuzz_never_accepts_out_of_range);
  return UNITY_END();
}
