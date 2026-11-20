#include <NightSchedule.h>
#include <gtest/gtest.h>

using namespace night_schedule;

TEST(NightSchedule, HoursAcrossMidnight) {
  EXPECT_TRUE(inNightHours(22 * 60, 22, 7));
  EXPECT_TRUE(inNightHours(2 * 60, 22, 7));
  EXPECT_TRUE(inNightHours(6 * 60 + 59, 22, 7));
  EXPECT_FALSE(inNightHours(7 * 60, 22, 7));
  EXPECT_FALSE(inNightHours(12 * 60, 22, 7));
}

TEST(NightSchedule, HoursWithinDayAndEmpty) {
  EXPECT_TRUE(inNightHours(13 * 60, 12, 14));
  EXPECT_FALSE(inNightHours(14 * 60, 12, 14));
  EXPECT_FALSE(inNightHours(5, 8, 8));
}

TEST(NightSchedule, NextChange) {
  EXPECT_EQ(minutesToNextChange(23 * 60, 22, 7), 8 * 60);
  EXPECT_EQ(minutesToNextChange(12 * 60, 22, 7), 10 * 60);
  EXPECT_EQ(minutesToNextChange(22 * 60, 22, 7), 9 * 60);  // boundary now is a day away
}

TEST(NightSchedule, OverrideHoldsUntilChange) {
  Override ov{true, false, 23 * 60, 8 * 60};  // light screens held from 23:00 to 07:00
  EXPECT_TRUE(overrideHolds(ov, 23 * 60 + 30));
  EXPECT_TRUE(overrideHolds(ov, 3 * 60));
  EXPECT_FALSE(overrideHolds(ov, 7 * 60));
  EXPECT_FALSE(overrideHolds(ov, 8 * 60));
}

TEST(NightSchedule, IsInverted) {
  Override none;
  EXPECT_TRUE(isInverted(true, true, 23 * 60, 22, 7, false, none));
  EXPECT_FALSE(isInverted(true, true, 12 * 60, 22, 7, true, none));  // manual ignored while scheduled
  EXPECT_TRUE(isInverted(false, true, 12 * 60, 22, 7, true, none));  // schedule off: manual
  EXPECT_TRUE(isInverted(true, false, 12 * 60, 22, 7, true, none));  // clock unknown: manual
  Override ov{true, false, 23 * 60, 8 * 60};
  EXPECT_FALSE(isInverted(true, true, 23 * 60 + 5, 22, 7, false, ov));
  // Day override after the hours changed: the clock rules again.
  EXPECT_FALSE(isInverted(true, true, 7 * 60, 22, 7, false, ov));
}
