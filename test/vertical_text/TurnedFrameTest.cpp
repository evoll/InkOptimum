// The turned frame: a vertical-rl page laid out as a horizontal one, drawn one 90-degree step from the reader's.
#include <gtest/gtest.h>

#include "Epub/FlowContext.h"
#include "Epub/TurnedFrame.h"
#include "Epub/css/CssStyle.h"

using namespace turnedframe;

TEST(TurnedFrame, FramesAreOneStepApart) {
  for (int reader = 0; reader < 4; ++reader) {
    EXPECT_EQ(reader, readerOf(contentOf(reader)));
    EXPECT_NE(reader, contentOf(reader));
  }
  EXPECT_EQ(LandscapeCounterClockwise, contentOf(Portrait));
}

TEST(TurnedFrame, APointAndItsRectAgree) {
  const int readerWidth = 480;
  // The content point (x, y) is the reader point (readerWidth - 1 - y, x).
  const Rect r = toReader(readerWidth, {10, 20, 30, 40});
  EXPECT_EQ(readerWidth - 20 - 40, r.x);
  EXPECT_EQ(10, r.y);
  EXPECT_EQ(40, r.width);
  EXPECT_EQ(30, r.height);
  const Point p = toContentPoint(readerWidth, readerWidth - 1 - 20, 10);
  EXPECT_EQ(10, p.x);
  EXPECT_EQ(20, p.y);
}

TEST(TurnedFrame, BlockZeroIsTheRightEdge) {
  // A page drawn at reader offsets (8, 12), its block extent 440 wide on a 480 screen.
  const Offsets o = contentOffsets(480, 8, 12, 440);
  EXPECT_EQ(12, o.x);
  EXPECT_EQ(480 - 8 - 440, o.y);
}

TEST(FlowContext, TheSettingAndTheCssDecide) {
  using V = VerticalText;
  EXPECT_EQ(V::Off, FlowContext::requestFor(V::On, false));
  EXPECT_EQ(V::On, FlowContext::requestFor(V::On, true));
  EXPECT_FALSE(FlowContext::verticalFor(V::Off, CssWritingMode::VerticalRl, PageProgression::Rtl));
  EXPECT_TRUE(FlowContext::verticalFor(V::On, CssWritingMode::HorizontalTb, PageProgression::Default));
  EXPECT_TRUE(FlowContext::verticalFor(V::Automatic, CssWritingMode::VerticalRl, PageProgression::Default));
  EXPECT_FALSE(FlowContext::verticalFor(V::Automatic, CssWritingMode::HorizontalTb, PageProgression::Rtl));
  EXPECT_FALSE(FlowContext::verticalFor(V::Automatic, CssWritingMode::VerticalLr, PageProgression::Default));
  EXPECT_FALSE(FlowContext::verticalFor(V::Automatic, CssWritingMode::Unset, PageProgression::Default));
  EXPECT_TRUE(FlowContext::of(V::Automatic, true, PageProgression::Default).pageProgressionRtl);
  EXPECT_FALSE(FlowContext::of(V::Off, true, PageProgression::Rtl).pageProgressionRtl);
}
