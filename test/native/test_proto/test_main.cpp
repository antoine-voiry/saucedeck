#include <unity.h>
#include <string.h>
#include "helper_proto.h"
#include "host_link.h"

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
  return UNITY_END();
}
