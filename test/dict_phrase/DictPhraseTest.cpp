#include <gtest/gtest.h>

#include "DictPhrase.h"

using dict_phrase::buildCandidates;

TEST(DictPhrase, NothingToCombine) {
  EXPECT_TRUE(buildCandidates({}).empty());
  EXPECT_TRUE(buildCandidates({"word"}).empty());
  EXPECT_TRUE(buildCandidates({"", "next"}).empty());
}

TEST(DictPhrase, TwoWords) {
  const auto c = buildCandidates({"ice", "cream"});
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0], "ice cream");
}

TEST(DictPhrase, LongestFirstUpToThree) {
  const auto c = buildCandidates({"in", "spite", "of", "everything"});
  ASSERT_EQ(c.size(), 2u);
  EXPECT_EQ(c[0], "in spite of");
  EXPECT_EQ(c[1], "in spite");
}

TEST(DictPhrase, StopsAtGap) {
  // An empty slot marks a non-word (punctuation, image): the phrase must not span it.
  const auto c = buildCandidates({"give", "up", "", "later"});
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0], "give up");
}

TEST(DictPhrase, CustomLimit) {
  const auto c = buildCandidates({"a", "b", "c", "d"}, 4);
  ASSERT_EQ(c.size(), 3u);
  EXPECT_EQ(c[0], "a b c d");
  EXPECT_EQ(c[2], "a b");
}

TEST(DictPhrase, Cyrillic) {
  const auto c = buildCandidates({"по", "сравнению", "с", "ним"});
  ASSERT_EQ(c.size(), 2u);
  EXPECT_EQ(c[0], "по сравнению с");
}
