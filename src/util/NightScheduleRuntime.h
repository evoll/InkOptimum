#pragma once

// Glue between the settings, the clock and night_schedule:: for the X4 Pro Night Mode.
namespace NightScheduleRuntime {

// What the output polarity should be right now.
bool inverted();

// The manual Night Mode switch. With the schedule off it flips the stored setting; with the schedule
// on it holds the other polarity until the night hours next change. Returns the new polarity.
bool toggle();

}  // namespace NightScheduleRuntime
