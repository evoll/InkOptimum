#pragma once

#include "activities/Activity.h"
#include "activities/reader/DailyReadingStore.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsUtils.h"

// Apps > Reading calendar: a month grid with the days you read marked (taken from the 730-day
// reading history kept by the statistics; the minutes read are printed under the day number when recorded). Left/Up and Right/Down (or a tap on the left/right half
// of the screen) change the month, Confirm returns to the current month, then toggles the year heat map (Left/Right: previous/next year), Back exits.
class ReadingCalendarActivity final : public Activity {
 public:
  explicit ReadingCalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingCalendar", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void shiftMonth(int delta);
  void renderYear(int top);
  bool yearView_ = false;  // Confirm on the current month toggles a whole-year heat map

  GlobalReadingStats stats_;
  DailyReadingSeconds daily_;  // minutes per day shown in the grid (days recorded since daily_seconds.bin appeared)
  ReadingStatsDate today_;
  int year_ = 0;
  int month_ = 0;  // 1..12
};
