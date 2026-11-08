#include <gtest/gtest.h>

#include "TimezoneDetect/TimezoneDetect.h"

using timezone_detect::parseOffsetQuarters;

TEST(TimezoneDetect, ParsesCommonOffsets) {
  uint8_t b = 0;
  ASSERT_TRUE(parseOffsetQuarters(R"({"status":"success","offset":7200})", b));
  EXPECT_EQ(b, 48 + 8);  // UTC+2
  ASSERT_TRUE(parseOffsetQuarters(R"({"status":"success","offset":10800})", b));
  EXPECT_EQ(b, 48 + 12);  // Moscow
  ASSERT_TRUE(parseOffsetQuarters(R"({"status":"success","offset":-18000})", b));
  EXPECT_EQ(b, 48 - 20);  // UTC-5
  ASSERT_TRUE(parseOffsetQuarters(R"({"status":"success","offset":0})", b));
  EXPECT_EQ(b, 48);
}

TEST(TimezoneDetect, HandlesOddZonesAndLimits) {
  uint8_t b = 0;
  ASSERT_TRUE(parseOffsetQuarters(R"({"status":"success","offset":20700})", b));
  EXPECT_EQ(b, 48 + 23);  // Nepal +5:45
  ASSERT_TRUE(parseOffsetQuarters(R"({"status":"success","offset":19800})", b));
  EXPECT_EQ(b, 48 + 22);  // India +5:30
  ASSERT_TRUE(parseOffsetQuarters(R"({"offset":50400,"status":"success"})", b));
  EXPECT_EQ(b, 104);  // +14:00
  ASSERT_TRUE(parseOffsetQuarters(R"({"status":"success","offset":-43200})", b));
  EXPECT_EQ(b, 0);  // -12:00
}

TEST(TimezoneDetect, RejectsFailuresAndGarbage) {
  uint8_t b = 77;
  EXPECT_FALSE(parseOffsetQuarters(R"({"status":"fail","message":"private range","offset":7200})", b));
  EXPECT_FALSE(parseOffsetQuarters(R"({"status":"success"})", b));
  EXPECT_FALSE(parseOffsetQuarters(R"({"status":"success","offset":"abc"})", b));
  EXPECT_FALSE(parseOffsetQuarters(R"({"status":"success","offset":7300})", b));      // not a quarter hour
  EXPECT_FALSE(parseOffsetQuarters(R"({"status":"success","offset":54000})", b));     // > +14:00
  EXPECT_FALSE(parseOffsetQuarters(R"({"status":"success","offset":-46800})", b));    // < -12:00
  EXPECT_FALSE(parseOffsetQuarters("", b));
  EXPECT_FALSE(parseOffsetQuarters("<html>captive portal</html>", b));
  EXPECT_EQ(b, 77);  // untouched on failure
}
