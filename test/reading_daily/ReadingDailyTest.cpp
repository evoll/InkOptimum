#include <gtest/gtest.h>

#include "DailyReadingSeconds.h"

TEST(ReadingDaily, SpanInsideOneDay) {
  DailyReadingSeconds d;
  d.record(100, 3600, 600);
  d.record(100, 7200, 300);
  EXPECT_EQ(d.secondsFor(100), 900u);
  EXPECT_EQ(d.secondsFor(99), 0u);
  EXPECT_EQ(d.entries().size(), 1u);
}

TEST(ReadingDaily, SpanCrossesMidnightAndSplits) {
  DailyReadingSeconds d;
  d.record(100, 86400 - 60, 180);  // 60 s before midnight, 120 s after
  EXPECT_EQ(d.secondsFor(100), 60u);
  EXPECT_EQ(d.secondsFor(101), 120u);
}

TEST(ReadingDaily, LongSpanCoversSeveralDaysAndCapsAtADay) {
  DailyReadingSeconds d;
  d.record(10, 0, 3 * 86400);
  EXPECT_EQ(d.secondsFor(10), 86400u);
  EXPECT_EQ(d.secondsFor(12), 86400u);
  EXPECT_EQ(d.secondsFor(13), 0u);
  d.record(10, 0, 500);  // already full: stays capped
  EXPECT_EQ(d.secondsFor(10), 86400u);
}

TEST(ReadingDaily, RangeSumAndOrder) {
  DailyReadingSeconds d;
  d.record(30, 0, 30);
  d.record(10, 0, 10);
  d.record(20, 0, 20);
  ASSERT_EQ(d.entries().size(), 3u);
  EXPECT_EQ(d.entries()[0].dayIndex, 10u);
  EXPECT_EQ(d.entries()[2].dayIndex, 30u);
  EXPECT_EQ(d.secondsBetween(10, 20), 30u);
  EXPECT_EQ(d.secondsBetween(11, 100), 50u);
}

TEST(ReadingDaily, KeepsOnlyTheNewestDays) {
  DailyReadingSeconds d;
  for (uint32_t i = 0; i < DailyReadingSeconds::MAX_DAYS + 25; ++i) d.record(1000 + i, 0, 60);
  EXPECT_EQ(d.entries().size(), DailyReadingSeconds::MAX_DAYS);
  EXPECT_EQ(d.secondsFor(1000), 0u);                                    // oldest dropped
  EXPECT_EQ(d.secondsFor(1000 + DailyReadingSeconds::MAX_DAYS + 24), 60u);  // newest kept
}

TEST(ReadingDaily, ZeroAndBadInputIgnored) {
  DailyReadingSeconds d;
  d.record(5, 0, 0);
  d.record(5, 86400, 10);
  EXPECT_TRUE(d.empty());
}

TEST(ReadingDaily, SerializeRoundTrip) {
  DailyReadingSeconds d;
  d.record(46000, 0, 1234);
  d.record(46002, 0, 99);
  const auto bytes = d.serialize();
  EXPECT_EQ(bytes.size(), 3u + 2 * 8);
  DailyReadingSeconds back;
  ASSERT_TRUE(DailyReadingSeconds::deserialize(bytes.data(), bytes.size(), back));
  EXPECT_EQ(back.secondsFor(46000), 1234u);
  EXPECT_EQ(back.secondsFor(46002), 99u);
}

TEST(ReadingDaily, RejectsDamagedFiles) {
  DailyReadingSeconds d;
  d.record(7, 0, 50);
  auto bytes = d.serialize();
  DailyReadingSeconds out;
  EXPECT_FALSE(DailyReadingSeconds::deserialize(bytes.data(), bytes.size() - 1, out));  // truncated
  auto wrongVersion = bytes;
  wrongVersion[0] = 9;
  EXPECT_FALSE(DailyReadingSeconds::deserialize(wrongVersion.data(), wrongVersion.size(), out));
  auto tooMany = bytes;
  tooMany[1] = 0xFF;
  tooMany[2] = 0xFF;
  EXPECT_FALSE(DailyReadingSeconds::deserialize(tooMany.data(), tooMany.size(), out));
  EXPECT_FALSE(DailyReadingSeconds::deserialize(nullptr, 0, out));
  // days not strictly increasing
  DailyReadingSeconds two;
  two.record(1, 0, 1);
  two.record(2, 0, 1);
  auto b2 = two.serialize();
  b2[3 + 8] = 1;  // second entry day -> 1 (same as first)
  EXPECT_FALSE(DailyReadingSeconds::deserialize(b2.data(), b2.size(), out));
  EXPECT_TRUE(out.empty());
}
