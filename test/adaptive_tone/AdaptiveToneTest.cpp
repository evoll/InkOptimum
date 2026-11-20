#include <gtest/gtest.h>

#include <cstdint>

#include "AdaptiveTone.h"
#include "BitmapHelpers.h"

using namespace adaptive_tone;

namespace {
Points pointsFor(int lo, int hi) {
  uint32_t h[256] = {};
  h[lo] = 500;
  h[hi] = 500;
  return derivePoints(h, 1000);
}
}  // namespace

TEST(AdaptiveTone, NarrowRangeIsStretched) {
  const Points p = pointsFor(60, 190);
  ASSERT_TRUE(p.active);
  EXPECT_EQ(p.blackPoint, 60);
  EXPECT_EQ(p.whitePoint, 190);
  EXPECT_LT(apply(p, 60), 60 + 1);
  EXPECT_GT(apply(p, 190), 190);
}

TEST(AdaptiveTone, TooNarrowRangeStaysInactiveAndIdentity) {
  const Points p = pointsFor(100, 150);
  EXPECT_FALSE(p.active);
  EXPECT_EQ(apply(p, 100), 100);
}

TEST(AdaptiveTone, EmptyHistogramInactive) {
  uint32_t h[256] = {};
  EXPECT_FALSE(derivePoints(h, 0).active);
  EXPECT_FALSE(derivePoints(nullptr, 10).active);
}

TEST(AdaptiveTone, HighlightsNeverDarkened) {
  const Points p = pointsFor(60, 250);
  ASSERT_TRUE(p.active);
  for (int v = 243; v <= 255; v++) EXPECT_GE(apply(p, static_cast<uint8_t>(v)), v);
}

TEST(AdaptiveTone, MonotonicBetweenPoints) {
  const Points p = pointsFor(40, 200);
  int prev = -1;
  for (int v = 0; v < 256; v++) {
    const int o = apply(p, static_cast<uint8_t>(v));
    EXPECT_GE(o, prev);
    prev = o;
  }
}

TEST(NativeDitherer, EvenLevelsOnFlatInput) {
  AtkinsonDitherer d(4, Gray4QuantizationMode::Native);
  // 255 and 0 are exact levels: no error, no change.
  for (int x = 0; x < 4; x++) EXPECT_EQ(d.processPixel(255, x), 3);
  d.nextRow();
  for (int x = 0; x < 4; x++) EXPECT_EQ(d.processPixel(0, x), 0);
  FloydSteinbergDitherer f(1, Gray4QuantizationMode::Native);
  EXPECT_EQ(f.processPixel(170, 0), 2);
}
