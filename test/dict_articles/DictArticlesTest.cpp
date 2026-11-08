#include <DictArticles.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

using dict_articles::Part;

namespace {
Part part(const char* head, uint32_t offset = 0) {
  Part p;
  std::strncpy(p.headword, head, sizeof(p.headword) - 1);
  p.bodyOffset = offset;
  return p;
}

// A reader that serves fixed bodies, honouring the byte limit.
struct Bodies {
  std::vector<std::string> text;
  size_t operator()(size_t i, char* dst, size_t maxBytes) const {
    const size_t n = std::min(text[i].size(), maxBytes);
    std::memcpy(dst, text[i].data(), n);
    return n;
  }
};
}  // namespace

TEST(DictArticles, OnePartIsTheArticleAlone) {
  const Part parts[] = {part("よむ")};
  char out[64];
  const size_t n = dict_articles::assemble(parts, 1, out, sizeof(out), Bodies{{"to read"}});
  EXPECT_EQ(std::string(out), "to read");
  EXPECT_EQ(n, 7u);
}

TEST(DictArticles, SeveralPartsEachGetTheirHeadwordAndABlankLine) {
  const Part parts[] = {part("よむ", 1), part("よぶ", 2)};
  char out[128];
  const size_t n = dict_articles::assemble(parts, 2, out, sizeof(out), Bodies{{"to read", "to call"}});
  EXPECT_EQ(std::string(out), "よむ\nto read\n\nよぶ\nto call");
  EXPECT_EQ(n, std::strlen(out));
}

TEST(DictArticles, ALongArticleCannotPushTheOthersOut) {
  const Part parts[] = {part("a", 1), part("b", 2), part("c", 3)};
  char out[100];
  dict_articles::assemble(parts, 3, out, sizeof(out), Bodies{{std::string(500, 'x'), "second", "third"}});
  const std::string s(out);
  EXPECT_NE(s.find("b\nsecond"), std::string::npos) << s;
  EXPECT_NE(s.find("c\nthird"), std::string::npos) << s;
  EXPECT_LT(s.size(), sizeof(out));
}

TEST(DictArticles, AnUnreadablePartLeavesNoBareLabel) {
  const Part parts[] = {part("a", 1), part("b", 2)};
  char out[64];
  dict_articles::assemble(parts, 2, out, sizeof(out), Bodies{{"", "second"}});
  EXPECT_EQ(std::string(out).find("a\n"), std::string::npos);
  EXPECT_NE(std::string(out).find("b\nsecond"), std::string::npos);
}

TEST(DictArticles, NothingReadableIsEmpty) {
  const Part parts[] = {part("a", 1), part("b", 2)};
  char out[64] = "junk";
  EXPECT_EQ(dict_articles::assemble(parts, 2, out, sizeof(out), Bodies{{"", ""}}), 0u);
  EXPECT_EQ(out[0], '\0');
  EXPECT_EQ(dict_articles::assemble(parts, 0, out, sizeof(out), Bodies{{}}), 0u);
}

TEST(DictArticles, ADuplicateIsRecognisedByOffsetOrByHeadword) {
  const Part parts[] = {part("よむ", 100), part("よぶ", 200)};
  EXPECT_TRUE(dict_articles::contains(parts, 2, 100, "other"));   // same text on disk
  EXPECT_TRUE(dict_articles::contains(parts, 2, 999, "よぶ"));    // same headword
  EXPECT_FALSE(dict_articles::contains(parts, 2, 300, "のむ"));
}

TEST(DictArticles, OutputNeverOverrunsATightBuffer) {
  const Part parts[] = {part("headword-one", 1), part("headword-two", 2), part("headword-three", 3)};
  for (size_t cap = 2; cap < 80; ++cap) {
    std::vector<char> out(cap + 8, '#');
    const size_t n = dict_articles::assemble(parts, 3, out.data(), cap, Bodies{{"alpha alpha", "beta beta", "gamma gamma"}});
    EXPECT_LT(n, cap) << cap;
    EXPECT_EQ(std::strlen(out.data()), n) << cap;
    for (size_t i = cap; i < out.size(); ++i) EXPECT_EQ(out[i], '#') << "overrun at cap " << cap;
  }
}
