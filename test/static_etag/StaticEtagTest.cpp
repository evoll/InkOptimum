#include <gtest/gtest.h>

#include <cstdio>

#include "StaticEtag.h"

TEST(StaticEtag, StableAndContentSensitive) {
  const uint8_t a[] = {1, 2, 3, 4};
  const uint8_t b[] = {1, 2, 3, 5};
  EXPECT_EQ(static_etag::make(a, 4), static_etag::make(a, 4));
  EXPECT_NE(static_etag::make(a, 4), static_etag::make(b, 4));
  EXPECT_NE(static_etag::make(a, 4), static_etag::make(a, 3));
  const std::string tag = static_etag::make(a, 4);
  EXPECT_EQ(tag.front(), '"');
  EXPECT_EQ(tag.back(), '"');
}

TEST(StaticEtag, IfNoneMatchForms) {
  const std::string tag = "\"abc-4\"";
  EXPECT_TRUE(static_etag::matches("\"abc-4\"", tag));
  EXPECT_TRUE(static_etag::matches("W/\"abc-4\"", tag));
  EXPECT_TRUE(static_etag::matches("\"x\", \"abc-4\" ,\"y\"", tag));
  EXPECT_TRUE(static_etag::matches("*", tag));
  EXPECT_FALSE(static_etag::matches("\"abc-5\"", tag));
  EXPECT_FALSE(static_etag::matches("", tag));
  EXPECT_FALSE(static_etag::matches("\"abc-4\"", ""));
}
