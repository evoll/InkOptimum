#include <CjkBreak.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "hyphenation/Hyphenator.h"

using cjk_break::breakAllowed;

TEST(CjkBreak, IdeographsBreakAnywhere) {
  EXPECT_TRUE(breakAllowed(0x4F60, 0x597D));  // 你 | 好
  EXPECT_TRUE(breakAllowed(0x3042, 0x3044));  // あ | い
  EXPECT_TRUE(breakAllowed(0xAC00, 0xAC01));  // Hangul syllables
}

TEST(CjkBreak, ClosingPunctuationNeverStartsALine) {
  for (const uint32_t closer : {0x3002u, 0x3001u, 0xFF0Cu, 0xFF01u, 0xFF1Fu, 0xFF09u, 0x300Du, 0x300Fu, 0x3011u, 0x201Du,
                                0x30FBu, 0x30FCu, 0x2026u}) {
    EXPECT_FALSE(breakAllowed(0x597D, closer)) << std::hex << closer;
  }
}

TEST(CjkBreak, OpeningBracketNeverEndsALine) {
  for (const uint32_t opener : {0x300Cu, 0x300Eu, 0xFF08u, 0x3010u, 0x300Au, 0x201Cu}) {
    EXPECT_FALSE(breakAllowed(opener, 0x4F60)) << std::hex << opener;
  }
}

TEST(CjkBreak, SmallKanaAndIterationMarksStayWithTheirNeighbour) {
  EXPECT_FALSE(breakAllowed(0x30AD, 0x30E3));  // キ | ャ
  EXPECT_FALSE(breakAllowed(0x4EBA, 0x3005));  // 人 | 々
  EXPECT_TRUE(breakAllowed(0x30E3, 0x30AD));   // ャ | キ is fine
}

TEST(CjkBreak, DashAndEllipsisPairsAreNotSplit) {
  EXPECT_FALSE(breakAllowed(0x2014, 0x2014));
  EXPECT_FALSE(breakAllowed(0x2026, 0x2026));
  EXPECT_FALSE(breakAllowed(0x2015, 0x2015));
}

TEST(CjkBreak, AsciiClosersAfterCjkAndOpenersBeforeIt) {
  EXPECT_FALSE(breakAllowed(0x597D, ')'));
  EXPECT_FALSE(breakAllowed(0x597D, '.'));
  EXPECT_FALSE(breakAllowed('(', 0x597D));
  EXPECT_TRUE(breakAllowed(0x597D, '('));  // a break before an opener is fine
  EXPECT_TRUE(breakAllowed(')', 0x597D));
}

TEST(CjkBreak, LatinPairsAreNotJudged) {
  EXPECT_TRUE(breakAllowed('a', 'b'));
  EXPECT_TRUE(breakAllowed('a', ')'));  // no CJK involved: other rules decide
  EXPECT_TRUE(breakAllowed(0x2018, 'a'));
}

namespace {
// Code point index of every break the hyphenator offers for `word`.
std::vector<size_t> breakCodePoints(const std::string& word) {
  std::vector<size_t> out;
  for (const auto& b : Hyphenator::breakOffsets(word, true)) {
    size_t cps = 0;
    for (size_t i = 0; i < b.byteOffset; ++i) {
      if ((static_cast<unsigned char>(word[i]) & 0xC0) != 0x80) ++cps;
    }
    out.push_back(cps);
  }
  return out;
}
bool has(const std::vector<size_t>& v, size_t x) {
  for (auto e : v) if (e == x) return true;
  return false;
}
}  // namespace

TEST(CjkBreakHyphenator, NoBreakBeforeStopOrAfterOpeningBracket) {
  // 他说「你好。」我走了  -> code points: 他0 说1 「2 你3 好4 。5 」6 我7 走8 了9
  const auto breaks = breakCodePoints("他说「你好。」我走了");
  EXPECT_TRUE(has(breaks, 2));    // 说 | 「
  EXPECT_FALSE(has(breaks, 3));   // 「 | 你  would end a line on an opener
  EXPECT_TRUE(has(breaks, 4));    // 你 | 好
  EXPECT_FALSE(has(breaks, 5));   // 好 | 。
  EXPECT_FALSE(has(breaks, 6));   // 。 | 」
  EXPECT_TRUE(has(breaks, 7));    // 」 | 我
}

TEST(CjkBreakHyphenator, CjkBreaksNeedNoHyphen) {
  for (const auto& b : Hyphenator::breakOffsets("你好世界朋友", true)) EXPECT_FALSE(b.requiresInsertedHyphen);
}
