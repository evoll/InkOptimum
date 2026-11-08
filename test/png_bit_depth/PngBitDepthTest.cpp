#include <PngBitDepth.h>
#include <gtest/gtest.h>

using png::isValidBitDepth;

TEST(PngBitDepth, GrayscaleAllowsFiveDepths) {
  for (int d : {1, 2, 4, 8, 16}) EXPECT_TRUE(isValidBitDepth(0, d)) << d;
  for (int d : {0, 3, 5, 6, 7, 9, 15, 32}) EXPECT_FALSE(isValidBitDepth(0, d)) << d;
}

TEST(PngBitDepth, PaletteStopsAtEight) {
  for (int d : {1, 2, 4, 8}) EXPECT_TRUE(isValidBitDepth(3, d)) << d;
  for (int d : {0, 3, 16}) EXPECT_FALSE(isValidBitDepth(3, d)) << d;
}

TEST(PngBitDepth, ColourTypesWithChannelsAllowEightOrSixteen) {
  for (int type : {2, 4, 6}) {
    EXPECT_TRUE(isValidBitDepth(type, 8));
    EXPECT_TRUE(isValidBitDepth(type, 16));
    EXPECT_FALSE(isValidBitDepth(type, 4));
    EXPECT_FALSE(isValidBitDepth(type, 1));
  }
}

TEST(PngBitDepth, UnknownColourTypeIsRejected) {
  EXPECT_FALSE(isValidBitDepth(1, 8));
  EXPECT_FALSE(isValidBitDepth(5, 8));
  EXPECT_FALSE(isValidBitDepth(7, 8));
}
