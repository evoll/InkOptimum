#include <CssFontSize.h>
#include <gtest/gtest.h>

using css_font_size::isUnreadablySmall;

TEST(CssFontSize, WatermarkSizesAreUnreadable) {
  EXPECT_TRUE(isUnreadablySmall(CssLength{1.0f, CssUnit::Pixels}));
  EXPECT_TRUE(isUnreadablySmall(CssLength{1.0f, CssUnit::Points}));
  EXPECT_TRUE(isUnreadablySmall(CssLength{0.05f, CssUnit::Em}));
  EXPECT_TRUE(isUnreadablySmall(CssLength{5.0f, CssUnit::Percent}));
  EXPECT_TRUE(isUnreadablySmall(CssLength{1.5f, CssUnit::Pixels}));
}

TEST(CssFontSize, ReadableSizesAreNot) {
  EXPECT_FALSE(isUnreadablySmall(CssLength{0.5f, CssUnit::Em}));   // sup/sub default
  EXPECT_FALSE(isUnreadablySmall(CssLength{60.0f, CssUnit::Percent}));  // xx-small
  EXPECT_FALSE(isUnreadablySmall(CssLength{8.0f, CssUnit::Pixels}));
  EXPECT_FALSE(isUnreadablySmall(CssLength{10.0f, CssUnit::Percent}));  // a tenth stays the floor's case
  EXPECT_FALSE(isUnreadablySmall(CssLength{1.0f, CssUnit::Em}));
}

TEST(CssFontSize, ZeroAndNegativeAreNotReported) {
  EXPECT_FALSE(isUnreadablySmall(CssLength{0.0f, CssUnit::Pixels}));
  EXPECT_FALSE(isUnreadablySmall(CssLength{-1.0f, CssUnit::Pixels}));
}
