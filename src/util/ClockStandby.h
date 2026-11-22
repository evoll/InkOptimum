#pragma once

#include <HalClock.h>

#include "InkMODSettings.h"

// Clock app power saving. After 15 s without input the Clock app calls enter(): it paints the clock face without
// button hints, arms an RTC timer for the next minute and deep-sleeps. Every timer wake runs a minimal boot (no
// activities): settings and fonts, redraw, deep sleep again. The power button wakes the device normally (to Home).
// Only boards with a hardware RTC take part: the software clock of the plain X4 cannot keep the minute over a sleep.
namespace ClockStandby {

inline bool available() {
  return !SETTINGS.clockDisabled && halClock.isAvailable() && !halClock.needsPeriodicNTPSync() &&
         halClock.hasValidDateTime();
}

// Paints the standby frame and sleeps until the next minute. Does not return. Implemented in main.cpp.
[[noreturn]] void enter();

// Called by setup() on a timer wake from deep sleep with power saving on. Draws the new minute and sleeps again, or
// returns (false) when the clock cannot be used so that the normal boot continues. Implemented in main.cpp.
bool onTimerWake();

// Seconds to sleep so that the wake lands `leadMs` before the next minute boundary, given the RTC's current second.
inline uint64_t sleepMicros(const int second, const int leadMs) {
  const int toBoundaryMs = (60 - second) * 1000;
  int ms = toBoundaryMs - leadMs;
  if (ms < 1000) ms += 60000;  // too close: aim at the following minute
  return static_cast<uint64_t>(ms) * 1000ULL;
}

}  // namespace ClockStandby
