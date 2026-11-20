#pragma once

#include "activities/Activity.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"

// Apps > Reading calendar: a month grid with the days you read marked (taken from the 730-day
// reading history kept by the statistics). Left/Up and Right/Down (or a tap on the left/right half
// of the screen) change the month, Confirm returns to the current month, Back exits.
class ReadingCalendarActivity final : public Activity {
 public:
  explicit ReadingCalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingCalendar", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void shiftMonth(int delta);

  GlobalReadingStats stats_;
  ReadingStatsDate today_;
  int year_ = 0;
  int month_ = 0;  // 1..12
};
