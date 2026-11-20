#include "NightScheduleRuntime.h"

#include <HalClock.h>

#include <cstdio>

#include "InkMODSettings.h"
#include "NightSchedule.h"

namespace {
night_schedule::Override gOverride;

bool readMinuteOfDay(int& minute) {
  if (!halClock.isAvailable() || !halClock.hasValidDateTime()) return false;
  char buf[9];
  if (!halClock.formatTime(buf, sizeof(buf), SETTINGS.clockUtcOffsetQ, false)) return false;
  int h = 0, m = 0;
  if (sscanf(buf, "%d:%d", &h, &m) != 2) return false;
  minute = h * 60 + m;
  return true;
}
}  // namespace

namespace NightScheduleRuntime {

bool inverted() {
  int minute = 0;
  const bool known = SETTINGS.nightSchedule != 0 && readMinuteOfDay(minute);
  return night_schedule::isInverted(SETTINGS.nightSchedule != 0, known, minute, SETTINGS.nightFromHour,
                                    SETTINGS.nightUntilHour, SETTINGS.screenInverted != 0, gOverride);
}

bool toggle() {
  int minute = 0;
  if (SETTINGS.nightSchedule != 0 && readMinuteOfDay(minute)) {
    const bool now = inverted();
    if (night_schedule::overrideHolds(gOverride, minute)) {
      gOverride = {};  // switched back before the hours changed: the schedule rules again
    } else {
      gOverride.active = true;
      gOverride.inverted = !now;
      gOverride.startMinute = minute;
      gOverride.durationMinutes =
          night_schedule::minutesToNextChange(minute, SETTINGS.nightFromHour, SETTINGS.nightUntilHour);
    }
    return inverted();
  }
  SETTINGS.screenInverted = SETTINGS.screenInverted ? 0 : 1;
  return SETTINGS.screenInverted != 0;
}

}  // namespace NightScheduleRuntime
