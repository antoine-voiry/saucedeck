#pragma once
#include <stdint.h>
#include "reset_log.h"

// Reset history in NVS ("saucedeck"/"resets") plus the previous run's uptime kept in RTC memory.
// Call resetRecordBoot() once in setup(); it appends this boot and returns that entry.
ResetEntry resetRecordBoot(ResetLog &log);
void resetUptimeTick(uint32_t nowMs);  // refreshes the RTC uptime copy (once a second)
