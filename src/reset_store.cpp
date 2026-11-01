#include "reset_store.h"
#include <Preferences.h>
#include <esp_attr.h>
#include <esp_system.h>

static const char *kNs = "saucedeck";
static const char *kKey = "resets";
static constexpr uint32_t kMagic = 0x5AD3C0DEu;

// RTC slow memory is not cleared by panic / watchdog / software resets (ESP-IDF RTC_NOINIT_ATTR),
// so it carries how long the crashed run was up. Lost on power-on (magic then invalid).
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t rtcUpS;

ResetEntry resetRecordBoot(ResetLog &log) {
  uint8_t reason = static_cast<uint8_t>(esp_reset_reason());
  ResetEntry e = {reason, reason != ESP_RST_POWERON && rtcMagic == kMagic ? rtcUpS : 0};
  rtcMagic = kMagic;
  rtcUpS = 0;

  Preferences p;
  uint8_t buf[ResetLog::kBytes];
  if (p.begin(kNs, true)) {
    size_t n = p.getBytes(kKey, buf, sizeof buf);
    p.end();
    log.load(buf, n);  // missing / corrupt -> empty log
  }
  log.push(e);
  size_t n = log.serialize(buf, sizeof buf);
  if (p.begin(kNs, false)) {
    p.putBytes(kKey, buf, n);
    p.end();
  }
  return e;
}

void resetUptimeTick(uint32_t nowMs) {
  rtcMagic = kMagic;
  rtcUpS = nowMs / 1000;
}
