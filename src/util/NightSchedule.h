#pragma once

#include <cstdint>

// Night Mode on a schedule (X4 Pro: global inverted output polarity). Pure rules, tested natively
// (test/night_schedule); the clock and settings glue is in NightScheduleRuntime.
//
// The night hours run from one whole hour of the day to another and may cross midnight; equal hours
// mean no night hours. A manual switch (panel tile, reader long-press menu) while the schedule is on
// does not touch the stored setting: it holds the other polarity until the night hours next change.
namespace night_schedule {

constexpr int kMinutesPerDay = 24 * 60;

constexpr bool inNightHours(const int minuteOfDay, const int fromHour, const int untilHour) {
  const int from = fromHour * 60;
  const int until = untilHour * 60;
  if (from == until) return false;
  return from < until ? (minuteOfDay >= from && minuteOfDay < until) : (minuteOfDay >= from || minuteOfDay < until);
}

// Minutes from now to a time of day, 1..1440 (a boundary at this very minute is a day away).
constexpr int minutesAhead(const int minuteOfDay, const int boundaryMinute) {
  const int ahead = boundaryMinute - minuteOfDay;
  return ahead <= 0 ? ahead + kMinutesPerDay : ahead;
}

constexpr int minutesToNextChange(const int minuteOfDay, const int fromHour, const int untilHour) {
  const int toStart = minutesAhead(minuteOfDay, fromHour * 60);
  const int toEnd = minutesAhead(minuteOfDay, untilHour * 60);
  return toStart < toEnd ? toStart : toEnd;
}

struct Override {
  bool active = false;
  bool inverted = false;
  int startMinute = 0;     // minute of day when the switch was made
  int durationMinutes = 0;  // until the night hours next change
};

constexpr bool overrideHolds(const Override& ov, const int nowMinute) {
  if (!ov.active) return false;
  const int elapsed = (nowMinute - ov.startMinute + kMinutesPerDay) % kMinutesPerDay;
  return elapsed < ov.durationMinutes;
}

// Whether the output is inverted. Without a set clock the stored manual setting decides.
constexpr bool isInverted(const bool scheduled, const bool clockKnown, const int minuteOfDay, const int fromHour,
                          const int untilHour, const bool manualInverted, const Override& ov) {
  if (!scheduled || !clockKnown) return manualInverted;
  if (overrideHolds(ov, minuteOfDay)) return ov.inverted;
  return inNightHours(minuteOfDay, fromHour, untilHour);
}

}  // namespace night_schedule
