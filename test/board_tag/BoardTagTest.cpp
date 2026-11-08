#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "BoardTag/BoardTagScanner.h"

using board_tag::Scanner;
using board_tag::Verdict;

namespace {
std::string tag(const std::string& v) { return std::string("INKMOD_") + "BOARD=" + v + ";"; }

Scanner scan(const std::string& data, size_t chunk) {
  Scanner s;
  for (size_t i = 0; i < data.size(); i += chunk) {
    const size_t n = std::min(chunk, data.size() - i);
    s.feed(reinterpret_cast<const uint8_t*>(data.data()) + i, n);
  }
  return s;
}
}  // namespace

TEST(BoardTag, FindsTagInNoise) {
  const std::string img = std::string(1000, '\x07') + tag("x4pro") + std::string(500, '\xff');
  const Scanner s = scan(img, 4096);
  ASSERT_TRUE(s.found());
  EXPECT_STREQ(s.variant(), "x4pro");
}

TEST(BoardTag, FindsTagSpanningEveryChunkBoundary) {
  const std::string img = std::string(37, 'a') + tag("x4classic") + std::string(11, 'b');
  for (size_t chunk = 1; chunk <= 40; ++chunk) {
    const Scanner s = scan(img, chunk);
    ASSERT_TRUE(s.found()) << chunk;
    EXPECT_STREQ(s.variant(), "x4classic") << chunk;
  }
}

TEST(BoardTag, UntaggedWhenAbsent) {
  const Scanner s = scan(std::string(5000, 'z'), 4096);
  EXPECT_FALSE(s.found());
  EXPECT_EQ(board_tag::evaluate(s, "x4pro"), Verdict::UNTAGGED);
}

TEST(BoardTag, RestartsAfterPartialPrefix) {
  const std::string img = std::string("INKMOD_BOAR") + "INKMOD_INKMOD_BOARD=x3x4;";
  const Scanner s = scan(img, 3);
  ASSERT_TRUE(s.found());
  EXPECT_STREQ(s.variant(), "x3x4");
}

TEST(BoardTag, IgnoresMalformedAndKeepsLooking) {
  const std::string img = tag("") + tag("BAD NAME") + tag("x4pro");
  const Scanner s = scan(img, 5);
  ASSERT_TRUE(s.found());
  EXPECT_STREQ(s.variant(), "x4pro");
}

TEST(BoardTag, RejectsOverlongVariant) {
  const Scanner s = scan(tag(std::string(40, 'a')), 7);
  EXPECT_FALSE(s.found());
}

TEST(BoardTag, VerdictAndPolicy) {
  const Scanner pro = scan(tag("x4pro"), 64);
  EXPECT_EQ(board_tag::evaluate(pro, "x4pro"), Verdict::MATCH);
  EXPECT_EQ(board_tag::evaluate(pro, "x4classic"), Verdict::MISMATCH);

  EXPECT_TRUE(board_tag::allowed(Verdict::MATCH, false));
  EXPECT_FALSE(board_tag::allowed(Verdict::MISMATCH, true));
  EXPECT_FALSE(board_tag::allowed(Verdict::UNTAGGED, false));
  EXPECT_TRUE(board_tag::allowed(Verdict::UNTAGGED, true));
}
