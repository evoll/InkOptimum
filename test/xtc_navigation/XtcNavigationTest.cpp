#include <gtest/gtest.h>

#include "XtcReaderNavigation.h"

TEST(XtcNavigation, PercentMapsToNearestPage) {
  EXPECT_EQ(xtcPageForPercent(0.0f, 200), 0u);
  EXPECT_EQ(xtcPageForPercent(50.0f, 200), 99u);   // page 100 of 200 is 50%
  EXPECT_EQ(xtcPageForPercent(100.0f, 200), 199u);  // the last bitmap, not the end screen
  EXPECT_EQ(xtcPageForPercent(140.0f, 200), 199u);
  EXPECT_EQ(xtcPageForPercent(-5.0f, 200), 0u);
  EXPECT_EQ(xtcPageForPercent(50.0f, 0), 0u);
}
