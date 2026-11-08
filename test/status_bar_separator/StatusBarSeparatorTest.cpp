#include <gtest/gtest.h>

#include "StatusBarSeparator.h"

using namespace StatusBarSeparator;

TEST(StatusBarSeparator, GlyphsAndSanitize) {
  EXPECT_STREQ(glyph(Bullet), "\xE2\x80\xA2");
  EXPECT_STREQ(glyph(Dash), "\xE2\x80\x93");
  EXPECT_STREQ(glyph(Pipe), "|");
  EXPECT_STREQ(glyph(MiddleDot), "\xC2\xB7");
  EXPECT_STREQ(glyph(200), "\xE2\x80\xA2");  // out of range -> bullet
  EXPECT_EQ(sanitize(Count), Bullet);
  EXPECT_EQ(sanitize(Pipe), Pipe);
}

TEST(StatusBarSeparator, JoinSkipsEmptyParts) {
  EXPECT_EQ(join({"3/10", "", "5 min", "Title"}, Pipe), "3/10 | 5 min | Title");
  EXPECT_EQ(join({"", ""}, Dash), "");
  EXPECT_EQ(join({"only"}, MiddleDot), "only");
}
