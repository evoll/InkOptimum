#pragma once

#include <DailyReadingSeconds.h>

#include <cstdint>

#include "ReadingStatsUtils.h"

// Per-day reading seconds on the SD card (/.inkmod/daily_seconds.bin). Written once per reading session, together
// with the other statistics; a missing or damaged file simply starts empty.
namespace DailyReadingStore {
DailyReadingSeconds load();
bool save(const DailyReadingSeconds& daily);
// Load, add the span that began at `localStart` and save.
void recordSpan(const ReadingStatsDateTime& localStart, uint32_t seconds);
}  // namespace DailyReadingStore
