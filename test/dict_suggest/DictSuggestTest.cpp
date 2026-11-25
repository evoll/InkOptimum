#include <DictSuggest.h>

#include <algorithm>

#include "gtest/gtest.h"

namespace {
bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}  // namespace

TEST(DictSuggest, LatinTransposeDeleteSubstituteInsert) {
  const auto c = dict_suggest::editCandidates("recieve");
  EXPECT_TRUE(has(c, "receive"));          // swapped
  EXPECT_TRUE(has(dict_suggest::editCandidates("helo"), "hello"));   // insertion
  EXPECT_TRUE(has(dict_suggest::editCandidates("helllo"), "hello"));  // deletion
  EXPECT_TRUE(has(dict_suggest::editCandidates("hallo"), "hello"));   // substitution
}

TEST(DictSuggest, CyrillicAndCase) {
  EXPECT_TRUE(has(dict_suggest::editCandidates("Привте"), "привет"));
  EXPECT_TRUE(has(dict_suggest::editCandidates("карова"), "корова"));
}

TEST(DictSuggest, NoSelfNoDuplicatesCapped) {
  const auto c = dict_suggest::editCandidates("hello", 50);
  EXPECT_LE(c.size(), 50u);
  EXPECT_FALSE(has(c, "hello"));
  for (size_t i = 0; i < c.size(); ++i)
    for (size_t j = i + 1; j < c.size(); ++j) EXPECT_NE(c[i], c[j]);
}

TEST(DictSuggest, RejectsUnsuitableWords) {
  EXPECT_TRUE(dict_suggest::editCandidates("ab").empty());
  EXPECT_TRUE(dict_suggest::editCandidates("abc123").empty());
  EXPECT_TRUE(dict_suggest::editCandidates("two words").empty());
  EXPECT_TRUE(dict_suggest::editCandidates("смесь mix").empty());
  EXPECT_TRUE(dict_suggest::editCandidates("日本語").empty());
}

TEST(DictSuggest, MostLikelyFirst) {
  const auto c = dict_suggest::editCandidates("teh");
  ASSERT_FALSE(c.empty());
  EXPECT_EQ(c[0], "eth");  // transpositions come first, in word order
  EXPECT_TRUE(has(c, "the"));
}
