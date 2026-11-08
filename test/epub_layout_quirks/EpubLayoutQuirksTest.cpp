#include <gtest/gtest.h>

#include "LayoutQuirks.h"

using namespace epub_layout;

TEST(EpubLayoutQuirks, PackKeepsImageRenderingAndModeSeparate) {
  ornamentMode = ORNAMENT_STANDARD;
  EXPECT_EQ(packImageRendering(2), 2);
  ornamentMode = ORNAMENT_COMPACT;
  EXPECT_EQ(packImageRendering(0), 4);
  ornamentMode = ORNAMENT_COMPACT_ALIGNED;
  EXPECT_EQ(packImageRendering(1), 9);
  ornamentMode = 77;  // corrupt value falls back to compact
  EXPECT_EQ(packImageRendering(0), 4);
}

TEST(EpubLayoutQuirks, ModeChangeInvalidatesCacheKey) {
  ornamentMode = ORNAMENT_COMPACT;
  const uint8_t a = packImageRendering(0);
  ornamentMode = ORNAMENT_STANDARD;
  EXPECT_NE(a, packImageRendering(0));
}

TEST(EpubLayoutQuirks, ImageXHonoursAlignmentOnlyInAlignedMode) {
  ornamentMode = ORNAMENT_COMPACT;
  EXPECT_EQ(imageX(480, 100, 10, 20, 1, true), 190);
  ornamentMode = ORNAMENT_COMPACT_ALIGNED;
  EXPECT_EQ(imageX(480, 100, 10, 20, 1, true), 10);     // left
  EXPECT_EQ(imageX(480, 100, 10, 20, 3, true), 360);    // right
  EXPECT_EQ(imageX(480, 100, 10, 20, 2, true), 190);    // centre
  EXPECT_EQ(imageX(480, 100, 10, 20, 0, true), 190);    // justify -> centre
  EXPECT_EQ(imageX(480, 100, 10, 20, 1, false), 190);   // not set by the book -> centre
  ornamentMode = ORNAMENT_COMPACT;
}
